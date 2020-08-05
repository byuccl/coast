/*
 * cloning.cpp
 *
 * This file contains the functions necessary for the cloning logic in dataflowProtection
 */

#include "dataflowProtection.h"

#include <algorithm>
#include <deque>
#include <vector>
#include <string>
#include <list>

#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/DIBuilder.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Analysis/AliasSetTracker.h>
#include <llvm-c/Core.h>

using namespace llvm;


// Arrays of function pointers are partially developed
#define NO_FN_PTR_ARRAY

// Command line options
extern std::list<std::string> clGlobalsToRuntimeInit;
extern std::list<std::string> ignoreGlbl;
extern std::list<std::string> skipLibCalls;
extern std::list<std::string> coarseGrainedUserFunctions;
extern std::list<std::string> protectedLib;
extern cl::opt<bool> noMemReplicationFlag;
extern cl::opt<bool> verboseFlag;
extern cl::opt<bool> noCloneOperandsCheckFlag;

// other shared variables
extern std::set<StoreInst*> syncGlobalStores;
extern std::map<Function*, std::set<int> > noXmrArgList;

/* There are some functions that are not supported.
 * It is in here instead of the config file because we don't want users touching it.
 * TODO: with recent changes to COAST, it may be possible to support these (cloneAfterCall)
 */
std::set<std::string> unsupportedFunctions = {"fscanf", "scanf", "fgets", "gets", "sscanf", "__isoc99_fscanf"};

// for debug info
static DIBuilder* dBuilder = nullptr;

/*
 * NOTE: look at Function::hasAddressTaken() as a way to see if uses of functions are calls or not
 */

//----------------------------------------------------------------------------//
// Initialization
//----------------------------------------------------------------------------//
void dataflowProtection::populateValuesToClone(Module& M) {
	// Some pointers become stale. Therefore, second set of Instructions that is not volatile
	//  contains the instructions marked as such by the annotations.
	instsToClone.clear();
	instsToClone.insert(instsToCloneAnno.begin(), instsToCloneAnno.end());
	constantExprToClone.clear();

	static std::set<Value*> warnValueLater;

	// make sure DIBuilder set up
	if (dBuilder == nullptr) {
		dBuilder = new DIBuilder(M);
	}

	for (auto F : fnsToClone) {
		if (isCoarseGrainedFunction(F->getName())) {
//			errs() << F->getName() << " is coarse grained. Not replicating.\n";
			continue;
		}

		for (auto & bb : *F) {
			for (auto & I : bb) {

				if (willBeSkipped(&I)) {
//					errs() << "Not cloning instruction " << I << "\n";
					continue;
				}

				// If store instructions not cloned, skip them
				if (noMemReplicationFlag) {
					if (dyn_cast<StoreInst>(&I)) {
						continue;
					}
				}

				if (CallInst * ci = dyn_cast<CallInst>(&I)) {

					// Don't touch/clone inline assembly
					if (ci->isInlineAsm()) {
						continue;
					}

					// Skip special clone after call
					if (cloneAfterCallArgMap.find(ci) != cloneAfterCallArgMap.end()) {
						continue;
					}

					// Clone constants in the function call
					for (unsigned int i = 0; i < ci->getNumArgOperands(); i++) {
						Value * arg = ci->getArgOperand(i);
						if (ConstantExpr * e = dyn_cast<ConstantExpr>(arg)) {
							constantExprToClone.insert(e);
						}
					}

					// skip bitcasts and print a warning message, because this might skip more than bitcasts
					if (!isIndirectFunctionCall(ci, "populateValuesToClone", false)) {
						Function* cF = ci->getCalledFunction();

						// C standard library header atomics.h is not supported
						if (cF->getName().startswith("atomic_")) {
							errs() << err_string << " function \"" << cF->getName() << "\" not supported in.\n";
							errs() << "COAST does not work well with atomic operations.\n";

							std::exit(-1);
							assert(false && "Atomic instructions not supported");
						}

						if (std::find(skipLibCalls.begin(), skipLibCalls.end(),
								cF->getName()) != skipLibCalls.end()) {
//							errs() << "Skipping the libcall " << cF->getName() << "\n";
							continue;
						}

						// Only replicate coarseGrained user functions
						if ( !(cF->hasExternalLinkage() && cF->isDeclaration()) ) {
							if (!isCoarseGrainedFunction(cF->getName())) {
//								errs() << cF->getName() << " is coarse-grained user function\n";
								continue;
							}
						}

						if (!isCoarseGrainedFunction(cF->getName())) {
							// If this isn't in the list of function calls to clone,
							//  and it's a declaration
							if (cF->isDeclaration()) {
								// If none of the operands are going to be cloned,
								//  then don't need to clone the instruction itself
								bool opsWillBeCloned = false;
								for (unsigned opNum = 0; opNum < ci->getNumOperands(); opNum++) {
									auto op = ci->getOperand(opNum);
									if (willBeCloned(op)) {
										opsWillBeCloned = true;
										break;
									}
								}
								if (!opsWillBeCloned) {
									continue;
								}
							}
						}

						// skip replicating debug function calls, the debugger only knows about the
						//  original variable names anyway.
						if (cF->getName().startswith_lower("llvm.dbg.") ||
								cF->getName().startswith_lower("llvm.lifetime.")) {
							continue;
						}

					} else {	// it is an indirect function call

						Value* calledValue = ci->getCalledValue();

						if (auto* cexpr = dyn_cast<ConstantExpr>(calledValue)) {

							// then see if we've got a name for a function in there
							if (Function* indirectF = dyn_cast<Function>(calledValue->stripPointerCasts())) {
								StringRef indirectName = indirectF->getName();
//								errs() << "The name of the indirect function called is " << indirectName << "\n";

								// perform the same checks as above for the function name
								if (std::find(skipLibCalls.begin(), skipLibCalls.end(),
										indirectF->getName()) != skipLibCalls.end()) {
									continue;
								}
								if ( !(indirectF->hasExternalLinkage() && indirectF->isDeclaration()) ) {
									if (!isCoarseGrainedFunction(indirectName)) {
										continue;
									}
								}
							}

							// see if we've got a bitcast
							if (cexpr->isCast()) {
								// TODO
								errs() << "We have found a bitcast:\n";
								errs() << "\t" << *calledValue << "\n";
							}

						}
						// if not, print some kind of warning message
						else {
							if (warnValueLater.find(calledValue) == warnValueLater.end()) {
								if (verboseFlag) {
									errs() << warn_string << " unidentified indirect function call is being added to the clone list:\n";
									errs() << *calledValue << "\n";
								}
								warnValueLater.insert(calledValue);
							}
						}

					}

				}

				// We don't clone terminators
				// Invoke is "designed to operate as a standard call instruction in most regards" - don't clone
				if (I.isTerminator() || isa<InvokeInst>(I)) {
					// we do need to clone the invokes if the function they call is marked as coarse-grained
					if (InvokeInst* invInst = dyn_cast<InvokeInst>(&I)) {
						if (isCoarseGrainedFunction(invInst->getCalledFunction()->getName())) {
							;	// add it to the list
						} else {
							continue;
						}
					} else {
						continue;
					}
				}

				// Don't clone stores to external globals - assumed to be devices
				if (StoreInst* SI = dyn_cast<StoreInst>(&I)) {
					if (GlobalVariable* GV = dyn_cast<GlobalVariable>(SI->getPointerOperand())) {
						assert(GV && "GV?");
						if (GV->hasExternalLinkage() && !(GV->hasInitializer())) {
							continue;
						}
					}
				}

				// don't clone landingpad instructions; there can only be one at the head of a basic block
				if (isa<LandingPadInst>(&I)) {
					continue;
				}

				instsToClone.insert(&I);
			}
		}
	}

	for (GlobalVariable & g : M.getGlobalList()) {
		StringRef globalName = g.getName();

		if (globalName.startswith("llvm")) {
//			errs() << "WARNING: not duplicating global value " << g.getName() << ", assuming it is llvm-created\n";
			continue;
		}

		// Don't clone ISR function pointers
		if (g.getType()->isPointerTy() && g.getNumOperands() == 1) {
			auto gVal = g.getOperand(0);
			if (auto gFuncVal = dyn_cast<Function>(gVal)) {
				if (isISR(*gFuncVal)) {
					continue;
				}
			}
		}

		// Externally available globals without initializer -> external global
		if (g.hasExternalLinkage() && !g.hasInitializer())
			continue;

		if (globalsToSkip.find(&g) != globalsToSkip.end()) {
//			errs() << "WARNING: not duplicating global variable " << g.getName() << "\n";
			continue;
		}

		if (std::find(ignoreGlbl.begin(), ignoreGlbl.end(), g.getName().str()) != ignoreGlbl.end()) {
			continue;
		}

		if (xMR_default) {
			globalsToClone.insert(&g);
		}
	}

}


//----------------------------------------------------------------------------//
// Modify functions
//----------------------------------------------------------------------------//
void dataflowProtection::populateFnWorklist(Module& M) {

	// Populate a set with all user-defined functions
	std::set<Function*> fnList;
	for (auto & fn_it : M) {
		// check for unsupported functions
		if (unsupportedFunctions.find(fn_it.getName()) != unsupportedFunctions.end()) {
			errs() << err_string << "\n    " << fn_it.getName() << ": function is not supported!\n\n\n";
			// don't quit, because application writer may have way of dealing with it
		}

		// Ignore library calls
		if (fn_it.isDeclaration()) {
			continue;
		}

		// Don't erase ISRs
		if (isISR(fn_it)) {
			continue;
		}

		// skip user marked coarse-grained functions
		if (isCoarseGrainedFunction(fn_it.getName())) {
			continue;
		}

		fnList.insert(&fn_it);
	}


	// Get a list of all the functions that should not be modified because they
	//  are related to fnToSkip
	bool fnsAdded = true;
	std::set<CallInst*> skippedIndirectCalls;

	while (fnsAdded) {
		fnsAdded = false;
		for (auto & F : fnsToSkip) {
			for (auto & bb : *F) {
				for (auto & I : bb) {
					if (CallInst* CI = dyn_cast<CallInst>(&I)) {
						if (CI->isInlineAsm()) {
							continue;
						}
						// Skip any thing that doesn't have a called function, print warnings
						if (isIndirectFunctionCall(CI, "populateFnWorklist", false)) {
							skippedIndirectCalls.insert(CI);
							continue;
						}
						Function* calledF = CI->getCalledFunction();
						if (calledF->isDeclaration()) {
							continue;
						} else if (fnsToSkip.find(calledF) == fnsToSkip.end()) {
							// Add anything that inherits from a function marked to be skipped
							if (fnsToClone.find(calledF) != fnsToClone.end()) {
								// unless is specifically marked to be cloned
								continue;
							}
							fnsToSkip.insert(calledF);
							fnsAdded = true;
						}
					}
				}
			}
		}
	}

	// Iterate through the fnsToErase list and remove them from the main function list
	for (auto & e : fnsToSkip) {
		fnList.erase(e);
	}

	// Get a list of all the functions that should be modified
	// Start with main, and look at subfunctions
	fnsAdded = true;
	Function* mainF = M.getFunction("main");

	if (xMR_default) {
		// If we don't have a main(), insert all found functions
		if (!mainF) {
			fnsToClone = fnList;
		// or if user said to skip main()
		} else if (fnsToSkip.find(mainF) != fnsToSkip.end()) {
			fnsToClone = fnList;
		// otherwise, visit all descendants of main()
		} else {
			fnsToClone.insert(mainF);
			while (fnsAdded) {
				fnsAdded = false;
				for (auto & F : fnsToClone) {
					for (auto & bb : *F) {
						for (auto & I : bb) {
							if (CallInst* CI = dyn_cast<CallInst>(&I)) {
								if (CI->isInlineAsm())
									continue;
								// skip any thing that doesn't have a called function and print warning
								if (isIndirectFunctionCall(CI, "populateFnWorklist", false)) {
									skippedIndirectCalls.insert(CI);
									continue;
								}
								if (CI->getCalledFunction()->isDeclaration())
									continue;
								else if (fnsToSkip.find(CI->getCalledFunction()) != fnsToSkip.end())
									continue;
								else if (fnsToClone.find(CI->getCalledFunction()) == fnsToClone.end()) {
									fnsToClone.insert(CI->getCalledFunction());
									fnsAdded = true;
								}
							}
						}
					}
				}
			}
		}
	}

	// print warnings
	if (skippedIndirectCalls.size() > 0) {
		errs() << warn_string << " skipping indirect function calls in populateFnWorklist:\n";
		for (auto CI : skippedIndirectCalls) {
			PRINT_VALUE(CI);
		}
	}

	// Get a list of all functions that are meant to be both cloned and skipped
	for (auto & skip_it: fnsToSkip) {
		if (fnsToClone.find(skip_it) != fnsToClone.end())
			fnsToCloneAndSkip.insert(skip_it);
	}

	// Make sure coarse grained functions aren't modified
	for (auto it : fnsToClone) {
		if (isCoarseGrainedFunction(it->getName())) {
			fnsToClone.erase(it);
		}
	}

}


/*
 * Copies the attributes from the old attribute to the new one.
 * (Things like nocapture, readonly, etc)
 */
static void addArgAttrs(Function* orig, Function* cloned, unsigned int orig_idx, unsigned int new_idx) {
	Attribute::AttrKind attr_kind;
	unsigned int endNum = static_cast<unsigned int>(Attribute::AttrKind::EndAttrKinds);

	for (unsigned int idx = 0; idx != endNum; idx++) {
		attr_kind = static_cast<Attribute::AttrKind>(idx);

		if (orig->hasParamAttribute(orig_idx, attr_kind)) {
			// according to Atrributes.cpp:1342, there are some we can't copy
			if (attr_kind == Attribute::AttrKind::Dereferenceable) {
				// TODO: is there another way to add it?
				continue;
			}
			cloned->addParamAttr(new_idx, attr_kind);
//			errs() << "Adding " << attr_kind << " to '" << cloned->getName() << "' argument " << new_idx << "\n";
		}
	}
}


/*
 * We maintain some lists of instructions that will be dealt with later in the pass,
 *  but after the functions that will be cloned have been.  Some of the instructions
 *  were marked in the original function body.  We need to add the equivalent
 *  instructions from the new function body to the same lists.
 *
 * TODO: there may be more lists we need to look at
 */
void dataflowProtection::updateInstLists(Function* F, Function* Fnew) {
	// BasicBlock iterators
	auto bbOld = F->begin();		auto oldEnd = F->end();
	auto bbNew = Fnew->begin();		auto newEnd = Fnew->end();
	for (; bbOld != oldEnd && bbNew != newEnd; ++bbOld, ++bbNew) {
		// Instruction iterators
		auto iOld = bbOld->begin();		auto iOldEnd = bbOld->end();
		auto iNew = bbNew->begin();		auto iNewEnd = bbNew->end();
		for (; iOld != iOldEnd && iNew != iNewEnd; ++iOld, ++iNew) {
			if (StoreInst* si = dyn_cast<StoreInst>(&*iOld))  {
				if (syncGlobalStores.find(si) != syncGlobalStores.end()) {
					// add to list
					syncGlobalStores.insert(dyn_cast<StoreInst>(&*iNew));
					// see if we should remove the original
					if (fnsToCloneAndSkip.find(F) == fnsToCloneAndSkip.end()) {
						syncGlobalStores.erase(si);
						// errs() << "removing from syncGlobalStores:\n";
						// PRINT_VALUE(si);
					}
				}
			}
		}
	}
}


// #define DBG_CLN_FN_ARGS
void dataflowProtection::cloneFunctionArguments(Module & M) {
	std::vector<Function*> functionsToFix;
	int warnedFnPtrs = 0;
	// since the functionality is now broken into 2 parts, we have to
	//  keep track of some values across the for loops
	typedef std::tuple< Function*, std::vector<bool> > funcArg_t;
	std::map<Function*, funcArg_t> newFuncArgsMap;

	// If we aren't replicating everything by default then don't update fn sig
	// There won't be any clones to pass into it
	#ifdef DBG_CLN_FN_ARGS
	int debugFlag = 0;
	#endif

	// a list of aggregates which are users of functions
	// these will be used later to skip users of functions that are not CallInsts
	std::list<ConstantAggregate*> skipAggList;

	for (auto g_it = M.global_begin(); g_it != M.global_end(); g_it++) {
		// we're looking for a particular global that causes problems
		if (g_it->getName() == "llvm.global_ctors") {
			// all the operands of this global
			for (auto op = g_it->op_begin(); op != g_it->op_end(); op++) {
//				errs() << *(*op) << "\n";
				// see if it's a ConstantArray
				if (auto cnst = dyn_cast<ConstantArray>(*op)) {
					// look at all of its operands
					for (auto op2 = cnst->op_begin(); op2 != cnst->op_end(); op2++) {
//						errs() << *(*op2) << "\n";
						// see if the operand of the array is an aggregate type
						if (auto agg = dyn_cast<ConstantAggregate>(*op2)) {
							// look at all the operands of the aggregate
							for (auto op3 = agg->op_begin(); op3 != agg->op_end(); op3++) {
								// if one of these operands is a function, then keep track of the aggregate
								if (auto opf = dyn_cast<Function>(*op3)) {
									skipAggList.push_back(agg);
//									errs() << opf->getName() << "\n";
								}
							}
						}
					}
				}
			}
		}
	}

	for (auto F : fnsToClone) {
		if (!F->isDeclaration()) {
			functionsToFix.push_back(F);
		}
	}

	for (auto F : functionsToFix) {
		unsigned int numArgs = F->arg_size();

		#ifdef DBG_CLN_FN_ARGS
		if (F->getName() == "ff_fprintf") {
			errs() << "Found function '" << F->getName() << "'\n";
			debugFlag = 1;
		} else {
			debugFlag = 0;
		}
		#endif

		if (isISR(*F)) {
			continue;
		}

		// do not alter the function signatures for ones that will be a "library" call
		if (protectedLibList.find(F) != protectedLibList.end()) {
			continue;
		}

		if (fnsToSkip.find(F) != fnsToSkip.end()) {
			// it can be in both
			if (fnsToClone.find(F) == fnsToClone.end()) {
				#ifdef DBG_CLN_FN_ARGS
				if (debugFlag) {
					PRINT_STRING("marked to skip this function");
				}
				#endif
				continue;
			}
		}

		if (verboseFlag) {
			errs() << "Adding clone arguments to function: " << F->getName() << "\n";
		}

		std::vector<bool> cloneArg(numArgs, false);

		// See if what is passed in has a clone
		for (auto u : F->users()) {

			// Ignore global annotations - globals containing bitcasts
			if (auto ce = dyn_cast<ConstantExpr>(u)) {
				if (ce->isCast()) {
//					errs() << "WARNING: In cloneFnArgs in cloning.cpp\n";
//					errs() << "    " << *u << " is a user/cast of fn " << F->getName() << ", skipping it\n";
					continue;
				}
			}

			// see if it's used in an aggregate type constructor global variable
			if (std::find(skipAggList.begin(), skipAggList.end(), u) != skipAggList.end()) {
//				errs() << info_string << " Skipping " << *u << "\n";
				continue;
			}

			// check for aliases and skip them
			if (isa<GlobalAlias>(u)) {
				if (verboseFlag) {
					errs() << info_string << " Skipping global alias in cloneFunctionArguments()\n";
				}
				continue;
			}

			// check for invoke instructions
			if (InvokeInst* invInst = dyn_cast<InvokeInst>(u)) {
				if (verboseFlag) {
					errs() << info_string << " Synchronizing on an InvokeInst\n";
//					errs() << *u << "\n";
				}
				/*
				 * The only functions called by an invoke instruction that can be protected are
				 * user-defined functions. If this is a library call, it must not be touched,
				 * because we can't change the body of library functions, and invoke instructions
				 * are treated as terminator instructions, so we can't just replicate the call.
				 * However, library calls shouldn't show up in this part of the pass.
				 */

				// clone the operands
				for (unsigned int i = 0; i < invInst->getNumArgOperands(); i++) {
					if (willBeCloned(invInst->getArgOperand(i))) {
						cloneArg[i] = true;
					}
				}
				continue;
			}

			// Handle arrays of function pointers by marking what should be modified
			if (ConstantArray* ca = dyn_cast<ConstantArray>(u)) {
				#ifndef NO_FN_PTR_ARRAY
				for(int i=0; i<ca->getNumOperands(); i++){
					if(ca->getOperand(i)->getName() == F->getName()){
						int index = -1;
						for(auto arg=F->arg_begin(); arg!=F->arg_end(); arg++){
							index++;
							cloneArg[index] = true;
						}
					}
				}
				#endif
				continue;
			}

			CallInst * callInst = dyn_cast<CallInst>(u);
			if (!callInst) {
				// then it's probably something with function pointers
				if (verboseFlag) {
					if (!warnedFnPtrs) {
						errs() << warn_string << " function pointers (" << F->getName();
						errs() << ") are not supported by COAST.  Use at your own risk\n";
						warnedFnPtrs = 1;
					}
					errs() << *u << "\n";
				}
				continue;
			}
			assert(callInst && "User is not a call instruction");

			// It's possible that the function user is not actually a call to the function, but a call
			//  to some other function that passes this one as a parameter.
			Function* CF = callInst->getCalledFunction();
			if (CF != F) {
				#ifdef DBG_CLN_FN_ARGS
				if (debugFlag) {
					errs() << " > " << *u << "\n";
				}
				#endif
				continue;
			}

			for (unsigned int i = 0; i < callInst->getNumArgOperands(); i++) {
				if (willBeCloned(callInst->getArgOperand(i))) {
					cloneArg[i] = true;
				}
				// override: user directives could force certain arguments to
				//  *not* be cloned
				if (noXmrArgList.find(F) != noXmrArgList.end()) {
					if (noXmrArgList[F].find(i) != noXmrArgList[F].end()) {
						cloneArg[i] = false;
					}
				}
			}
		}
		warnedFnPtrs = 0;

		// Check if any parameters need clones
		bool needClones = false;
		for (auto b : cloneArg) {
			needClones |= b;
		}

		if (!needClones) {
			#ifdef DBG_CLN_FN_ARGS
			if (debugFlag) {
				PRINT_STRING("Doesn't need clones!");
			}
			#endif
			continue;
		}

		// Now clone the function arguments
		std::vector<Type*> params;
		for (unsigned int i = 0; i < F->getFunctionType()->params().size(); i++) {

			Type* nextType = F->getFunctionType()->getParamType(i);
			params.push_back(nextType);

			if (cloneArg[i]) {
				params.push_back(nextType);
				if (TMR) {
					params.push_back(nextType);
				}
			}
		}

		ArrayRef<Type*> paramArray(params);

		FunctionType* oldFtype = F->getFunctionType();
		FunctionType* Ftype = FunctionType::get(
				oldFtype->getReturnType(), paramArray, oldFtype->isVarArg());

		std::string Fname;
		if (!TMR)
			Fname= F->getName().str() + "_DWC";
		else
			Fname= F->getName().str() + "_TMR";
		Constant * c = M.getOrInsertFunction(Fname, Ftype);
		Function * Fnew = dyn_cast<Function>(c);
		assert(Fnew && "New function is non-void");

		unsigned int i = 0;

		auto argIt = F->arg_begin();
		auto argItNew = Fnew->arg_begin();

		ValueToValueMapTy paramMap;

		while (i < numArgs) {
			argItNew->setName(argIt->getName());

			Argument* argNew = &*argItNew;
			Argument* arg = &*argIt;
			paramMap[arg] = argNew;

			if (cloneArg[i]) {
				argItNew++;
				argItNew->setName(argIt->getName() + "_DWC");
				Value* v1 = &*argItNew;

				Value* v2;
				if (TMR) {
					argItNew++;
					argItNew->setName(argIt->getName() + "_TMR");
					v2 = &*argItNew;
				}

				cloneMap[argNew] = ValuePair(v1,v2);
			}
			argIt++;
			argItNew++;
			i++;
		}

//		errs() << "Function arguments: \n";
//		for(auto p = Fnew->arg_begin(); p != Fnew->arg_end(); p++){
//			errs() << *p << "\n";
//		}
//		errs() << "\n";


		SmallVector<ReturnInst*, 8> returns;
		CloneFunctionInto(Fnew, F, paramMap, true, returns);
		origFunctions.push_back(F);
		fnsToClone.insert(Fnew);
		fnsToClone.erase(F);
//		errs() << "\nReplacing " << F->getName() << " with " << Fnew->getName() << "\n";

		// if there's an entry already for this, it's from cloneFunctionReturnVals
		if (functionMap.find(F) != functionMap.end()) {
			functionMap[Fnew] = functionMap[F];
		}
		functionMap[F] = Fnew;

		// also need to update the replReturn set
		if (replReturn.find(F) != replReturn.end()) {
			replReturn.insert(Fnew);
			replReturn.erase(F);
			// We need to remove the old one from the list here
			// Because there should only be one version of it in the list
		}

		/*
		 * This is needed because we clone functions into new functions while updating references.
		 * Occasionally, functions had been cloned but instsToClone hadn't been updated,
		 *  so nothing in the new function was listed in instsToClone.
		 * This led to the pass refusing to replace the cloned arguments in calls when
		 *  the call lived in a new function, because none of the insts in it were in instsToClone.
		 */
		populateValuesToClone(M);
		// there are also some special lists that may need to be updated
		updateInstLists(F, Fnew);

		// TODO: might want to break up this whole function right here into 2 parts
		//  so that replacing calls all takes place after the function clones have
		//  been created
		newFuncArgsMap[F] = funcArg_t(Fnew, cloneArg);
	}

	for (auto F : functionsToFix) {
		// only do this if it's in the map (right?)
		if (newFuncArgsMap.find(F) == newFuncArgsMap.end())
			continue;

		// set up values
		unsigned int numArgs = F->arg_size();
		funcArg_t funcArgs = newFuncArgsMap[F];
		Function* Fnew = std::get<0>(funcArgs);
		std::vector<bool> cloneArg = std::get<1>(funcArgs);

//		errs() << "Function: " << F->getName() << "\n";
		// Replace all function calls
		for (auto u : F->users()) {
			// Check for bitcasts in case of annotations
			if (auto ce = dyn_cast<ConstantExpr>(u)) {
				if (ce->isCast()) {
					continue;
				}
			}

			#ifdef DBG_CLN_FN_ARGS
			if (F->getName() == "ff_fprintf") {
				debugFlag = 1;
			} else {
				debugFlag = 0;
			}
			#endif

//			errs() << "original function call: " << *u << "\n";

			std::vector<Value*> args;

			// Account for arrays of fn pointers
			unsigned int j = 0;
			if (ConstantArray* ca = dyn_cast<ConstantArray>(u)) {
				for (int i=0; i<ca->getNumOperands(); i++) {
					if (ca->getOperand(i)->getName() == F->getName()) {
						int index = -1;
						for (auto arg=F->arg_begin(); arg!=F->arg_end(); arg++) {
							index++;
							args.push_back(dyn_cast<Value>(arg));
							if (cloneArg[index]) {
								if (willBeCloned(dyn_cast<Value>(arg))) {
									argNumsCloned[Fnew].push_back(j);
								}
								args.push_back(dyn_cast<Value>(arg));
								j++;
								if (TMR) {
									args.push_back(dyn_cast<Value>(arg));
									j++;
								}
							}
						}
						ArrayRef<Value*>* callArgs;
						callArgs = new ArrayRef<Value*>(args);
						ca->setOperand(i,Fnew);
					}
				}

			} else if (CallInst* callInst = dyn_cast<CallInst>(u)) {
				assert(callInst && "Replacing function calls in cloneFnArgs");

				Function* parentFn = callInst->getParent()->getParent();
				// this is the right check, because original functions were removed from this set,
				//  and their clones added to it
				if (fnsToClone.find(parentFn) == fnsToClone.end()) {
					continue;
				}
				#ifdef DBG_CLN_FN_ARGS
				if (debugFlag) {
					errs() << " > " << *callInst << " in '" << parentFn->getName() << "'\n";
				}
				#endif

				// If the use of the function is actually a function pointer *in* the call,
				//  then need to skip doing anything to this one.
				// NOTE: possible error if calling a function passes
				//  the called function as a parameter as well (unlikely)
				if (callInst->getCalledFunction() != F) {
					continue;
				}

				for (unsigned int i = 0; i < numArgs; i++, j++) {
					Value * argOrig = callInst->getArgOperand(i);
					args.push_back(argOrig);
					if (cloneArg[i]) {
						if (willBeCloned(argOrig)) {
							argNumsCloned[Fnew].push_back(j);
						}
						args.push_back(argOrig);
						j++;
						if (TMR) {
							args.push_back(argOrig);
							j++;
						}
					}
				}

				/*
				 * Special check for calls to variadic functions.
				 * Make sure to add the extra arguments to the new function call.
				 */
				if (F->isVarArg() && (callInst->getNumArgOperands() > numArgs)) {
					#ifdef DBG_CLN_FN_ARGS
					if (debugFlag) {
						errs() << " - orig call has " << callInst->getNumArgOperands() << " arguments\n";
						errs() << " - new var arg? " << Fnew->isVarArg() << "\n";
					}
					#endif
					// add the rest
					for (unsigned int i = numArgs; i < callInst->getNumArgOperands(); i++) {
						Value* extraArg = callInst->getArgOperand(i);
						args.push_back(extraArg);
					}
				}

				ArrayRef<Value*>* callArgs;
				callArgs = new ArrayRef<Value*>(args);
				CallInst* newCallInst;

				// Turns out that void returning function calls have no name, so have to be careful here.
				// There may be other cases where it doesn't have a name; check those too.
				if ( (Fnew->getReturnType() == Type::getVoidTy(M.getContext())) ||
					 (!callInst->hasName()) )
				{
					newCallInst = CallInst::Create((Value*) Fnew, *callArgs);
					newCallInst->insertBefore(callInst);
				} else {
					// The casting here is to stop Eclipse from complaining that the Create call doesn't have the right types
					newCallInst = CallInst::Create((Value*) Fnew, *callArgs,
							Twine(callInst->getName()), (Instruction*) callInst);
				}

				// Deal with function calls inside function args when casted - not recognized as callInsts
				for (auto ops : newCallInst->operand_values()) {
					if (auto ce = dyn_cast<ConstantExpr>(ops)) {
						if (ce->isCast()) {
							assert(ce->getNumOperands() == 1 && "Setting the arg of a cast");
							Function* oldFn = dyn_cast<Function>(ce->getOperand(0));
							if (functionMap[oldFn]) {
								ce->setOperand(0,functionMap[oldFn]);
							}
						}
					}
				}

				// if there's debug information for the call, preserve it
				if (auto dbgLoc = callInst->getDebugLoc()) {
					newCallInst->setDebugLoc(dbgLoc);
				}

				// Replace all uses of the original call instruction with the new one
				if (callInst->getType() != newCallInst->getType()) {
					if (F->hasName()) {
						errs() << "Looking at function '" << F->getName() << "'\n";
						errs() << *F->getFunctionType() << "\n";
					}
					errs() << *callInst << "\n";
					errs() << *newCallInst << "\n";
				}
				assert(callInst->getType() == newCallInst->getType());

				callInst->replaceAllUsesWith(newCallInst);
				callInst->eraseFromParent();
			} else if (InvokeInst* invInst = dyn_cast<InvokeInst>(u)) {
				assert(invInst && "Replacing function calls in cloneFnArgs");

				Function* parentFn = invInst->getParent()->getParent();
				if (fnsToClone.find(parentFn) == fnsToClone.end()) {
					continue;
				}

				for (unsigned int i = 0; i < numArgs; i++, j++) {
					Value * argOrig = invInst->getArgOperand(i);
					args.push_back(argOrig);
					if (cloneArg[i]) {
						if (willBeCloned(argOrig)) {
							argNumsCloned[Fnew].push_back(j);
						}
						args.push_back(argOrig);
						j++;
						if (TMR) {
							args.push_back(argOrig);
							j++;
						}
					}
				}

				ArrayRef<Value*>* callArgs;
				callArgs = new ArrayRef<Value*>(args);

				// The casting here is to stop Eclipse from complaining that the Create call doesn't have the right types
				InvokeInst* newInvInst = InvokeInst::Create((Value*) Fnew, invInst->getNormalDest(),
						invInst->getUnwindDest(), *callArgs, Twine(invInst->getName()), (Instruction*) invInst);

				// Deal with function calls inside function args when casted - not recognized as callInsts
				for (auto ops : newInvInst->operand_values()) {
					if (auto ce = dyn_cast<ConstantExpr>(ops)) {
						if (ce->isCast()) {
							assert(ce->getNumOperands() == 1 && "Setting the arg of a cast");
							Function* oldFn = dyn_cast<Function>(ce->getOperand(0));
							if (functionMap[oldFn]) {
								ce->setOperand(0,functionMap[oldFn]);
							}
						}
					}
				}

				// preserve debug information
				if (auto dbgLoc = invInst->getDebugLoc()) {
					newInvInst->setDebugLoc(dbgLoc);
				}

				// Replace all uses of the original call instruction with the new one
				invInst->replaceAllUsesWith(newInvInst);
				invInst->eraseFromParent();
			} else {
				assert(false && "wrong type!\n");
//				TODO: what would cause this to fail?
			}

		}

		// update the metadata describing the new function
		cloneMetadata(M, Fnew);

		// copy all of the function attributes to the cloned attributes
		unsigned int i = 0;
		unsigned int j = 0;

		while (i < numArgs) {
			if (cloneArg[i]) {
				j++;
				addArgAttrs(F, Fnew, i, j);

				if (TMR) {
					j++;
					addArgAttrs(F, Fnew, i, j);
				}
			}
			i++;
			j++;
		}

	}

	#ifndef NO_FN_PTR_ARRAY
	// Update any arrays of function pointers
	// They are stored as global arrays
	for(GlobalVariable & g : M.getGlobalList()){
		if(g.hasPrivateLinkage() && g.hasAtLeastLocalUnnamedAddr()){
			if(g.getNumOperands() == 1){
				if(ConstantArray* ca = dyn_cast<ConstantArray>(g.getOperand(0))){

					ArrayType* arrTy = ArrayType::get(ca->getOperand(0)->getType(),ca->getNumOperands());
					std::vector<Constant*> arg_type_v;
					for(int i=0;i<ca->getNumOperands();i++){
						arg_type_v.push_back(cast<Constant>(ca->getOperand(i)));
					}
					ArrayRef<Constant*> args = ArrayRef<Constant*>(arg_type_v);

					GlobalVariable* gNew = new GlobalVariable(M, arrTy, g.isConstant(), g.getLinkage(),
						ConstantArray::get(arrTy,args), g.getName() + "_new", &g);

					for(auto gUse : g.users()){
						for(int j=0;j<gUse->getNumOperands();j++){
							if(GlobalVariable* gv = dyn_cast<GlobalVariable>(gUse->getOperand(j))){
								if(gv == &g){
									gUse->getOperand(j)->mutateType(gNew->getType());
								}
							}
						}
					}

					g.replaceAllUsesWith(gNew);
					gNew->setName(g.getName());
				}
			}
		}
	}
	/*To finish function pointer support:
	 * For arrays of function pointers, the following are implemented:
     	 * The function signatures are modified properly
     	 * The global array of pointers points to the proper functions
     	 * The global array of points has the proper type
     	 * The users of the global array of pointers have the proper type
	 * Remaining problem:
	 * The typing on function points is key. Updating the function in the users breaks any instructions
	 * that are indirect users of the result, because they have the incorrect type. LLVM does not give
	 * you any convenient way to find these instructions, because they are not users. As a result, we do
	 * not support this. This also leads to a failed assertion when compiling.
	 * When you fix this, feel free to get rid of the NO_FN_PTR_ARRAY flag.
	 * To test this, run with -dumpModule.
	 * It will print out the entire module, which you can paste into an *.ll file and run with lli.
	 * NOTE: Function->hasAddressTaken() could be useful
	 */
	 #endif

}


/*
 * There is a command line argument called "cloneReturn" that specifies functions which
 *  should have their return values replicated.
 * We will implement this by changing the function signature to include 2 new
 *  pointers at the beginning where the extra return values can be stored.
 * The call sites will be changed to allocate this space.
 * NOTE: should we do anything differently based on the calling convention?
 *
 * The reason this is called before cloneInsns() is because we want the functions to
 *  exist so that the clones will actually be in the cloneMap for later.
 * There is another function that will finish up the functionality for this.
 */
void dataflowProtection::cloneFunctionReturnVals(Module& M) {
	for (Function* F : replReturn) {
		// things about the current function
		Type* retType = F->getReturnType();
		if (retType->isVoidTy()) {
			// skip void functions
			errs() << warn_string << " cannot replicate return values of function '"
				   << F->getName() << "' because it is a void type\n";
			continue;
		}
		FunctionType* fType = F->getFunctionType();
		auto argList = fType->params();

		/*
		 * Add 2 new arguments to the parameter list.
		 * These will go at the end of the current list of parameters.
		 * Even if this is a variadic function, extra variables aren't in the function signature;
		 *  they show up in the function call, if at all.
		 * Since these values will only be used on entrance/exit of the function, it will reduce
		 *  register spilling by keeping these arguments in the stack instead of something that
		 *  is used more frequently.
		 */
		Type* newRetType = retType->getPointerTo();
		std::vector<Type*> newParams;
		// add old args
		for (auto oldArg : fType->params()) {
			newParams.push_back(oldArg);
		}
		// add new return value args
		newParams.push_back(newRetType);
		if (TMR)
			newParams.push_back(newRetType);

		// new function type, same arguments
		FunctionType* newFuncType = fType->get(retType,     	   	/* return type */
											   newParams,   		/* arguments */
											   fType->isVarArg());	/* variadic */

		// create a new function
		Function* newFunc = F->Create(newFuncType,					/* function type */
									  F->getLinkage(),				/* linkage */
							  		  F->getName() + ".RR",			/* name */
									  &M);

		// set up stuff for copying
		ValueToValueMapTy paramMap;
		SmallVector<ReturnInst*, 8> returns;
		unsigned int i = 0;
		unsigned int numArgs = F->arg_size();
		auto argIt = F->arg_begin();
		auto argItNew = newFunc->arg_begin();

		while (i < numArgs) {
			// similar to in cloneFunctionArguments, but 1:1 mapping
			argItNew->setName(argIt->getName());

			Argument* argNew = &*argItNew;
			Argument* arg = &*argIt;
			paramMap[arg] = argNew;

			// see if it's in the clone map
			if (isCloned(arg)) {
				Value *v1, *v2;
				v1 = &*(argItNew + 1);
				if (TMR)
					v2 = &*(argItNew + 2);
				cloneMap[argNew] = ValuePair(v1, v2);
			}

			argIt++;
			argItNew++;
			i++;
		}

		// add the new return pointers
		argItNew->setName("__retVal.DWC");
		argItNew++;
		if (TMR) {
			argItNew->setName("__retVal.TMR");
			argItNew++;
		}

		// copy the body of the function
		CloneFunctionInto(newFunc, F, paramMap, true, returns);

		// find the last alloca in the entry block
		BasicBlock& entryBB = newFunc->getEntryBlock();
		replRetMap[F] = returns;
		functionMap[F] = newFunc;

		// record these things
		fnsToClone.insert(newFunc);
		if (verboseFlag) {
			errs() << info_string << " Created new function named '"
				   << newFunc->getName() << "'\n";
		}
	}
}

/* 
 * If there are instructions that are nested that both show up in the replReturn list,
 *  then we need to save un-fixed use checking until after all the updates have run.
 * This includes removing unused functions, because we expect the original function
 *  to be removed.
 * See validateRRFuncs()
 */
static std::set<Instruction*> checkUsesLater;

// #define DEBUG_CHANGE_RR_CALLS
/*
 * Finish updating the functions that are marked to replicate return values,
 *  as well as the associated call sites.
 * Probably isn't a case where we want the original as well as the changed function
 */
void dataflowProtection::updateRRFuncs(Module& M) {

	for (auto& kv : replRetMap) {

        ////////////////////// unpack information //////////////////////

		Function* F = kv.first;
		Function* rrFunc = functionMap[F];
		SmallVector<ReturnInst*, 8> returns = kv.second;

		// errs() << "Looking at function '" << rrFunc->getName() << "'\n";
		Type* newRetType = rrFunc->getReturnType()->getPointerTo();
		BasicBlock& entryBB = rrFunc->getEntryBlock();

		// get a handle for the first iterator
		auto argIt = rrFunc->arg_end();
		argIt--;
		if (TMR) argIt--;

        //////////////////////// allocate addr /////////////////////////

		// we can now insert the alloca's, because they won't get xMR'd here
		AllocaInst* alloc1, * alloc2;
		Constant* one = ConstantInt::get(
			IntegerType::getInt32Ty(M.getContext()), 1, false);
		unsigned int addrSpace = M.getDataLayout().getAllocaAddrSpace();
		unsigned int alignNum = M.getDataLayout().getPrefTypeAlignment(newRetType);
		alloc1 = new AllocaInst(
			newRetType,		/* Type */
			addrSpace,		/* AddrSpace */
			one,			/* Value* ArraySize */
			alignNum,		/* Align */
			Twine((argIt)->getName() + ".addr")
		);
		alloc1->insertBefore(&*entryBB.getFirstInsertionPt());

		if (TMR) {
			alloc2 = new AllocaInst(
				newRetType,		/* Type */
				addrSpace,		/* AddrSpace */
				one,			/* Value* ArraySize */
				alignNum,		/* Align */
				Twine((argIt+1)->getName() + ".addr")
			);
			alloc2->insertAfter(alloc1);
		}

        //////////////////////// store ptr addr ////////////////////////

		// store the addresses
		StoreInst* storeRetAddr1, * storeRetAddr2;
		storeRetAddr1 = new StoreInst(&*argIt, alloc1);
		storeRetAddr1->insertAfter(alloc1);
		storeRetAddr1->setAlignment(alignNum);
		if (TMR) {
			storeRetAddr2 = new StoreInst(&*(argIt+1), alloc2);
			storeRetAddr2->insertAfter(alloc2);
			storeRetAddr2->setAlignment(alignNum);
		}

        ///////////////////// change return points /////////////////////

		for (auto ret : returns) {
			Value* retVal = ret->getReturnValue();
			ValuePair retClones = getClone(retVal);

			// load the argument
			LoadInst* loadRet = new LoadInst(alloc1, "loadRet", ret);
			// PRINT_VALUE(loadRet);
			// store the copy
			StoreInst* storeRet = new StoreInst(
				retClones.first,	/* value to store */
				loadRet,			/* pointer where to store */
				ret					/* InsertBefore */
			);
			StoreInst* storeRet2;
			if (TMR) {
				LoadInst* loadRet2 = new LoadInst(alloc2, "loadRet2", ret);
				storeRet2 = new StoreInst(
					retClones.second,	/* value to store */
					loadRet2,			/* pointer where to store */
					ret					/* InsertBefore */
				);
			}

			// change how it's registered as part of synchronization logic
			//  so segmenting works
			auto retIt = startOfSyncLogic.find(ret);
			if (retIt == startOfSyncLogic.end()) {
				syncPoints.push_back(ret);
				// if not specific spot already, make it the load
				startOfSyncLogic[ret] = loadRet;
			} else if (retIt->second == ret) {
				startOfSyncLogic[ret] = loadRet;
			}

			// also register as clones
			cloneMap[ret] = ValuePair(storeRet, storeRet2);
			// PRINT_VALUE(storeRet);
			// if (ret->getParent()->getName() == "prvInitialiseMutex.exit")
			// 	PRINT_VALUE(ret->getParent());
		}

        ////////////////////// change call sites ///////////////////////

		// change the call sites - based on code in cloneFunctionArguments
		for (auto u : F->users()) {
			// Check for bitcasts in case of annotations
			if (auto ce = dyn_cast<ConstantExpr>(u)) {
				if (ce->isCast()) {
					continue;
				}
			}

			// we won't deal with arrays of function pointers here just yet
			CallInst* callInst = dyn_cast<CallInst>(u);
			InvokeInst* invInst = dyn_cast<InvokeInst>(u);

			if (callInst || invInst) {

                ///////////////// ones to skip /////////////////

				// skip ones that aren't being cloned
				Function* parentFn = callInst->getParent()->getParent();
				if (fnsToClone.find(parentFn) == fnsToClone.end()) {
					continue;
				}

				// If the use of the function is actually a function pointer *in*
				//  the call, then need to skip doing anything to this CallInst.
				if (callInst && (callInst->getCalledFunction() != F)) {
					continue;
				}

				// get entry point of current function
				StringRef callName;
				BasicBlock* callEntryBB;
				Instruction* oldInst;
				if (callInst) {
					callEntryBB = &callInst->getParent()->getParent()->getEntryBlock();
					callName = callInst->getName();
					oldInst = callInst;
				} else {
					callEntryBB = &invInst->getParent()->getParent()->getEntryBlock();
					callName = invInst->getName();
					oldInst = invInst;
				}

                /////////////// call site alloca ///////////////
				Type* normalRetType = rrFunc->getReturnType();

				// allocate some space for the new return value pointers
				AllocaInst* callAlloca1, * callAlloca2;
				callAlloca1 = new AllocaInst(
					normalRetType,	/* Type */
					addrSpace,		/* AddrSpace */
					one,			/* Value* ArraySize */
					alignNum,		/* Align */
					Twine(callName + ".DWC.addr")
				);
				callAlloca1->insertBefore(&*(*callEntryBB).getFirstInsertionPt());
				if (TMR) {
					callAlloca2 = new AllocaInst(
						normalRetType,	/* Type */
						addrSpace,		/* AddrSpace */
						one,			/* Value* ArraySize */
						alignNum,		/* Align */
						Twine(callName + ".TMR.addr")
					);
					callAlloca2->insertAfter(callAlloca1);
				}
				// PRINT_VALUE(callAlloca1);

                ///////////////// create call //////////////////

				Instruction* newInst;
				if (callInst) {
					// create call arg list
					std::vector<Value*> args;
					// add existing arguments
					for (unsigned i = 0; i < callInst->getNumArgOperands(); i++) {
						args.push_back(callInst->getArgOperand(i));
					}
					// finish arg list
					args.push_back(callAlloca1);
					if (TMR) {
						args.push_back(callAlloca2);
					}
					ArrayRef<Value*> callArgs(args);

					// make the new call instruction
					CallInst* newCallInst;
					if (!callInst->hasName()) {
						// In some strange cases, the call may not have a name, even though it's not a void function.
						newCallInst = CallInst::Create(
							rrFunc,							/* Function to call */
							callArgs						/* argument list */
						);
						newCallInst->insertBefore(callInst);
					} else {
						newCallInst = CallInst::Create(
							rrFunc,							/* Function to call */
							callArgs,						/* argument list */
							Twine(callInst->getName()),		/* name */
							callInst						/* InsertBefore */
						);
					}

					// do we need to worry about line 767?

					newInst = newCallInst;
				}
				else {		/* invInst */
					// create call arg list
					std::vector<Value*> args;
					// add existing arguments
					for (unsigned i = 0; i < invInst->getNumArgOperands(); i++) {
						args.push_back(invInst->getArgOperand(i));
					}
					// finish arg list
					args.push_back(callAlloca1);
					if (TMR) {
						args.push_back(callAlloca2);
					}
					ArrayRef<Value*> callArgs(args);

					InvokeInst* newInvInst = InvokeInst::Create(
						rrFunc,							/* Function to call */
						invInst->getNormalDest(),		/* IfNormal */
						invInst->getUnwindDest(),		/* IfException */
						callArgs,						/* argument list */
						Twine(invInst->getName()),		/* name */
						invInst							/* InsertBefore */
					);

					// do we need to worry about line 829?

					newInst = newInvInst;
				}
				// PRINT_VALUE(newInst);

				// copy debug info
				if (auto dbgLoc = oldInst->getDebugLoc()) {
					newInst->setDebugLoc(dbgLoc);
				}

                ////////////// load return clones //////////////

				LoadInst* loadRet1 = new LoadInst(
						callAlloca1, newInst->getName() + ".DWC");
				loadRet1->insertAfter(newInst);
				LoadInst* loadRet2;
				if (TMR) {
					loadRet2 = new LoadInst(
							callAlloca2, newInst->getName() + ".TMR");
					loadRet2->insertAfter(loadRet1);
				}
				// register them as clones
				cloneMap[newInst] = ValuePair(loadRet1, loadRet2);
				
				// PRINT_VALUE(loadRet1);

                ///////////////// replace uses /////////////////

				#ifdef DEBUG_CHANGE_RR_CALLS
				int debugThis = false;
				if (callName == "call1" && callInst->getCalledFunction()->getName() == "xQueueGenericCreate_TMR") {
					debugThis = true;
					// PRINT_VALUE(oldInst->getParent()->getParent());
					errs() << "In function '" << oldInst->getParent()->getParent()->getName() << "'\n";
				}
				#endif
				/*
				 * This is trickier than just doing "replaceAllUsesWith()", because we
				 *  have to replace the uses that are clones with the correct value.
				 *
				 * Somehow the original instruction is sometimes not in the user list, but the clones are.
				 * I don't know why that would happen, but we'll have to work around that, as stupid
				 *  as that is.
				 * The call to getCloneOrig() should help detect if the original is missing
				 *  from the user list.
				 * First, make a list (set) of all Values to look at.
				 * Then replace the operands accordingly.
				 */
				std::set<User*> checkTheseUses;
				std::set<Instruction*> callUses;

				for (auto use : oldInst->users()) {
					// normal lookup
					if (isCloned(use)) {
						checkTheseUses.insert(use);
					} else {
						// inverse lookup
						Value* origUse = getCloneOrig(use);
						if (auto U = dyn_cast_or_null<User>(origUse)) {
							checkTheseUses.insert(U);
						}
						// Also check for call instructions (and presumably invoke as well)
						//  for which the operands are the oldInst
						else if (auto callUse = dyn_cast<CallInst>(use)) {
							// errs() << " &> Found call use:\n" << *callUse << "\n";
							callUses.insert(callUse);
						}
						else if (auto invokeUse = dyn_cast<InvokeInst>(use)) {
							callUses.insert(invokeUse);
						}
						#ifdef DEBUG_CHANGE_RR_CALLS
						else if (debugThis) {
							errs() << "leftover: " << *use << "\n";
						}
						#endif
					}
				}

				// specially handle call uses
				for (auto instUse : callUses) {
					// either call or invoke - because can't instantiate CallBase
					auto callUse = dyn_cast<CallInst>(instUse);
					auto invokeUse = dyn_cast<InvokeInst>(instUse);
					// get called function
					Function* Fcalled;
					if (callUse)
						Fcalled = callUse->getCalledFunction();
					else
						Fcalled = invokeUse->getCalledFunction();
					// iterate over operands
					for (unsigned opNum = 0; opNum < instUse->getNumOperands(); opNum++) {
						Value* op = instUse->getOperand(opNum);
						if (op == oldInst) {
							// at least replace the old one
							instUse->setOperand(opNum, newInst);
							// now check if the args themselves are cloned
							if (Fcalled) {
								auto argsCloned = argNumsCloned[Fcalled];
								// if the vector contains opNum, then change the next 1/2 args
								if (std::count(argsCloned.begin(), argsCloned.end(), opNum)) {
									auto clones = getClone(newInst);
									instUse->setOperand(opNum+1, clones.first);
									if (TMR) {
										instUse->setOperand(opNum+2, clones.second);
									}
								}
							}
						}
					}
				}

				// Now check all of these uses and replace with the new instruction
				for (auto use : checkTheseUses) {
					ValuePair clones = getClone(use);
					std::vector<unsigned> replaceIdxs;

					// replace original uses and record indices
					for (unsigned opNum = 0; opNum < use->getNumOperands(); opNum++) {
						Value* op = use->getOperand(opNum);
						if (op == oldInst) {
							use->setOperand(opNum, newInst);
							replaceIdxs.push_back(opNum);
						}
					}
					#ifdef DEBUG_CHANGE_RR_CALLS
					if (debugThis && replaceIdxs.size() < 1) {
						errs() << "size too small!\n";
					}
					#endif

					// unpack clones
					Instruction* c1, * c2;
					c1 = dyn_cast<Instruction>(clones.first);
					if (TMR) {
						c2 = dyn_cast<Instruction>(clones.second);
						assert(c2 && "clone exists");
					}

					// replace the clones using marked indices
					for (unsigned opNum : replaceIdxs) {
						c1->setOperand(opNum, loadRet1);
						#ifdef DEBUG_CHANGE_RR_CALLS
						if (debugThis) {
							errs() << "setting op " << opNum << " of " << *c1 << "\n";
						}
						#endif
						if (TMR) {
							c2->setOperand(opNum, loadRet2);
						}
					}
				}

				// remove old call - try now
				if (oldInst->use_empty()) {
					oldInst->eraseFromParent();
				} else {
					// if it doesn't work, try again later
					checkUsesLater.insert(oldInst);
				}
			}
		}
	}
}


/*
 * Checks to make sure old calls to functions that have had their
 *  return values replicated have been removed successfully.
 */
void dataflowProtection::validateRRFuncs(void) {
	bool foundProblem = false;
	// Now we can remove the old instructions
	for (auto oldInst : checkUsesLater) {
		if (oldInst && (oldInst->use_empty()) ) {
			// also check if the parent was already removed
			BasicBlock* parentBB = oldInst->getParent();
			if (parentBB) {
				Function* parentF = parentBB->getParent();
				if (parentF) {
					oldInst->eraseFromParent();
				}
			}
		}
		else {
			errs() << "Still have uses for " << *oldInst << "\n";
			for (auto U : oldInst->users()) {
				PRINT_VALUE(U);
			}
			Function* parentF = oldInst->getParent()->getParent();
			errs() << "in " << (parentF->getName()) << "\n";
			for (auto use : parentF->users()) {
				errs() << "  - " << *use << "\n";
			}
			foundProblem = true;
		}
	}

	assert(!foundProblem && "must remove the original call!");
	// If your code hits this assertion, please contact the maintainers
}


// #define DBG_UPDATE_CALLS
void dataflowProtection::updateCallInsns(Module & M) {

#ifdef DBG_UPDATE_CALLS
	bool debugFlag = 0;
#endif

	for (auto &F : M) {
		// If we are skipping the function, don't update the call instructions
		if (fnsToCloneAndSkip.find(&F) != fnsToCloneAndSkip.end()) {
			if (fnsToClone.find(&F) == fnsToClone.end()) {
				continue;
			}
		}

		for (auto & bb : F) {
			for (auto & I : bb) {
				if (CallInst * CI = dyn_cast<CallInst>(&I)) {
					Function * Fcalled = CI->getCalledFunction();

					if (cloneAfterFnCall.find(Fcalled) != cloneAfterFnCall.end()) {
						// This handles cases where all of the arguments are
						//  going to be cloned.
						unsigned int numArgs = CI->getNumArgOperands();
						for (int argNum = 0; argNum < numArgs; ++argNum) {
							// get the clones
							Value* op = CI->getArgOperand(argNum);
							ValuePair clonePair = getClone(op);
							Value* clone1 = clonePair.first;
							assert(clone1 && "value is cloned!");

							// load the original
							LoadInst* loadOrig = new LoadInst(op, "loadOrig");
							loadOrig->insertAfter(CI);

							// store to the copy
							StoreInst* storeCopy = new StoreInst(
								loadOrig,			/* value to store */
								clone1				/* pointer where to store */
							);
							storeCopy->insertAfter(loadOrig);

							if (TMR) {
								// one more store instruction for TMR copy
								Value* clone2 = clonePair.second;
								assert(clone2 && "valid 2nd clone with TMR");
								StoreInst* storeCopy2 = new StoreInst(
										loadOrig, clone2);
								storeCopy2->insertAfter(storeCopy);
							}
						}
						// PRINT_VALUE(CI->getParent());
					}

					else if (cloneAfterCallArgMap.find(CI) != cloneAfterCallArgMap.end()) {
						// This handles cases where the application writer specifies
						//  certain arguments to clone-after-call.
						unsigned int numArgs = CI->getNumArgOperands();
						// iterate through the ones specified
						for (auto argNum : cloneAfterCallArgMap[CI]) {
							// check bounds
							if (argNum > (numArgs + 1)) {
								continue;
							}

							// get clone
							Value* op = CI->getArgOperand(argNum);
							ValuePair clonePair = getClone(op);
							Value* clone1 = clonePair.first;
							assert(clone1 && "value is cloned!");

							// load original
							LoadInst* loadOrig = new LoadInst(op, "loadOrig");
							loadOrig->insertAfter(CI);

							// store to copy
							StoreInst* storeCopy = new StoreInst(loadOrig, clone1);
							storeCopy->insertAfter(loadOrig);

							if (TMR) {
								// once more for TMR copy
								Value* clone2 = clonePair.second;
								assert(clone2 && "valid 2nd clone with TMR");
								StoreInst* storeCopy2 = new StoreInst(
										loadOrig, clone2);
								storeCopy2->insertAfter(storeCopy);
							}
						}
					}

					else if (argNumsCloned.find(Fcalled) != argNumsCloned.end()) {
						auto argsCloned = argNumsCloned[Fcalled];

						#ifdef DBG_UPDATE_CALLS
						if (Fcalled && (Fcalled->getName() == "ff_fprintf_TMR") ) {
							debugFlag = 1;
							errs() << " # " << *CI << "\n";
						}
						#endif

						for (auto argNum : argsCloned) {
							#ifdef DBG_UPDATE_CALLS
							if (debugFlag) {
								errs() << "arg " << argNum << "\n";
							}
							#endif
							Value* op = CI->getArgOperand(argNum);
							if (isCloned(op)) {
								Value* clone1 = getClone(op).first;
								CI->setArgOperand(argNum + 1, clone1);

								Value* clone2;
								if (TMR) {
									clone2 = getClone(op).second;
									CI->setArgOperand(argNum + 2, clone2);
								}
							}
						}
					}
					#ifdef DBG_UPDATE_CALLS
					debugFlag = 0;
					#endif
				}
			}
		}
	}
}


void dataflowProtection::updateInvokeInsns(Module & M) {

	for (auto &F : M) {
		// If we are skipping the function, don't update the call instructions
		if (fnsToCloneAndSkip.find(&F) != fnsToCloneAndSkip.end()) {
			if (fnsToClone.find(&F) == fnsToClone.end()) {
				continue;
			}
		}

		for (auto & bb : F) {
			for (auto & I : bb) {
				// also need to update Invoke instructions
				if (InvokeInst* invInst = dyn_cast<InvokeInst>(&I)) {
					Function* Fcalled = invInst->getCalledFunction();

					// clone the arguments
					if (argNumsCloned.find(Fcalled) != argNumsCloned.end()) {
						auto argsCloned = argNumsCloned[Fcalled];

						for (auto argNum : argsCloned) {
							Value* op = invInst->getArgOperand(argNum);
							if (isCloned(op)) {
								Value* clone1 = getClone(op).first;
								invInst->setArgOperand(argNum + 1, clone1);

								Value* clone2;
								if (TMR) {
									clone2 = getClone(op).second;
									invInst->setArgOperand(argNum + 2, clone2);
								}
							}
						}
					}
				}
			}
		}
	}
}


/*
 * Helper function for cloneInsns().
 * Handle changing references in complicated ConstantExpr's.
 */
void dataflowProtection::cloneConstantExprOperands(ConstantExpr* ce, InstructionPair clone, unsigned i) {
	// Don't need to update references to constant ints
	assert(ce && "Null ConstantExpr ce");
	if (isa<ConstantInt>(ce->getOperand(0))) {
		return;
	}

	/*				needed to be down lower to not filter out things too early
			if (!willBeCloned(ce->getOperand(0))) {
				continue;
			}
	 */

	// Don't mess with loads with inline GEPs
	if (noMemReplicationFlag) {
		if (ce->isGEPWithNoNotionalOverIndexing()) {
			return;
		}
	}

	/*
	 * check if it's an inline bitcast
	 * This can occur if the source code has a global array that ends with a series of 0 values
	 *  Clang will compile the code to use the 'zeroinitializer' directive, which changes the
	 *  type of the variable. Instead of having something like
	 *     @array1 = dso_local constant [64 x i8]
	 *  it will output
	 *     @array2 = dso_local constant <{ [32 x i8], [32 x i8] }>
	 * Then the call to accessing an element of this array will look like
	 * 	   %arrayidx = getelementptr inbounds [64 x i8], [64 x i8]* bitcast (<{ [32 x i8], [32 x i8] }>* @array2 to [64 x i8]*), i64 0, i64 %idxprom
	 * This only is a problem when the noMemReplication flag, therefore it's OK to skip changing
	 *  the instruction arguments, since it would all be the same argument anyway.
	 *
	 * might be an inline reference to a global variable. example:
	 * %0 = load <4 x i32>, <4 x i32>* bitcast ([2 x [8 x i32]]* @matrix to <4 x i32>*), align 16, !tbaa !2
	 *
	 * More tricky stuff:
	 * call void @llvm.memset.p0i8.i64(i8* align 4 bitcast (i32* getelementptr inbounds (%struct.block_s, %struct.block_s* @globalBlock, i64 0, i32 2, i64 0) to i8*), i8 0, i64 64, i1 false)
	 * store %struct.xMINI_LIST_ITEM* getelementptr inbounds ([4 x %struct.xLIST], [4 x %struct.xLIST]* @pxReadyTasksLists, i64 0, i64 0, i32 2), %struct.xMINI_LIST_ITEM** bitcast (%struct.xLIST_ITEM** getelementptr inbounds ([4 x %struct.xLIST], [4 x %struct.xLIST]* @pxReadyTasksLists, i64 0, i64 0, i32 1) to %struct.xMINI_LIST_ITEM**), align 8, !tbaa !9
	 * Or the worst:
	 * store <2 x %struct.xMINI_LIST_ITEM*> <%struct.xMINI_LIST_ITEM* getelementptr inbounds ([4 x %struct.xLIST], [4 x %struct.xLIST]* @pxReadyTasksLists, i64 0, i64 0, i32 2), %struct.xMINI_LIST_ITEM* getelementptr inbounds ([4 x %struct.xLIST], [4 x %struct.xLIST]* @pxReadyTasksLists, i64 0, i64 0, i32 2)>, <2 x %struct.xMINI_LIST_ITEM*>* bitcast (%struct.xLIST_ITEM** getelementptr inbounds ([4 x %struct.xLIST], [4 x %struct.xLIST]* @pxReadyTasksLists, i64 0, i64 0, i32 2, i32 1) to <2 x %struct.xMINI_LIST_ITEM*>*), align 8, !tbaa !11
	 */

	// in the following code segment, the leading underscores in names represent levels of indirection
	if (ce->isCast()) {

		if (noMemReplicationFlag)
			return;

		Value* _op = ce->getOperand(0);
		if (isCloned(_op)) {
//						errs() << *_op << "\n";
			ConstantExpr* ce1 = dyn_cast<ConstantExpr>(clone.first->getOperand(i));
			Value* _op1 = cloneMap[_op].first;
			assert(_op1 && "valid clone");
//						errs() << *_op1 << "\n";
			Constant* _nop1 = dyn_cast<Constant>(_op1);
			Constant* nce1 = ce1->getWithOperandReplaced(0, _nop1);
//						errs() << *nce1 << "\n";
			clone.first->setOperand(i, nce1);
			if (TMR) {
				ConstantExpr* ce2 = dyn_cast<ConstantExpr>(clone.second->getOperand(i));
				Value* _op2 = cloneMap[_op].second;
				assert(_op2 && "valid second clone");
				Constant* _nop2 = dyn_cast<Constant>(_op2);
				Constant* nce2 = ce2->getWithOperandReplaced(0, _nop2);
				clone.second->setOperand(i, nce2);
			}
			return;
		}
		// could be something ugly like:
		//%2 = load <4 x i32>, <4 x i32>* bitcast (i32* getelementptr inbounds ([2 x [8 x i32]], [2 x [8 x i32]]* @matrix, i64 0, i64 0, i64 4) to <4 x i32>*), align 16, !tbaa !2
		ConstantExpr* innerGEPclone1 = dyn_cast<ConstantExpr>(_op);
		if (innerGEPclone1 && innerGEPclone1->isGEPWithNoNotionalOverIndexing()) {

			// get the place to update
			ConstantExpr* innerGEPclone1 = dyn_cast<ConstantExpr>(ce->getOperand(0));
//						errs() << " - " << *innerGEPclone1 << "\n";

			// this next thing is what has the clone(s)
			Value* GEPvalOrig = innerGEPclone1->getOperand(0);

			// have to check if it's been cloned
			if (isCloned(GEPvalOrig)) {
				// get the clone
				Value* GEPvalClone1 = cloneMap[GEPvalOrig].first;
				assert(GEPvalClone1 && "valid clone");

				// replace uses
				Constant* newGEPclone1 = innerGEPclone1->getWithOperandReplaced(
						0, dyn_cast<Constant>(GEPvalClone1));
				Constant* newCE = ConstantExpr::getCast(
						ce->getOpcode(), newGEPclone1, ce->getType());
				clone.first->setOperand(i, newCE);
//				errs() << " - " << *ce << "\n";
//				errs() << " - " << *clone.first << "\n";

				if (TMR) {
					ConstantExpr* ce2 = dyn_cast<ConstantExpr>(clone.second->getOperand(i));
					ConstantExpr* innerGEPclone2 = dyn_cast<ConstantExpr>(ce2->getOperand(0));
					Value* GEPvalClone2 = cloneMap[GEPvalOrig].second;
					assert(GEPvalClone2 && "valid second clone");
					Constant* newGEPclone2 = innerGEPclone2->getWithOperandReplaced(
							0, dyn_cast<Constant>(GEPvalClone2));
					Constant* newCE2 = ConstantExpr::getCast(
							ce2->getOpcode(), newGEPclone2, ce2->getType());
					clone.second->setOperand(i, newCE2);
				}
				return;
			}
		}
		// otherwise, throw an error
		else if (verboseFlag) {
			errs() << warn_string << " In cloneInsns() skipping processing cloned ConstantExpr:\n";
			errs() << " " << *ce << "\n";
		}
		return;
	}

	if (!willBeCloned(ce->getOperand(0))) {
		return;
	}

	/*
	 * Error checking here for things missing in the cloneMap.
	 * If this is NULL, then that means we just inserted the operand
	 *  into the map, and therefore it wasn't in there before.
	 *
	 * Trying to dereference 0 is a bad idea
	 * How did this get in the list, but not in the map?
	 */
	Value* v_temp = cloneMap[ce->getOperand(0)].first;
	if (v_temp == nullptr) {
		errs() << err_string << " in cloneInsns!\n";
		errs() << *ce << "\n";
	}
	assert(v_temp && "ConstantExpr is in cloneMap");

	Constant* newOp1 = dyn_cast<Constant>(v_temp);
	assert(newOp1 && "Null Constant newOp1");
	Constant* c1 = ce->getWithOperandReplaced(0, newOp1);
	ConstantExpr* eNew1 = dyn_cast<ConstantExpr>(c1);
	assert(eNew1 && "Null ConstantExpr eNew1");
	clone.first->setOperand(i, eNew1);

	if (TMR) {
		Constant* newOp2 = dyn_cast<Constant>(cloneMap[ce->getOperand(0)].second);
		assert(newOp2 && "Null Constant newOp2");
		Constant* c2 = ce->getWithOperandReplaced(0, newOp2);
		ConstantExpr* eNew2 = dyn_cast<ConstantExpr>(c2);
		assert(eNew2 && "Null ConstantExpr eNew2");
		clone.second->setOperand(i, eNew2);
	}
}

/*
 * Helper function to clone the operands of ConstantVector's.
 * Example:
 *  <2 x %struct.xMINI_LIST_ITEM*>
 *  <
 *    %struct.xMINI_LIST_ITEM* getelementptr inbounds
 *      ([4 x %struct.xLIST], [4 x %struct.xLIST]* @pxReadyTasksLists, i64 0, i64 3, i32 2),
 *    %struct.xMINI_LIST_ITEM* getelementptr inbounds
 *      ([4 x %struct.xLIST], [4 x %struct.xLIST]* @pxReadyTasksLists, i64 0, i64 3, i32 2)
 *  >
 *
 * NOTE: if this ever updates to LLVM 10, we will need to change how we're getting the number of elements.
 */
void dataflowProtection::cloneConstantVectorOperands(ConstantVector* constVec, InstructionPair clone, unsigned i) {

	// how many elements in the vector
	VectorType* vType = dyn_cast<VectorType>(constVec->getType());
	unsigned elemCount = vType->getVectorNumElements();
//	errs() << " * " << *constVec << "\n";
//	errs() << "constant vector type with " << elemCount << " elements:\n";

	// initialize some vectors which will later be converted into new constants
	std::vector<Constant*> newVecArray_DWC(elemCount, nullptr);
	std::vector<Constant*> newVecArray_TMR(elemCount, nullptr);

	// look at all the elements in the vector
	for (unsigned int k = 0; k < elemCount; k++) {
		Constant* vc = constVec->getAggregateElement(k);

		if (auto vc_const = dyn_cast<ConstantExpr>(vc)) {
			if (vc_const->isGEPWithNoNotionalOverIndexing()) {
				Value* _op = vc_const->getOperand(0);

				if (isCloned(_op)) {
//					errs() << *_op << "\n";
					Value* _op1 = cloneMap[_op].first;
					assert(_op1 && "valid clone");
					Constant* _nop1 = dyn_cast<Constant>(_op1);
//					errs() << *_nop1 << "\n";

					// clone of the vector
					ConstantVector* constVec_clone = dyn_cast<ConstantVector>(clone.first->getOperand(i));
//					errs() << *constVec_clone << "\n";
					// get vector element
					Constant* vc_clone = constVec_clone->getAggregateElement(k);
					ConstantExpr* vc_clone_expr = dyn_cast<ConstantExpr>(vc_clone);

					// get a new constant with the operand replaced with correct cloned value
					Constant* vc_clone_new = vc_clone_expr->getWithOperandReplaced(0, _nop1);
//					errs() << *vc_clone_new << "\n";

					// here's our new constant GEP that goes inside a vector
					newVecArray_DWC[k] = vc_clone_new;
//					errs() << *constVec_clone << "\n";

					if (TMR) {
						Value* _op2 = cloneMap[_op].second;
						assert(_op2 && "valid clone");
						Constant* _nop2 = dyn_cast<Constant>(_op2);

						ConstantVector* constVec_clone2 = dyn_cast<ConstantVector>(clone.second->getOperand(i));
						ConstantExpr* vc_clone_expr2 = dyn_cast<ConstantExpr>(constVec_clone2->getAggregateElement(k));

						Constant* vc_clone_new2 = vc_clone_expr2->getWithOperandReplaced(0, _nop2);
						newVecArray_TMR[k] = vc_clone_new2;
					}
				}
			}
		}

		// Anything that didn't have a reference changed, keep the same.
		if (newVecArray_DWC[k] == nullptr) {
			newVecArray_DWC[k] = vc;
		}
		if (TMR) {
			if (newVecArray_TMR[k] == nullptr) {
				newVecArray_TMR[k] = vc;
			}
		}

	}

	// Make some new constant vectors, and change the clone operands
	ArrayRef<Constant*> newVecArrayRef_DWC = ArrayRef<Constant*>(newVecArray_DWC);
	ConstantVector* newVec_DWC = dyn_cast<ConstantVector>(ConstantVector::get(newVecArrayRef_DWC));
	clone.first->setOperand(i, newVec_DWC);

	if (TMR) {
		ArrayRef<Constant*> newVecArrayRef_TMR = ArrayRef<Constant*>(newVecArray_TMR);
		ConstantVector* newVec_TMR = dyn_cast<ConstantVector>(ConstantVector::get(newVecArrayRef_TMR));
		clone.second->setOperand(i, newVec_TMR);
	}
}

extern bool comesFromSingleCall(Instruction* storeUse);


// #define DEBUGGING_CLONE_INSNS
//----------------------------------------------------------------------------//
// Fine-grained cloning of instructions
//----------------------------------------------------------------------------//
bool dataflowProtection::cloneInsns() {
	std::deque<Instruction*> cloneList;
	std::vector<InstructionPair> instsCloned;

	// Populate the clone list
	for (auto I : instsToClone) {
		Instruction* newI1;
		Instruction* newI2;
		if (InvokeInst* invInst = dyn_cast<InvokeInst>(I) ) {
			if (invInst->getCalledFunction()->getReturnType()->isVoidTy()) {
				continue;
			}
			Function* Fparent = invInst->getParent()->getParent();

			// we need to create a new basic block to branch to on success
			BasicBlock* beforeBlock = invInst->getParent();
			BasicBlock* afterBlock = invInst->getNormalDest();
			BasicBlock* landingBlock = invInst->getUnwindDest();

			const Twine& blockName1 = Fparent->getName() + ".invoke.DWC";
			BasicBlock* newBlock1 = BasicBlock::Create(Fparent->getContext(), \
					blockName1, Fparent, afterBlock);
			afterBlock = invInst->getNormalDest();

			// set original invoke to have new normal destination
			invInst->setNormalDest(newBlock1);

			// make a dummy instruction so we have somewhere to put the invoke
			ConstantInt* nothing = ConstantInt::get(IntegerType::getInt16Ty(Fparent->getContext()), 1, false);
			BinaryOperator* dummy1 = BinaryOperator::CreateNeg(nothing, "dummy1", newBlock1);

			// that contains a copy of the same invoke instruction
			InvokeInst* newInv1 = dyn_cast<InvokeInst>(invInst->clone());
			InvokeInst* newInv2;
			newInv1->setName(invInst->getName() + ".DWC");
			newInv1->insertAfter(dummy1);
			dummy1->eraseFromParent();

			// the new one will have the same unwind location
			newInv1->setUnwindDest(landingBlock);

			if (TMR) {
				const Twine& blockName2 = Fparent->getName() + ".invoke.TMR";
				BasicBlock* newBlock2 = BasicBlock::Create(Fparent->getContext(), blockName2, Fparent);
				newBlock2->moveAfter(newBlock1);

				BinaryOperator* dummy2 = BinaryOperator::CreateNeg(nothing, "dummy2", newBlock2);

				newInv2 = dyn_cast<InvokeInst>(invInst->clone());
				newInv2->setName(invInst->getName() + ".TMR");
				newInv2->insertAfter(dummy2);
				dummy2->eraseFromParent();

				newInv2->setUnwindDest(landingBlock);
				newInv1->setNormalDest(newBlock2);
				newInv2->setNormalDest(afterBlock);

//				errs() << " - new basic block:\n" << *newBlock1 << "\n";
//				errs() << " - new TMR basic block:\n" << *newBlock2 << "\n";
//				errs() << " - next:\n" << *newInv2->getNormalDest() << "\n";

				newI2 = dyn_cast<Instruction>(newInv2);
			} else {
				newInv1->setNormalDest(afterBlock);
//				errs() << " - new basic block:\n" << *newBlock1 << "\n";
			}
			// for the map
			newI1 = dyn_cast<Instruction>(newInv1);

			// debug stuff
			if (invInst->getDebugLoc()) {
				newInv1->setDebugLoc(invInst->getDebugLoc());
				if (TMR) {
					newInv2->setDebugLoc(invInst->getDebugLoc());
				}
			}

		} else {	// everything else besides InvokeInst
			// TODO: for alloca, copy/fix debug info
			newI1 = I->clone();

			if (!I->getType()->isVoidTy()) {
				newI1->setName(I->getName() + ".DWC");
			}

			newI1->insertAfter(I);

			if (TMR) {
				newI2 = I->clone();
				if (!I->getType()->isVoidTy()) {
					newI2->setName(I->getName() + ".TMR");
				}

				newI2->insertAfter(newI1);
			}
		}

		instsCloned.push_back(std::make_pair(newI1, newI2));
		cloneMap[I] = ValuePair(newI1, newI2);
	}

	// Iterate over the clone list and change references
	for (auto clone : instsCloned) {
		// Iterate over the operands in the instruction
		#ifdef DEBUGGING_CLONE_INSNS
		bool debugPrint = false;
		if (auto* debug_inst = dyn_cast<StoreInst>(clone.first)) {
			Function* parentF = debug_inst->getParent()->getParent();
			if (parentF->getName() == "core_list_mergesort_TMR.RR") {
				if (debug_inst->getParent()->getName() == "entry") {
					PRINT_VALUE(debug_inst);
					debugPrint = true;
				}
				// if (ai->getName().startswith_lower("res.addr")) {
				// 	PRINT_VALUE(debug_inst);
				// 	debugPrint = true;
				// }
			}
		}
		#endif

		for (unsigned i = 0; i < clone.first->getNumOperands(); i++) {
			// If the operand is found in the map change the reference
			Value* op = clone.first->getOperand(i);

			// skip changing basic block references on the invoke instructions,
			// we already set them up correctly above
			if (isa<InvokeInst>(clone.first) && isa<BasicBlock>(op)) {
				continue;
			}

			if (isCloned(op)) { 		// If we found it
				#ifdef DEBUGGING_CLONE_INSNS
				if (debugPrint) {
					PRINT_VALUE(op);
				}
				#endif
				if (noMemReplicationFlag) { 				// Not replicating memory
					// If we aren't replicating memory then we should not change the load inst. address
					if (dyn_cast<LoadInst>(clone.first)) { 	// Don't change load instructions
						assert(clone.first && "Clone exists when updating operand");
						clone.first->setOperand(i, op);
						if (TMR) {
							assert(clone.second && "Clone exists when updating operand");
							clone.second->setOperand(i, op);
						}
					} else { 								// Else update as normal
						clone.first->setOperand(i, cloneMap[op].first);
						if (TMR) {
							clone.second->setOperand(i, cloneMap[op].second);
						}
					}
				} else { 									// Replicating memory
					// GEPs nested inside bitcasts, and other tricky things
					if (ConstantExpr* ce = dyn_cast<ConstantExpr>(op)) {
						cloneConstantExprOperands(ce, clone, i);
					}
					// otherwise, it's simple to handle
					else {
						clone.first->setOperand(i, cloneMap[op].first);
						if (TMR) {
							clone.second->setOperand(i, cloneMap[op].second);
						}
					}
				}

			} else if (ConstantExpr* ce = dyn_cast<ConstantExpr>(op)) {
				/*
				 * Broken out into its own function.
				 */
				cloneConstantExprOperands(ce, clone, i);
			/*
			 * Sometimes there are packed vector instructions with inline GEPs to initialize them.
			 */
			} else if (op->getType()->isVectorTy()) {
				if (ConstantVector* constVec = dyn_cast<ConstantVector>(op)) {
					cloneConstantVectorOperands(constVec, clone, i);
				}

			} else {
				clone.first->setOperand(i, op);
				if (TMR) {
					assert(clone.second && "Clone exists to set operand");
					clone.second->setOperand(i, op);
				}
			}
		}
		#ifdef DEBUGGING_CLONE_INSNS
		debugPrint = false;
		#endif
	}

	return (instsToClone.size() > 0);
}

void dataflowProtection::verifyCloningSuccess() {
	if (!noMemReplicationFlag) {
		bool uhOhFlag = false;
		/*
		 * Sanity check: are any of the operands of the clones
		 *  equal to the operands of the original?
		 */
		for (auto entry : cloneMap) {
			Value* v0 = entry.first;
			if (Instruction* i0 = dyn_cast<Instruction>(v0)) {

				// Exception: comes from a single function call
				if (auto si = dyn_cast<StoreInst>(i0)) {
					if (comesFromSingleCall(si)) {
						continue;
					}
				}

				// Exception: use immediately following function argument (that isn't cloned)
				Function* parentF = i0->getParent()->getParent();
				for (auto argIter = parentF->arg_begin(); argIter != parentF->arg_end(); argIter++) {
					for (auto useIter = argIter->use_begin(); useIter != argIter->use_end(); useIter++) {
						if (cast<Value>(&*useIter) == cast<Value>(i0)) {
							continue;
						}
					}
				}

				Instruction* i1 = dyn_cast<Instruction>(entry.second.first);

				// Iterate over the operands in the instruction
				for (unsigned i = 0; i < i0->getNumOperands(); i++) {
					Value* op0 = i0->getOperand(i);
					Value* op1 = i1->getOperand(i);
					Type* opType = op0->getType();

					// Exception: instruction uses the same constant number for each operand, casted to pointer
					if (i0->isCast()) {
						opType = i0->stripPointerCasts()->getType();
					}

					// special treatment for nested constant expressions
					if (ConstantExpr* ce = dyn_cast<ConstantExpr>(op0)) {
						op0 = ce->getOperand(0);
						ConstantExpr* ce1 = dyn_cast<ConstantExpr>(op1);
						op1 = ce1->getOperand(0);
					}

					if (opType->isPointerTy() || opType->isVectorTy() || isa<ConstantExpr>(op0)) {
						if (isa<Function>(op0)) {
							continue;
						}
						// See if the operands are the same as the clone
						if (op0 == op1) {
							uhOhFlag = true;
							errs() << err_string << " operands are the same for each copy of instruction\n" << *i0 << "\n";
							// don't need to report more than once per instruction
							break;
						}
					}
				}
			}
		}

		if (uhOhFlag && !noCloneOperandsCheckFlag) {
			// by default, will exit here
			errs() << info_string << " COAST is having a hard time replicating the operands of these instructions.\n";
			errs() << "Please attempt to make the expression this comes from less complex, or contact the maintainers.\n\n";
			std::exit(-1);
		}
	}
}


//----------------------------------------------------------------------------//
// Cloning of constants
//----------------------------------------------------------------------------//
void dataflowProtection::cloneConstantExpr() {
	for (auto e : constantExprToClone) {
		if (e->isGEPWithNoNotionalOverIndexing() || e->isCast()) {
			Value* oldOp = e->getOperand(0);
			assert(isa<Constant>(oldOp));
			ValuePair clones = getClone(oldOp);

			Constant* constantOp1 = dyn_cast<Constant>(clones.first);
			assert(constantOp1);
			Constant* c1 = e->getWithOperandReplaced(0, constantOp1);
			ConstantExpr* e1 = dyn_cast<ConstantExpr>(c1);
			assert(e1);

			ConstantExpr* e2;
			if (TMR) {
				Constant* constantOp2 = dyn_cast<Constant>(clones.second);
				assert(constantOp2);
				Constant* c2 = e->getWithOperandReplaced(0, constantOp2);
				e2 = dyn_cast<ConstantExpr>(c2);
				assert(e2);
			}

			// assert(eNew->isGEPWithNoNotionalOverIndexing());
			cloneMap[e] = ValuePair(e1, e2);
		} else {
//			TODO: what could cause this to fail?
			assert(false && "Constant expr to clone not matching expected form");
		}
	}
}


//----------------------------------------------------------------------------//
// Cloning of globals
//----------------------------------------------------------------------------//
void dataflowProtection::cloneGlobals(Module & M) {

	if (noMemReplicationFlag)
		return;

	if (verboseFlag) {
		for (auto g : globalsToClone) {
			errs() << "Cloning global: " << g->getName() << "\n";
		}
	}

	// First figure out which globals will be initialized at runtime
	for (auto g : globalsToClone) {
		if (std::find(clGlobalsToRuntimeInit.begin(), clGlobalsToRuntimeInit.end(), g->getName().str()) != clGlobalsToRuntimeInit.end()) {
			globalsToRuntimeInit.insert(g);
		}
	}

	for (auto g : globalsToClone) {
		// Skip specified globals
		if (std::find(ignoreGlbl.begin(), ignoreGlbl.end(), g->getName().str()) != ignoreGlbl.end()) {
			if (verboseFlag) errs() << "Not replicating " << g->getName() << "\n";
			continue;
		}

		GlobalVariable* gNew = copyGlobal(M, g, g->getName().str() + "_DWC");

		GlobalVariable* gNew2;
		if (TMR) {
			gNew2 = copyGlobal(M, g, g->getName().str() + "_TMR");
		}

		cloneMap[g] = ValuePair(gNew, gNew2);
		/*
		 * One thing that's slightly annoying, is the ordering that these globals
		 *  end up in.  The constructor for GlobalVariable requires a parameter
		 *  `InsertBefore`.  This means the copies will be inserted in the address
		 *  space _before_ the original.
		 * TODO: why doesn't GDB know the global type? In the LLVM IR, it's the same
		 *  metadata info for the type.
		 * Perhaps one way to get around the weird address space issue would be to
		 * 	use iterators from `M.global_begin()` or something like that.
		 */
	}

}

/*
 * Creates a new global of the same type as `copyFrom`, but with name `newName` instead,
 *  and inserts the new global before `copyFrom`.
 */
GlobalVariable * dataflowProtection::copyGlobal(Module & M, GlobalVariable* copyFrom, std::string newName) {

	Constant * initializer;

	if (globalsToRuntimeInit.find(copyFrom) == globalsToRuntimeInit.end()) {
		initializer = copyFrom->getInitializer();
	} else {
		Type * initType = copyFrom->getInitializer()->getType();

		// for now we only support runtime initialization of arrays
		assert(isa<ArrayType>(initType));

		initializer = ConstantAggregateZero::get(initType);

		if (verboseFlag)	errs() << "Using zero initializer for global " << newName << "\n";

	}

	// create new global
	GlobalVariable* gNew = new GlobalVariable(
		M,							/* Module */
		copyFrom->getValueType(), 	/* Type */
		copyFrom->isConstant(), 	/* isConstant */
		copyFrom->getLinkage(),		/* Linkage */
		initializer,				/* Initializer */
		newName,					/* Name */
		copyFrom					/* InsertBefore */
	);

	// copy the other attributes
	gNew->setUnnamedAddr(copyFrom->getUnnamedAddr());
	gNew->copyAttributesFrom(copyFrom);

	// copy the debug information
	SmallVector<DIGlobalVariableExpression*, 4> debugInfo;
	copyFrom->getDebugInfo(debugInfo);
	for (auto dbg : debugInfo) {
		// we need to make a new entry for the variable name
		auto dbgVar = dbg->getVariable();

		// first, get the variable type
		DIType* varType = dyn_cast<DIType>(dbgVar->getRawType());
		// use debug info builder
		DIGlobalVariableExpression* newDbgInfo =
				dBuilder->createGlobalVariableExpression(
					dbgVar->getScope(), 		/* DIScope* Context */
					gNew->getName(), 			/* StringRef Name */
					dbgVar->getLinkageName(), 	/* StringRef LinkageName */
					dbgVar->getFile(), 			/* DIFile* File */
					dbgVar->getLine(), 			/* unsigned LineNo */
					varType, 					/* DIType* Ty */
					dbgVar->isLocalToUnit(), 	/* bool isLocalToUnit */
					/* bool isDefined - not in the version we're using */
					dbg->getExpression() 		/* DIExpression* Expr */
					/* MDNode* Decl=nullptr */
					/* MDTuple* TemplateParams=nullptr
					    - not in the version we're using */
					/* uint32_t AlignInBits=0 */
				);
		// errs() << *newDbgInfo << "\n"
		// 	   << *(newDbgInfo->getVariable()) << "\n";

		gNew->addDebugInfo(newDbgInfo);
	}

	if (verboseFlag)
		errs() << "New duplicate global: " << gNew->getName() << "\n";

	return gNew;
}


/*
 * For all globals that need to be initialized at runtime, insert memcpy calls at the start of main
 */
void dataflowProtection::addGlobalRuntimeInit(Module & M) {
	for (auto g : globalsToRuntimeInit) {
		ArrayType * arrayType = dyn_cast<ArrayType>(g->getType()->getContainedType(0));

		assert(arrayType);

		std::vector<Type *> arg_type_v;
		arg_type_v.push_back(Type::getInt8PtrTy(M.getContext()));
		arg_type_v.push_back(Type::getInt8PtrTy(M.getContext()));
		arg_type_v.push_back(Type::getInt64Ty(M.getContext()));
		ArrayRef<Type*> arg_type = ArrayRef<Type*>(arg_type_v);

		// this is an Eclipse error because the definition comes from a built file with an include guard,
		//  but this is a correct enum in Intrinsic.
		Function * fun = Intrinsic::getDeclaration(&M, Intrinsic::memcpy, arg_type);
		IRBuilder<> Builder(&(*(M.getFunction("main")->begin()->begin())));

		std::vector<Value *> args_v;

		// 1st argument is destination pointer (cast to i8*)
		args_v.push_back(ConstantExpr::getBitCast(cast<Constant>(cloneMap[g].first), Type::getInt8PtrTy(M.getContext())));

		// 2nd argument is source pointer (cast to i8*)
		args_v.push_back(ConstantExpr::getBitCast(cast<Constant>(g), Type::getInt8PtrTy(M.getContext())));

		// 3rd argument is size of array in bytes
		args_v.push_back(ConstantInt::get(Type::getInt64Ty(M.getContext()), getArrayTypeSize(M, arrayType)));

		// 4th argument is alignment (bitwidth of array element)
		args_v.push_back(ConstantInt::get(fun->getFunctionType()->getParamType(3), getArrayTypeElementBitWidth(M, arrayType)));

		// 5th argument is volatile (boolean)
		args_v.push_back(ConstantInt::getNullValue(fun->getFunctionType()->getParamType(4)));
		ArrayRef<Value*> args = ArrayRef<Value*>(args_v);


		Builder.CreateCall(fun, args);

		if (TMR) {
			args_v[0] = ConstantExpr::getBitCast(cast<Constant>(cloneMap[g].second), Type::getInt8PtrTy(M.getContext()));
			args = ArrayRef<Value*>(args_v);
			Builder.CreateCall(fun, args);
		}

	}
}


//----------------------------------------------------------------------------//
// Cloning debug information
//----------------------------------------------------------------------------//
/*
 * The debug information automatically generated for the new function is nearly identical to the old,
 *  except that it correctly changes the retainedNodes entry to match the local variables in the body
 *  of the new function.
 * This function changes the name of the debug metadata subprogram, and also changes the signature
 *  to match the changes made in cloneFunctionArguments().
 */
void dataflowProtection::cloneMetadata(Module& M, Function* Fnew) {
	DISubprogram* autoSp = Fnew->getSubprogram();
//	if there is no debug information there already, then we don't need to fix it
	if (!autoSp)
		return;

	LLVMContext & C = M.getContext();
	DICompileUnit* dcomp = autoSp->getUnit();
	DIBuilder* DB = new DIBuilder(M, true, dcomp);

	/*
	 * Operands of the DISubprogram, by index
	 * 0: scope
	 * 1: file
	 * 2: name
	 * 3: ?
	 * 4: type
	 * 5: compile unit
	 * 6: ?
	 * 7: retainedNodes
	 */

#if 0
	/* Print out all of the operands in the DISubprogram
 	for (int i = 0; i < N->getNumOperands(); i+=1) {
		const MDOperand& mop = N->getOperand(i);
		if (mop) {
			MDNode* op = dyn_cast<MDNode>(mop);
			if (op)
				errs() << i << ": " << *op << '\n';
			else
				errs() << i << ": " << *mop << '\n';
		} else {
			errs() << i << ": " << mop << '\n';
		}
	} */
#endif

	// have to make new types, based on signature of new function
	DISubroutineType* dtype = autoSp->getType();
//	errs() << dtype << "\n";
	DITypeRefArray dtypeArray = dtype->getTypeArray();
	std::vector<Metadata*> typs;

	// TODO: make this more robust, in case only some arguments were cloned
	for (unsigned i = 0; i < dtypeArray.size(); i+=1) {
		DITypeRef t = dtypeArray[i];
		auto tr = t.resolve();
		typs.push_back(tr);

		if (tr) {
//			errs() << *t << "\n";
			typs.push_back(tr);
			if (TMR)
				typs.push_back(tr);
		}
		else {
//			errs() << t << "\n";
		}
	}

	ArrayRef<Metadata*> typArray(typs);
	DITypeRefArray pTypes = DB->getOrCreateTypeArray(typArray);

	DISubroutineType* dtypeNew = DB->createSubroutineType(
			pTypes,				/* DITypeRefArray, ParameterTypes - return type at 0th index */
			dtype->getFlags(),	/* Flags */
			dtype->getCC()		/* Can also specify calling convention */
	);
//	errs() << *dtypeNew << "\n";

	autoSp->replaceOperandWith(2, dyn_cast<Metadata>(MDString::get(C, Fnew->getName())));
	autoSp->replaceOperandWith(4, dyn_cast<Metadata>(dtypeNew));
	Fnew->setSubprogram(autoSp);
//	errs() << *autoSp << "\n";

	return;
}
