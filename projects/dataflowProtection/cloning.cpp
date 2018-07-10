//This file contains the functions necessary for the cloning logic in dataflowProtection

#include "dataflowProtection.h"

#include <algorithm>
#include <deque>
#include <vector>
#include <string>

#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>

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
	instsToClone.clear();
	globalsToClone.clear();
	constantExprToClone.clear();

	for(auto F : fnsToClone) {
		if(std::find(coarseGrainedUserFunctions.begin(), coarseGrainedUserFunctions.end(), F->getName()) != coarseGrainedUserFunctions.end()){
//			errs() << F->getName() << " is coarse grained. Not replicating.\n";
			continue;
		}

		for (auto & bb : *F) {
			for (auto & I : bb) {

				if(willBeSkipped(&I)){
//					errs() << "Not cloning instruction " << I << "\n";
					continue;
				}

				//If store instructions not cloned, skip them
				if(noMemReplicationFlag){
					if(dyn_cast<StoreInst>(&I)){
						continue;
					}
				}

				if (CallInst * ci = dyn_cast<CallInst>(&I)) {

					//Don't touch/clone inline assembly
					if(ci->isInlineAsm())
						continue;

					//Clone constants in the function call
					for (unsigned int i = 0; i < ci->getNumArgOperands(); i++) {
						Value * arg = ci->getArgOperand(i);
						if (ConstantExpr * e = dyn_cast<ConstantExpr>(arg)) {
							constantExprToClone.insert(e);
						}
					}

					// skip bitcasts and print a warning message, because this might skip more than bitcasts
					if(!isIndirectFunctionCall(ci, "populateValuesToClone",false)){
						if(std::find(skipLibCalls.begin(), skipLibCalls.end(), ci->getCalledFunction()->getName()) != skipLibCalls.end()){
	//						errs() << "Skipping the libcall " << ci->getCalledFunction()->getName() << "\n";
							continue;
						}

						//Only replicate coarseGrained user functions
						if(!(ci->getCalledFunction()->hasExternalLinkage() &&
								ci->getCalledFunction()->isDeclaration())){
							if(std::find(coarseGrainedUserFunctions.begin(), coarseGrainedUserFunctions.end(), ci->getCalledFunction()->getName()) == coarseGrainedUserFunctions.end()){
	//							errs() << ci->getCalledFunction()->getName() << " is coarse-grained user function\n";
								continue;
							}
						}
					}
				}

				//We don't clone terminators
				//Invoke is "designed to operate as a standard call instruction in most regards" - don't clone
				if (I.isTerminator() || isa<InvokeInst>(I))
					continue;

				//Don't clone stores to external globals - assumed to be devices
				if (StoreInst* SI = dyn_cast<StoreInst>(&I)) {
					if (GlobalVariable* GV = dyn_cast<GlobalVariable>(SI->getPointerOperand())) {
						assert(GV && "GV?");
						if (GV->hasExternalLinkage() && !(GV->hasInitializer())) {
							continue;
						}
					}
				}

				instsToClone.insert(&I);
			}
		}
	}

	for (GlobalVariable & g : M.getGlobalList()) {
		StringRef globalName = g.getName();

		if(globalName.startswith("llvm")){
//			errs() << "WARNING: not duplicating global value " << g.getName() << ", assuming it is llvm-created\n";
			continue;
		}

		//Don't clone ISR function pointers
		if(globalName.startswith("__vector") || globalName.startswith("isr_")){
//			errs() << "WARNING: not duplicating global value " << g.getName() << ", assuming it is llvm-created\n";
			continue;
		}

		//Externally available globals without initializer -> external global
		if (g.hasExternalLinkage() && !g.hasInitializer())
			continue;

		if(globalsToSkip.find(&g) != globalsToSkip.end()){
//			errs() << "WARNING: not duplicating global variable " << g.getName() << "\n";
			continue;
		}

		globalsToClone.insert(&g);
	}

}

//----------------------------------------------------------------------------//
// Modify functions
//----------------------------------------------------------------------------//
void dataflowProtection::populateFnWorklist(Module& M){
	//Populate a set with all user-defined functions
	std::set<Function*> fnList;
	for (auto & fn_it : M){
		//check for unsupported functions
		if(std::find(unsupportedFunctions.begin(), unsupportedFunctions.end(), fn_it.getName()) != unsupportedFunctions.end()){
			errs() << "ERROR: \n    " << fn_it.getName() << ": function is not supported!\n\n\n";
			std::exit(-1);
			assert(false && "Function is not supported!");
		}

		//Ignore library calls
		if (fn_it.isDeclaration()) {
			continue;
		}

		//Don't erase ISRs
		if(isISR(fn_it)){
			continue;
		}

		if(std::find(coarseGrainedUserFunctions.begin(),coarseGrainedUserFunctions.end(),fn_it.getName())!=coarseGrainedUserFunctions.end()){
			continue;
		}
		fnList.insert(&fn_it);
	}


	//Get a list of all the functions that should not be modified because they
	//are related to fnToSkip
	bool fnsAdded = true;
	while(fnsAdded){
		fnsAdded = false;
		for(auto & F : fnsToSkip){
			for(auto & bb : *F){
				for(auto & I : bb){
					if(CallInst* CI = dyn_cast<CallInst>(&I)){
						if(CI->isInlineAsm()){
							continue;
						}
						//Skip any thing that doesn't have a called function, print warnings
						if(isIndirectFunctionCall(CI, "populateFnWorklist"))
							continue;
						if(CI->getCalledFunction()->isDeclaration()){
							continue;
						} else if(fnsToSkip.find(CI->getCalledFunction())==fnsToSkip.end()){
							fnsToSkip.insert(CI->getCalledFunction());
							fnsAdded = true;
						}
					}
				}
			}
		}
	}

	//Iterate through the fnsToErase list and remove them from the main function list
	for(auto & e : fnsToSkip){
		fnList.erase(e);
	}

	//Get a list of all the functions that should be modified
	//Start with main, and look at subfunctions
	fnsAdded = true;
	Function* mainF = M.getFunction("main");

	if(xMR_default){
		if(!mainF) { //If we don't have main, insert all
			fnsToClone = fnList;
		} else{
			fnsToClone.insert(mainF);
			while(fnsAdded){
				fnsAdded = false;
				for(auto & F : fnsToClone){
					for(auto & bb : *F){
						for(auto & I : bb){
							if(CallInst* CI = dyn_cast<CallInst>(&I)){
								if(CI->isInlineAsm())
									continue;
								// skip any thing that doesn't have a called function and print warning
								if(isIndirectFunctionCall(CI, "populateFnWorklist"))
									continue;
								if(CI->getCalledFunction()->isDeclaration())
									continue;
								else if(std::find(fnsToSkip.begin(), fnsToSkip.end(), CI->getCalledFunction()) != fnsToSkip.end())
									continue;
								else if(fnsToClone.find(CI->getCalledFunction())==fnsToClone.end()){
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
	for(auto & skip_it: fnsToSkip){
		if(fnsToClone.find(skip_it)!=fnsToClone.end())
			fnsToCloneAndSkip.insert(skip_it);
	}

	//Make sure coarse grained functions aren't modified
	for(auto it : fnsToClone){
		if(std::find(coarseGrainedUserFunctions.begin(),coarseGrainedUserFunctions.end(),it->getName())!=coarseGrainedUserFunctions.end()){
			fnsToClone.erase(it);
		}
	}

}

void dataflowProtection::cloneFunctionArguments(Module & M) {
	std::vector<Function*> functionsToFix;

	//If we aren't replicating everything by default then don't update fn sig
	//There won't be any clones to pass into it
	if(!xMR_default){
		return;
	}

	for(auto F : fnsToClone) {
		if (!F->isDeclaration()) {
			functionsToFix.push_back(F);
		}
	}

	for (auto F : functionsToFix) {
		unsigned int numArgs = F->getArgumentList().size();

		if(isISR(*F)){
			continue;
		}

		if(std::find(fnsToSkip.begin(),fnsToSkip.end(),F) != fnsToSkip.end()){
			continue;
		}

		std::vector<bool> cloneArg(numArgs, false);

		// See if what is passed in has a clone
		for (auto u : F->users()) {
			//Ignore global annotations - globals containing bitcasts
			if(auto ce = dyn_cast<ConstantExpr>(u)){
				if(ce->isCast()){
//					errs() << "WARNING: In cloneFnArgs in cloning.cpp\n";
//					errs() << "    " << *u << " is a user/cast of fn " << F->getName() << ", skipping it\n";
					continue;
				}
			}

			CallInst * callInst = dyn_cast<CallInst>(u);


			//Handle arrays of function pointers by marking what should be modified
			if(ConstantArray* ca = dyn_cast<ConstantArray>(u)){
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


			if(!callInst){
				errs() << "ERROR: User is not a CallInst!\n\t" << *u << "\n\n";
			}
			assert(callInst && "User is not a call instruction");

			for (unsigned int i = 0; i < callInst->getNumArgOperands(); i++) {
				if (willBeCloned(callInst->getArgOperand(i))){
					cloneArg[i] = true;
				}
			}
		}

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
		for (unsigned int i = 0; i < F->getFunctionType()->params().size();
				i++) {
			params.push_back(F->getFunctionType()->getParamType(i));
			if (cloneArg[i]){
				params.push_back(F->getFunctionType()->getParamType(i));
				if(TMR){
					params.push_back(F->getFunctionType()->getParamType(i));
				}
			}
		}

		ArrayRef<Type*> paramArray(params);

		FunctionType * Ftype = FunctionType::get(
				F->getFunctionType()->getReturnType(), paramArray, false);

		std::string Fname;
		if(!TMR)
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
				if(TMR){
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
			if(auto ce = dyn_cast<ConstantExpr>(u)){
				if(ce->isCast()){
					continue;
				}
			}

//			errs() << "original function call: " << *u << "\n";

			std::vector<Value*> args;

			//Account for arrays of fn pointers
			unsigned int j = 0;
			if(ConstantArray* ca = dyn_cast<ConstantArray>(u)){
				for(int i=0; i<ca->getNumOperands(); i++){
					if(ca->getOperand(i)->getName() == F->getName()){
						int index = -1;
						for(auto arg=F->arg_begin(); arg!=F->arg_end(); arg++){
							index++;
							args.push_back(dyn_cast<Value>(arg));
							if (cloneArg[index]) {
								if (willBeCloned(dyn_cast<Value>(arg))) {
									argNumsCloned[Fnew].push_back(j);
								}
								args.push_back(dyn_cast<Value>(arg));
								j++;
								if(TMR){
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

			} else{
				CallInst* callInst = dyn_cast<CallInst>(u);
				assert(callInst && "Replacing function calls in cloneFnArgs");

				Function* parentFn = callInst->getParent()->getParent();
				if(fnsToClone.find(parentFn)==fnsToClone.end()){
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
						if(TMR){
							args.push_back(argOrig);
							j++;
						}
					}
				}

				ArrayRef<Value*>* callArgs;
				callArgs = new ArrayRef<Value*>(args);

				//The casting here is to stop from complaining that the Create call doesn't have the right types
				CallInst * newCallInst = CallInst::Create((Value*) Fnew, *callArgs,
						Twine(callInst->getName()), (Instruction*) callInst);

				//Deal with function calls inside function args when casted - not recognized as callInsts
				for(auto ops : newCallInst->operand_values()){
					if(auto ce = dyn_cast<ConstantExpr>(ops)){
						if(ce->isCast()){
							assert(ce->getNumOperands() == 1 && "Setting the arg of a cast");
							Function* oldFn = dyn_cast<Function>(ce->getOperand(0));
							if(functionMap[oldFn]){
								ce->setOperand(0,functionMap[oldFn]);
							}
						}
					}
				}

				// Replace all uses of the original call instruction with the new one
				callInst->replaceAllUsesWith(newCallInst);
				callInst->eraseFromParent();
			}
		}
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

void dataflowProtection::updateCallInsns(Module & M) {

	for (auto &F : M) {
		//If we are skipping the function, don't update the call instructions
		if(fnsToCloneAndSkip.find(&F)!=fnsToCloneAndSkip.end()){
			if(fnsToClone.find(&F)==fnsToClone.end()){
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
								if(TMR){
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
}

//----------------------------------------------------------------------------//
// Fine-grained cloning of instructions
//----------------------------------------------------------------------------//
bool dataflowProtection::cloneInsns() {
	std::deque<Instruction*> cloneList;
	std::vector<std::pair<Instruction*,Instruction*>> instsCloned;

	//Populate the clone list
	for (auto I : instsToClone) {
		Instruction* newI1 = I->clone();
		Instruction* newI2;

		if (!I->getType()->isVoidTy()) {
			newI1->setName(I->getName() + ".DWC");
		}

		newI1->insertAfter(I);

		if(TMR){
			newI2 = I->clone();
			if (!I->getType()->isVoidTy()) {
				newI2->setName(I->getName() + ".TMR");
			}

			newI2->insertAfter(newI1);
		}

		instsCloned.push_back(std::pair<Instruction*,Instruction*>(newI1,newI2));
		cloneMap[I] = ValuePair(newI1,newI2);
	}

	//Iterate over the clone list and change references
	for (auto clone : instsCloned) {
		//Iterate over the operands in the instruction
		for (unsigned i = 0; i < clone.first->getNumOperands(); i++) {
			//If the operand is found in the map change the reference
			Value* op = clone.first->getOperand(i);
			if (cloneMap.find(op) != cloneMap.end()){ //If we found it
				if(noMemReplicationFlag){ //Not replicating memory
					//If we aren't replicating memory then we should not change the load inst. address
					if(dyn_cast<LoadInst>(clone.first)){ //Don't change load instructions
						clone.first->setOperand(i, op);
						if(TMR){
							assert(clone.second && "Clone exists when updating operand");
							clone.second->setOperand(i, op);
						}
					} else{ //Else update as normal
						clone.first->setOperand(i, cloneMap[op].first);
						if(TMR){
							clone.second->setOperand(i, cloneMap[op].second);
						}
					}
				} else{ //Replicating memory
					clone.first->setOperand(i, cloneMap[op].first);
					if(TMR){
						clone.second->setOperand(i, cloneMap[op].second);
					}
				}
			} else if(ConstantExpr* ce = dyn_cast<ConstantExpr>(op)){
				//Don't need to update references to constant ints
				if(isa<ConstantInt>(ce->getOperand(0))){
					continue;
				}

				if(!willBeCloned(ce->getOperand(0))){
					continue;
				}

				//Don't mess with loads with inline GEPs
				if(noMemReplicationFlag){
					if(ce->isGEPWithNoNotionalOverIndexing()){
						continue;
					}
				}

				Constant* newOp1 = dyn_cast<Constant>(cloneMap[ce->getOperand(0)].first);
				Constant* c1 = ce->getWithOperandReplaced(0, newOp1);
				ConstantExpr* eNew1 = dyn_cast<ConstantExpr>(c1);
				clone.first->setOperand(i, eNew1);

				if(TMR){
					Constant* newOp2 = dyn_cast<Constant>(cloneMap[ce->getOperand(0)].second);
					Constant* c2 = ce->getWithOperandReplaced(0, newOp2);
					ConstantExpr* eNew2 = dyn_cast<ConstantExpr>(c2);
					clone.second->setOperand(i, eNew2);
				}
			} else{
				clone.first->setOperand(i, op);
				if(TMR){
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
			assert(false && "Constant expr to clone not matching expected form");
		}
	}
}

//----------------------------------------------------------------------------//
// Cloning of globals
//----------------------------------------------------------------------------//
void dataflowProtection::cloneGlobals(Module & M) {

	if(noMemReplicationFlag)
		return;

	// First figure out which globals will be initialized at runtime
	for (auto g : globalsToClone) {
		if (std::find(clGlobalsToRuntimeInit.begin(), clGlobalsToRuntimeInit.end(), g->getName().str()) != clGlobalsToRuntimeInit.end()) {
			globalsToRuntimeInit.insert(g);
		}
	}

	for (auto g : globalsToClone) {
		//Skip specified globals
		if(std::find(ignoreGlbl.begin(), ignoreGlbl.end(), g->getName().str()) != ignoreGlbl.end()){
			if(verboseFlag) errs() << "Not replicating " << g->getName() << "\n";
			continue;
		}

		GlobalVariable* gNew = copyGlobal(M, g, "_DWC");

		GlobalVariable* gNew2;
		if(TMR){
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
