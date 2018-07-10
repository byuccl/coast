//===- exitMarker.cpp - Add in a function call to ID when main returns ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "ExitMarker"

#include <vector>
#include <set>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include "llvm/Support/raw_ostream.h"
#include <llvm/IR/Constants.h>
#include <llvm/Support/Debug.h>

using namespace llvm;

//--------------------------------------------------------------------------//
// Top level behavior
//--------------------------------------------------------------------------//
class ExitMarker : public ModulePass {
public:
	static char ID;             // Pass identification
	ExitMarker() : ModulePass(ID) { }

	bool runOnModule(Module &M);
	void removeUnusedFunctions(Module& M);
	void recursivelyVisitCalls(Module& M, Function* F, std::set<Function*> &functionList);
	std::set<Function*> fnsToClone;
private:

};

char ExitMarker::ID = 0;
static RegisterPass<ExitMarker> X("ExitMarker",
		"Insert a function call whenever main returns. Used by FIJI to detect when the program stops.", false, true);

void ExitMarker::removeUnusedFunctions(Module& M) {
	//Populate a list of all functions in the module
	std::set<Function*> functionList;
	for(auto & F : M){
		//Ignore external function declarations
		if(F.hasExternalLinkage() && F.isDeclaration()){
			continue;
		}

		//Don't erase ISRs
		if(F.getName().endswith("ISR") || F.getName().endswith("isr")){
			continue;
		}

		functionList.insert(&F);
	}

	Function* mainFunction = M.getFunction("main");
	assert(mainFunction && "Got the main function\n");
	recursivelyVisitCalls(M,mainFunction,functionList);

	if(functionList.size() == 0)
		return;

	errs() << "The following functions are unused, removing them: \n";
	for(auto q : functionList){
		errs() << "    " << q->getName() << "\n";
		assert(fnsToClone.find(q)==fnsToClone.end() && "The specified function is not called, so is being removed");
		q->eraseFromParent();
	}

}

void ExitMarker::recursivelyVisitCalls(Module& M, Function* F, std::set<Function*> &functionList){

	//If we've already deleted this function from the list
	if(functionList.find(F)==functionList.end())
		return;

	functionList.erase(F);

	for(auto & bb : *F){
		for(auto & I : bb){
			if(CallInst* CI = dyn_cast<CallInst>(&I)){
				recursivelyVisitCalls(M,CI->getCalledFunction(),functionList);
			}
		}
	}

}

bool ExitMarker::runOnModule(Module &M) {
	Function* mainFn;
	std::vector<ReturnInst*> returnInsts;
	std::vector<Value*> returnValues;

	//Get main, all the basic blocks in main that return
	for(auto &F : M){
		if(F.getName() == "main"){

			mainFn = &F;

			for(auto &bb : F){
				if(ReturnInst* RI = dyn_cast<ReturnInst>(bb.getTerminator())){
					returnInsts.push_back(RI);
					returnValues.push_back(RI->getReturnValue());
				}
			}

		}
	}

	//Assemble the proper function type
	std::vector<Type*> params;
	params.push_back(mainFn->getReturnType());
	ArrayRef<Type*> args(params);
	FunctionType* markerTy = FunctionType::get(mainFn->getReturnType(),args,false);

	//Create a function called EDDI_EXIT
	Constant* c = M.getOrInsertFunction("EXIT_MARKER", markerTy);
	Function* exitMarkerFn = dyn_cast<Function>(c);
	assert(exitMarkerFn && "Exit marker function is non-void");

	//Create a basic block that returns the value passed into it
	BasicBlock* bb = BasicBlock::Create(M.getContext(), Twine("entry"), exitMarkerFn, NULL);

	//Create the terminator that returns the argument passed into the function
	assert(exitMarkerFn->getArgumentList().size() == 1);
	Argument* arg = &*(exitMarkerFn->arg_begin());
	ReturnInst* term = ReturnInst::Create(M.getContext(), arg ,bb);

	//Insert the call instructions before all return instructions
	for(auto &I : returnInsts){
		CallInst* exitMarkerCall = CallInst::Create(exitMarkerFn,I->getReturnValue(), "", I);
	}

	removeUnusedFunctions(M);

	return true;
}
