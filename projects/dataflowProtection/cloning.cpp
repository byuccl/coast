//This file contains the functions necessary for the cloning logic in dataflowProtection

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

//Arrays from function pointer are partially developed
#define NO_FN_PTR_ARRAY

//Command line option
extern std::list<std::string> clGlobalsToRuntimeInit;
extern std::list<std::string> ignoreGlbl;
extern cl::opt<bool> noMemReplicationFlag;
extern std::list<std::string> skipLibCalls;
extern std::list<std::string> coarseGrainedUserFunctions;
extern cl::opt<bool> verboseFlag;
extern std::list<std::string> unsupportedFunctions;

using namespace llvm;

//----------------------------------------------------------------------------//
// Initialization
//----------------------------------------------------------------------------//
void dataflowProtection::populateValuesToClone(Module& M) {
	//why was this here? Makes it impossible to clone local variables with in-code directives
	//Because some pointers become stale. Therefore, second set of Instructions that is not volatile
	// contains the instructions marked as such by the annotations.
	instsToClone.clear();
	instsToClone.insert(instsToCloneAnno.begin(), instsToCloneAnno.end());
	// globalsToClone.clear();
	constantExprToClone.clear();

	for (auto F : fnsToClone) {
		if (std::find(coarseGrainedUserFunctions.begin(), coarseGrainedUserFunctions.end(),
				F->getName()) != coarseGrainedUserFunctions.end()) {
//			errs() << F->getName() << " is coarse grained. Not replicating.\n";
			continue;
		}

		for (auto & bb : *F) {
			for (auto & I : bb) {

				if (willBeSkipped(&I)) {
//					errs() << "Not cloning instruction " << I << "\n";
					continue;
				}

				//If store instructions not cloned, skip them
				if (noMemReplicationFlag) {
					if (dyn_cast<StoreInst>(&I)) {
						continue;
					}
				}

				if (CallInst * ci = dyn_cast<CallInst>(&I)) {

					//Don't touch/clone inline assembly
					if (ci->isInlineAsm())
						continue;

					//Clone constants in the function call
					for (unsigned int i = 0; i < ci->getNumArgOperands(); i++) {
						Value * arg = ci->getArgOperand(i);
						if (ConstantExpr * e = dyn_cast<ConstantExpr>(arg)) {
							constantExprToClone.insert(e);
						}
					}

					// skip bitcasts and print a warning message, because this might skip more than bitcasts
					if (!isIndirectFunctionCall(ci, "populateValuesToClone", false)) {
						Function* cF = ci->getCalledFunction();
						if (std::find(skipLibCalls.begin(), skipLibCalls.end(),
								cF->getName()) != skipLibCalls.end()) {
//							errs() << "Skipping the libcall " << cF->getName() << "\n";
							continue;
						}

						//Only replicate coarseGrained user functions
						if ( !(cF->hasExternalLinkage() && cF->isDeclaration()) ) {
							if (std::find(coarseGrainedUserFunctions.begin(), coarseGrainedUserFunctions.end(),
									cF->getName()) == coarseGrainedUserFunctions.end()) {
//								errs() << cF->getName() << " is coarse-grained user function\n";
								continue;
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
//						Value* calledValue = ci->getOperand(0);

						if (auto* cexpr = dyn_cast<ConstantExpr>(calledValue)) {

							//then see if we've got a name for a function in there
							if (Function* indirectF = dyn_cast<Function>(calledValue->stripPointerCasts())) {
								StringRef indirectName = indirectF->getName();
//								errs() << "The name of the indirect function called is " << indirectName << "\n";

								//perform the same checks as above for the function name
								if (std::find(skipLibCalls.begin(), skipLibCalls.end(),
										indirectF->getName()) != skipLibCalls.end()) {
									continue;
								}
								if ( !(indirectF->hasExternalLinkage() && indirectF->isDeclaration()) ) {
									if (std::find(coarseGrainedUserFunctions.begin(), coarseGrainedUserFunctions.end(),
											indirectName) == coarseGrainedUserFunctions.end()) {
										continue;
									}
								}
							}

							//see if we've got a bitcast
							if (cexpr->isCast()) {
								errs() << "We have found a bitcast:\n";
								errs() << "\t" << *calledValue << "\n";
							}

						} else {		//if not, print some kind of warning message
							if (verboseFlag) {
								errs() << warn_string << " unidentified indirect function call is being added to the clone list:\n";
								errs() << *calledValue << "\n";
							}
						}

					}

				}

				//We don't clone terminators
				//Invoke is "designed to operate as a standard call instruction in most regards" - don't clone
				if (I.isTerminator() || isa<InvokeInst>(I)) {
					//we do need to clone the invokes if the function they call is marked as coarse-grained
					if (InvokeInst* invInst = dyn_cast<InvokeInst>(&I)) {
						if (std::find(coarseGrainedUserFunctions.begin(), coarseGrainedUserFunctions.end(),
								invInst->getCalledFunction()->getName()) != coarseGrainedUserFunctions.end()) {
							;	//add it to the list
						} else {
							continue;
						}
					} else {
						continue;
					}
				}

				//Don't clone stores to external globals - assumed to be devices
				if (StoreInst* SI = dyn_cast<StoreInst>(&I)) {
					if (GlobalVariable* GV = dyn_cast<GlobalVariable>(SI->getPointerOperand())) {
						assert(GV && "GV?");
						if (GV->hasExternalLinkage() && !(GV->hasInitializer())) {
							continue;
						}
					}
				}

				//don't clone landingpad instructions; there can only be one at the head of a basic block
				if (isa<LandingPadInst>(&I)) {
					continue;
				}

//				if (instsToClone.empty())
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

		//Don't clone ISR function pointers
		//TODO: not a good way of checking for ISRs
		if (globalName.startswith("__vector") || globalName.startswith("isr_")) {
//			errs() << "WARNING: not duplicating global value " << g.getName() << ", assuming it is llvm-created\n";
			continue;
		}

		//Externally available globals without initializer -> external global
		if (g.hasExternalLinkage() && !g.hasInitializer())
			continue;

		if (globalsToSkip.find(&g) != globalsToSkip.end()) {
//			errs() << "WARNING: not duplicating global variable " << g.getName() << "\n";
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

	//Populate a set with all user-defined functions
	std::set<Function*> fnList;
	for (auto & fn_it : M) {
		//check for unsupported functions
		if (std::find(unsupportedFunctions.begin(), unsupportedFunctions.end(),
				fn_it.getName()) != unsupportedFunctions.end()) {
			errs() << "ERROR: \n    " << fn_it.getName() << ": function is not supported!\n\n\n";
			// definitely will quit
			std::exit(-1);
			assert(false && "Function is not supported!");
		}

		//Ignore library calls
		if (fn_it.isDeclaration()) {
			continue;
		}

		//Don't erase ISRs
		if (isISR(fn_it)) {
			continue;
		}

		// skip user marked coarse-grained functions
		if (std::find(coarseGrainedUserFunctions.begin(), coarseGrainedUserFunctions.end(),
				fn_it.getName())!=coarseGrainedUserFunctions.end()) {
			continue;
		}

		fnList.insert(&fn_it);
	}


	//Get a list of all the functions that should not be modified because they
	//are related to fnToSkip
	bool fnsAdded = true;
	while (fnsAdded) {
		fnsAdded = false;
		for (auto & F : fnsToSkip) {
			for (auto & bb : *F) {
				for (auto & I : bb) {
					if (CallInst* CI = dyn_cast<CallInst>(&I)) {
						if (CI->isInlineAsm()) {
							continue;
						}
						//Skip any thing that doesn't have a called function, print warnings
						if (isIndirectFunctionCall(CI, "populateFnWorklist"))
							continue;
						if (CI->getCalledFunction()->isDeclaration()) {
							continue;
						} else if(fnsToSkip.find(CI->getCalledFunction())==fnsToSkip.end()) {
							fnsToSkip.insert(CI->getCalledFunction());
							fnsAdded = true;
						}
					}
				}
			}
		}
	}

	//Iterate through the fnsToErase list and remove them from the main function list
	for (auto & e : fnsToSkip) {
		fnList.erase(e);
	}

	//Get a list of all the functions that should be modified
	//Start with main, and look at subfunctions
	fnsAdded = true;
	Function* mainF = M.getFunction("main");

	if (xMR_default) {
		if (!mainF) { //If we don't have main, insert all
			fnsToClone = fnList;
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
								if (isIndirectFunctionCall(CI, "populateFnWorklist"))
									continue;
								if (CI->getCalledFunction()->isDeclaration())
									continue;
								else if (std::find(fnsToSkip.begin(), fnsToSkip.end(),
										CI->getCalledFunction()) != fnsToSkip.end())
									continue;
								else if (fnsToClone.find(CI->getCalledFunction())==fnsToClone.end()) {
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

	//Get a list of all functions that are meant to be both cloned and skipped
	for (auto & skip_it: fnsToSkip) {
		if (fnsToClone.find(skip_it)!=fnsToClone.end())
			fnsToCloneAndSkip.insert(skip_it);
	}

	//Make sure coarse grained functions aren't modified
	for (auto it : fnsToClone) {
		if (std::find(coarseGrainedUserFunctions.begin(), coarseGrainedUserFunctions.end(),
				it->getName())!=coarseGrainedUserFunctions.end()) {
			fnsToClone.erase(it);
		}
	}

}

void dataflowProtection::cloneFunctionArguments(Module & M) {
	std::vector<Function*> functionsToFix;
	int warnedFnPtrs = 0;

	//If we aren't replicating everything by default then don't update fn sig
	//There won't be any clones to pass into it
	// if(!xMR_default){
	// 	return;
	// }

	//a list of aggregates which are users of functions
	// these will be used later to skip users of functions that are not CallInsts
	std::list<ConstantAggregate*> skipAggList;

	for (auto g_it = M.global_begin(); g_it != M.global_end(); g_it++) {
		//we're looking for a particular global that causes problems
		if (g_it->getName() == "llvm.global_ctors") {
			//all the operands of this global
			for (auto op = g_it->op_begin(); op != g_it->op_end(); op++) {
//				errs() << *(*op) << "\n";
				//see if it's a ConstantArray
				if (auto cnst = dyn_cast<ConstantArray>(*op)) {
					//look at all of its operands
					for (auto op2 = cnst->op_begin(); op2 != cnst->op_end(); op2++) {
//						errs() << *(*op2) << "\n";
						//see if the operand of the array is an aggregate type
						if (auto agg = dyn_cast<ConstantAggregate>(*op2)) {
							//look at all the operands of the aggregate
							for (auto op3 = agg->op_begin(); op3 != agg->op_end(); op3++) {
								//if one of these operands is a function, then keep track of the aggregate
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

		if (isISR(*F)) {
			continue;
		}

		if (std::find(fnsToSkip.begin(),fnsToSkip.end(),F) != fnsToSkip.end()) {
			continue;
		}

		if (verboseFlag) {
			errs() << "Adding clone arguments to function: " << F->getName() << "\n";
		}

		std::vector<bool> cloneArg(numArgs, false);

		// See if what is passed in has a clone
		for (auto u : F->users()) {

			//Ignore global annotations - globals containing bitcasts
			if (auto ce = dyn_cast<ConstantExpr>(u)) {
				if (ce->isCast()) {
//					errs() << "WARNING: In cloneFnArgs in cloning.cpp\n";
//					errs() << "    " << *u << " is a user/cast of fn " << F->getName() << ", skipping it\n";
					continue;
				}
			}

			//see if it's used in an aggregate type constructor global variable
			if (std::find(skipAggList.begin(), skipAggList.end(), u) != skipAggList.end()) {
//				errs() << info_string << " Skipping " << *u << "\n";
				continue;
			}

			//check for aliases and skip them
			if (isa<GlobalAlias>(u)) {
				if (verboseFlag) {
					errs() << info_string << " Skipping global alias in cloneFunctionArguments()\n";
				}
				continue;
			}

			//check for invoke instructions
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

			//Handle arrays of function pointers by marking what should be modified
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

			// It's possible that the function user is not actually a call to the function, but a call
			//  to some other function that passes this one as a parameter.
			Function* CF = callInst->getCalledFunction();
			if (CF != F) {
				continue;
			}

			for (unsigned int i = 0; i < callInst->getNumArgOperands(); i++) {
				if (willBeCloned(callInst->getArgOperand(i))) {
					cloneArg[i] = true;
				}
			}
		}
		warnedFnPtrs = 0;

		// TODO: some of the arguments could be specifically requested to NOT clone them

		// Check if any parameters need clones
		bool needClones = false;
		for (auto b : cloneArg) {
			needClones |= b;
		}

		if (!needClones) {
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

		FunctionType * Ftype = FunctionType::get(
				F->getFunctionType()->getReturnType(), paramArray, false);

		std::string Fname;
		if (!TMR)
			Fname= F->getName().str() + "_DWC";
		else
			Fname= F->getName().str() + "_TMR";
		Constant * c = M.getOrInsertFunction(Fname, Ftype);
		Function * Fnew = dyn_cast<Function>(c);
		assert(Fnew && "New function is non-void");
		newFunctions.push_back(Fnew);

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

		SmallVector<ReturnInst*, 8> returns;
		CloneFunctionInto(Fnew, F, paramMap, true, returns);
		origFunctions.push_back(F);
		fnsToClone.insert(Fnew);
		fnsToClone.erase(F);
//		errs() << "\nReplacing " << F->getName() << " with " << Fnew->getName() << "\n";

		functionMap[F] = Fnew;

		//This is needed because we clone functions into new functions while updating references
		//Occasionally, functions had been cloned but instsToClone hadn't been updated,
		// so nothing in the new function was listed in instsToClone
		//This led to the pass refusing to replace the cloned arguments in calls when
		// the call lived in a new function, because none of the insts in it were in instsToClone
		populateValuesToClone(M);

//		errs() << "Function: " << F->getName() << "\n";
		// Replace all function calls
		for (auto u : F->users()) {
			//Check for bitcasts in case of annotations
			if (auto ce = dyn_cast<ConstantExpr>(u)) {
				if (ce->isCast()) {
					continue;
				}
			}

//			errs() << "original function call: " << *u << "\n";

			std::vector<Value*> args;

			//Account for arrays of fn pointers
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
				if (fnsToClone.find(parentFn) == fnsToClone.end()) {
					continue;
				}

				// if the use of the function is actually a function pointer *in* the call,
				//  then need to skip doing anything to this one
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

				ArrayRef<Value*>* callArgs;
				callArgs = new ArrayRef<Value*>(args);
				CallInst* newCallInst;

				//turns out that void returning function calls have no name, so have to be careful here
				if (Fnew->getReturnType() == Type::getVoidTy(M.getContext())) {
					newCallInst = CallInst::Create((Value*) Fnew, *callArgs);
					newCallInst->insertBefore(callInst);
				} else {
					//The casting here is to stop from complaining that the Create call doesn't have the right types
					newCallInst = CallInst::Create((Value*) Fnew, *callArgs,
							Twine(callInst->getName()), (Instruction*) callInst);
				}

				//Deal with function calls inside function args when casted - not recognized as callInsts
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

				//if there's debug information for the call, preserve it
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
				if (fnsToClone.find(parentFn)==fnsToClone.end()) {
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

				//The casting here is to stop from complaining that the Create call doesn't have the right types
				InvokeInst* newInvInst = InvokeInst::Create((Value*) Fnew, invInst->getNormalDest(),
						invInst->getUnwindDest(), *callArgs, Twine(invInst->getName()), (Instruction*) invInst);

				//Deal with function calls inside function args when casted - not recognized as callInsts
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

				// Replace all uses of the original call instruction with the new one
				invInst->replaceAllUsesWith(newInvInst);
				invInst->eraseFromParent();
			} else {
				assert(false && "wrong type!\n");
//				TODO: what would cause this to fail?
			}

		}

		//update the metadata describing the new function
		cloneMetadata(M, Fnew);

	}

	#ifndef NO_FN_PTR_ARRAY
	//Update any arrays of function pointers
	//They are stored as global arrays
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
	 */
	 #endif

}

//#define DBG_UPDATE_CALLS
void dataflowProtection::updateCallInsns(Module & M) {

	for (auto &F : M) {
		//If we are skipping the function, don't update the call instructions
		if (fnsToCloneAndSkip.find(&F)!=fnsToCloneAndSkip.end()) {
			if (fnsToClone.find(&F)==fnsToClone.end()) {
				continue;
			}
		}

		for (auto & bb : F) {
			for (auto & I : bb) {
				if (CallInst * CI = dyn_cast<CallInst>(&I)) {
					Function * Fcalled = CI->getCalledFunction();

					if (argNumsCloned.find(Fcalled) != argNumsCloned.end()) {
						auto argsCloned = argNumsCloned[Fcalled];

						for (auto argNum : argsCloned) {
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
				}
			}
		}
	}
	errs() << "\n";
}

void dataflowProtection::updateInvokeInsns(Module & M) {

	for (auto &F : M) {
		//If we are skipping the function, don't update the call instructions
		if (fnsToCloneAndSkip.find(&F)!=fnsToCloneAndSkip.end()) {
			if (fnsToClone.find(&F)==fnsToClone.end()) {
				continue;
			}
		}

		for (auto & bb : F) {
			for (auto & I : bb) {
				//also need to update Invoke instructions
				if (InvokeInst* invInst = dyn_cast<InvokeInst>(&I)) {
					Function* Fcalled = invInst->getCalledFunction();

					//clone the arguments
					if (argNumsCloned.find(Fcalled) != argNumsCloned.end()) {
						auto argsCloned = argNumsCloned[Fcalled];

						for (auto argNum : argsCloned) {
							Value* op = invInst->getArgOperand(argNum);
							if (isCloned(op)) {
								Value* clone1 = getClone(op).first;
								invInst->setArgOperand(argNum + 1, clone1);

								Value* clone2;
								if(TMR){
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

//#define DEBUGGING_MEMSET
//----------------------------------------------------------------------------//
// Fine-grained cloning of instructions
//----------------------------------------------------------------------------//
bool dataflowProtection::cloneInsns() {
	std::deque<Instruction*> cloneList;
	std::vector<std::pair<Instruction*,Instruction*>> instsCloned;

	//Populate the clone list
	for (auto I : instsToClone) {
		Instruction* newI1;
		Instruction* newI2;
		if (InvokeInst* invInst = dyn_cast<InvokeInst>(I) ) {
			if (invInst->getCalledFunction()->getReturnType()->isVoidTy()) {
				continue;
			}
			Function* Fparent = invInst->getParent()->getParent();

			//we need to create a new basic block to branch to on success
			BasicBlock* beforeBlock = invInst->getParent();
			BasicBlock* afterBlock = invInst->getNormalDest();
			BasicBlock* landingBlock = invInst->getUnwindDest();

			const Twine& blockName1 = Fparent->getName() + ".invoke.DWC";
			BasicBlock* newBlock1 = BasicBlock::Create(Fparent->getContext(), \
					blockName1, Fparent, afterBlock);
			afterBlock = invInst->getNormalDest();

			//set original invoke to have new normal destination
			invInst->setNormalDest(newBlock1);

			//make a dummy instruction so we have somewhere to put the invoke
			ConstantInt* nothing = ConstantInt::get(IntegerType::getInt16Ty(Fparent->getContext()), 1, false);
			BinaryOperator* dummy1 = BinaryOperator::CreateNeg(nothing, "dummy1", newBlock1);

			//that contains a copy of the same invoke instruction
			InvokeInst* newInv1 = dyn_cast<InvokeInst>(invInst->clone());
			InvokeInst* newInv2;
			newInv1->setName(invInst->getName() + ".DWC");
			newInv1->insertAfter(dummy1);
			dummy1->eraseFromParent();

			//the new one will have the same unwind location
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
			//for the map
			newI1 = dyn_cast<Instruction>(newInv1);

		} else {	//everything else besides InvokeInst
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

		instsCloned.push_back(std::pair<Instruction*,Instruction*>(newI1, newI2));
		cloneMap[I] = ValuePair(newI1, newI2);
	}

	//Iterate over the clone list and change references
	for (auto clone : instsCloned) {
		//Iterate over the operands in the instruction

		for (unsigned i = 0; i < clone.first->getNumOperands(); i++) {
			//If the operand is found in the map change the reference
			Value* op = clone.first->getOperand(i);

			//skip changing basic block references on the invoke instructions,
			// we already set them up correctly above
			if (isa<InvokeInst>(clone.first) && isa<BasicBlock>(op)) {
				continue;
			}

			if (cloneMap.find(op) != cloneMap.end()) { 	//If we found it
				if (noMemReplicationFlag) { 			//Not replicating memory
					//If we aren't replicating memory then we should not change the load inst. address
					if (dyn_cast<LoadInst>(clone.first)) { //Don't change load instructions
						assert(clone.first && "Clone exists when updating operand");
						clone.first->setOperand(i, op);
						if (TMR) {
							assert(clone.second && "Clone exists when updating operand");
							clone.second->setOperand(i, op);
						}
					} else { //Else update as normal
						clone.first->setOperand(i, cloneMap[op].first);
						if (TMR) {
							clone.second->setOperand(i, cloneMap[op].second);
						}
					}
				} else { //Replicating memory
					clone.first->setOperand(i, cloneMap[op].first);
					if (TMR) {
						clone.second->setOperand(i, cloneMap[op].second);
					}
				}
			} else if (ConstantExpr* ce = dyn_cast<ConstantExpr>(op)) {
				//Don't need to update references to constant ints
				assert(ce && "Null ConstantExpr ce");
				if (isa<ConstantInt>(ce->getOperand(0))) {
					continue;
				}

				if (!willBeCloned(ce->getOperand(0))) {
					continue;
				}

				//Don't mess with loads with inline GEPs
				if (noMemReplicationFlag) {
					if (ce->isGEPWithNoNotionalOverIndexing()) {
						continue;
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
				 */
				//might be an inline reference to a global variable. example:
				//%0 = load <4 x i32>, <4 x i32>* bitcast ([2 x [8 x i32]]* @matrix to <4 x i32>*), align 16, !tbaa !2
				//in the following code segment, the leading underscores in names represent levels of indirection
				if (ce->isCast()) {
					if (noMemReplicationFlag)
						continue;

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
						continue;
					}
					//could be something ugly like:
					//%2 = load <4 x i32>, <4 x i32>* bitcast (i32* getelementptr inbounds ([2 x [8 x i32]], [2 x [8 x i32]]* @matrix, i64 0, i64 0, i64 4) to <4 x i32>*), align 16, !tbaa !2
					ConstantExpr* innerGEPclone1 = dyn_cast<ConstantExpr>(_op);
					if (innerGEPclone1 && innerGEPclone1->isGEPWithNoNotionalOverIndexing()) {

						//get the place to update
						ConstantExpr* innerGEPclone1 = dyn_cast<ConstantExpr>(ce->getOperand(0));
//						errs() << " - " << *innerGEPclone1 << "\n";

						//this next thing is what has the clone(s)
						Value* GEPvalOrig = innerGEPclone1->getOperand(0);
//						errs() << " - " << *GEPvalOrig << "\n";
						//get the clone
						Value* GEPvalClone1 = cloneMap[GEPvalOrig].first;
						assert(GEPvalClone1 && "valid clone");

						//replace uses
						Constant* newGEPclone1 = innerGEPclone1->getWithOperandReplaced(
								0, dyn_cast<Constant>(GEPvalClone1));
						Constant* newCE = ConstantExpr::getCast(
								ce->getOpcode(), newGEPclone1, ce->getType());
						clone.first->setOperand(i, newCE);
//						errs() << " - " << *ce << "\n";
//						errs() << " - " << *clone.first << "\n";

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
					}
					//otherwise, throw an error
					else if (verboseFlag) {
						errs() << warn_string << " In cloneInsns() skipping processing cloned ConstantExpr:\n";
						errs() << " " << *ce << "\n";
					}
					continue;
				}

				if (!willBeCloned(ce->getOperand(0))) {
					continue;
				}

				// error checking here for things missing in the cloneMap
				//  if this is NULL, then that means we just inserted the operand
				//	into the map, and therefore it wasn't in there before
				// trying to dereference 0 is a bad idea
				// how did this get in the list, but not in the map?
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
			} else {
				clone.first->setOperand(i, op);
				if (TMR) {
					assert(clone.second && "Clone exists to set operand");
					clone.second->setOperand(i, op);
				}
			}
		}
	}

	return (instsToClone.size() > 0);
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
			if(TMR){
				Constant* constantOp2 = dyn_cast<Constant>(clones.second);
				assert(constantOp2);
				Constant* c2 = e->getWithOperandReplaced(0, constantOp2);
				e2 = dyn_cast<ConstantExpr>(c2);
				assert(e2);
			}

			//assert(eNew->isGEPWithNoNotionalOverIndexing());
			cloneMap[e] = ValuePair(e1,e2);
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
		//Skip specified globals
		if (std::find(ignoreGlbl.begin(), ignoreGlbl.end(), g->getName().str()) != ignoreGlbl.end()) {
			if (verboseFlag) errs() << "Not replicating " << g->getName() << "\n";
			continue;
		}

		GlobalVariable* gNew = copyGlobal(M, g, "_DWC");

		GlobalVariable* gNew2;
		if (TMR) {
			gNew2 = copyGlobal(M, g, "_TMR");
		}

		cloneMap[g] = ValuePair(gNew,gNew2);
	}

}

GlobalVariable * dataflowProtection::copyGlobal(Module & M, GlobalVariable * g, std::string suffix) {

	Constant * initializer;

	if (globalsToRuntimeInit.find(g) == globalsToRuntimeInit.end()) {
		initializer = g->getInitializer();
	} else {
		Type * initType = g->getInitializer()->getType();

		// for now we only support runtime initialization of arrays
		assert(isa<ArrayType>(initType));

		initializer = ConstantAggregateZero::get(initType);

		if(verboseFlag)	errs() << "Using zero initializer for global " << g->getName() + suffix << "\n";

	}

	GlobalVariable* gNew = new GlobalVariable(M, g->getValueType(), g->isConstant(), g->getLinkage(),
			initializer, g->getName() + suffix, g);
	gNew->setUnnamedAddr(g->getUnnamedAddr());

	if (verboseFlag)
		errs() << "New duplicate global: " << gNew->getName() << "\n";

	return gNew;
}

// For all globals that need to be initialized at runtime, insert memcpy calls
// at the start of main
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

	//have to make new types, based on signature of new function
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
