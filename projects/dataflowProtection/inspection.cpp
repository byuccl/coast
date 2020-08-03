/*
 * inspection.cpp
 *
 * This file has small functions which will give information about short queries.
 */

#include "dataflowProtection.h"

// standard library includes
#include <algorithm>
#include <string>
#include <list>


using namespace llvm;


// command line options
extern cl::opt<bool> verboseFlag;

// shared variables
extern std::list<std::string> coarseGrainedUserFunctions;


//----------------------------------------------------------------------------//
// Cloning utilities
//----------------------------------------------------------------------------//
bool dataflowProtection::willBeSkipped(Instruction* I) {
	return instsToSkip.find(I) != instsToSkip.end();
}


bool dataflowProtection::willBeCloned(Value* v) {
	Instruction* I = dyn_cast<Instruction>(v);
	if (I) {
		return instsToClone.find(I) != instsToClone.end();
	}

	GlobalVariable* g = dyn_cast<GlobalVariable>(v);
	if (g) {
		return globalsToClone.find(g) != globalsToClone.end();
	}

	ConstantExpr* e = dyn_cast<ConstantExpr>(v);
	if (e) {
		return constantExprToClone.find(e) != constantExprToClone.end();
	}

	if (Argument* a = dyn_cast<Argument>(v)) {
		Function * f = a->getParent();
		return fnsToClone.find(f) != fnsToClone.end();
	}

	return false;
}


bool dataflowProtection::isCloned(Value * v) {
	return cloneMap.find(v) != cloneMap.end();
}


ValuePair dataflowProtection::getClone(Value* I) {
	if (cloneMap.find(I) == cloneMap.end()) {
		return ValuePair(I,I);
	} else {
		return cloneMap[I];
	}
}

/*
 * Reverse lookup of getClone.
 * Returns nullptr if the input value isn't a clone of anything.
 */
Value* dataflowProtection::getCloneOrig(Value* v) {
	for (auto cloneIt : cloneMap) {
		Value* orig = cloneIt.first;
		ValuePair clones = cloneIt.second;
		Value* clone1 = clones.first;
		Value* clone2 = clones.second;
		if ( (clone1 == v) || (clone2 == v) ) {
			return orig;
		}
	}
	return nullptr;
}


bool dataflowProtection::isCoarseGrainedFunction(StringRef fnName) {
	if (std::find(coarseGrainedUserFunctions.begin(), coarseGrainedUserFunctions.end(),
					fnName.str()) != coarseGrainedUserFunctions.end())
	{
		return true;
	} else {
		return false;
	}
}


//----------------------------------------------------------------------------//
// Synchronization utilities
//----------------------------------------------------------------------------//
bool dataflowProtection::isSyncPoint(Instruction* I) {
	if (isa<StoreInst>(I) || isa<CallInst>(I) || isa<TerminatorInst>(I) || isa<GetElementPtrInst>(I))
		return std::find(syncPoints.begin(), syncPoints.end(), I) != syncPoints.end();
	else
		return false;
}


bool dataflowProtection::isStoreMovePoint(StoreInst* SI) {
	if ( 	(getClone(SI).first == SI) ||						/* Doesn't have a clone */
			(SI->getOperand(0)->getType()->isPointerTy()) ||	/* Storing a pointer type */
			(dyn_cast<PtrToIntInst>(SI->getOperand(0))) ) 		/* Casted pointer */
	{
		return false;
	}
	// otherwise, we need to segment them together
	else
		return true;
}


bool dataflowProtection::isCallMovePoint(CallInst* ci) {
	if ( (getClone(ci)).first == ci) {
		return false;
	} else {
		return true;
	}
}


/*
 * Returns true if this will try to sync on a coarse-grained function return value
 * These should be avoided for things like the case of malloc()
 * If returns false, then it's OK to sync on the value
 */
bool dataflowProtection::checkCoarseSync(StoreInst* inst) {
	// need to check for if this value came from a replicated function call
	Value* op0 = inst->getOperand(0);
	if (CallInst* CI = dyn_cast<CallInst>(op0)) {
		Function* calledF = CI->getCalledFunction();
		if (calledF && calledF->hasName() &&
				isCoarseGrainedFunction(calledF->getName()))
		{
			// then we've got a coarse-grained value
			return true;
		}
	} else if (InvokeInst* II = dyn_cast<InvokeInst>(op0)) {
		Function* calledF = II->getCalledFunction();
		if (calledF && calledF->hasName() &&
				isCoarseGrainedFunction(calledF->getName()))
		{
			// again
			return true;
		}
	}
	return false;
}


//----------------------------------------------------------------------------//
// Miscellaneous
//----------------------------------------------------------------------------//
bool dataflowProtection::isIndirectFunctionCall(CallInst* CI, std::string errMsg, bool print) {
	// This partially handles bitcasts and other inline LLVM functions
	if (CI->getCalledFunction() == nullptr) {
		// probably don't want to hear about skipping inline assembly, clean up output
		if ( print && (!CI->isInlineAsm()) ) {
			errs() << warn_string << " in " << errMsg << " skipping:\n\t" << *CI << "\n";
		}
		return true;
	} else {
		return false;
	}
}


/*
 * A function will only be treated as an ISR if it's marked as such by the user.
 * This can be done with in-code directives or a command-line option.
 */
bool dataflowProtection::isISR(Function& F) {
	bool ans = isrFunctions.find(&F) != isrFunctions.end();
	return ans;
}
