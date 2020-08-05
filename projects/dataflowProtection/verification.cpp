/*
 * verification.cpp
 *
 * This file contains the functions used by the dataflowProtection pass to verify
 *  that the configuration options are consistent with each other.
 */

#include "dataflowProtection.h"

// standard library includes
#include <map>
#include <set>
#include <list>

// LLVM includes
#include <llvm/IR/Module.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/Intrinsics.h>

using namespace llvm;


// all of the stores to globals that should become sync points
std::set<StoreInst*> syncGlobalStores;
// crossings that are marked to be skipped
std::map<GlobalVariable*, std::set<Function*> > globalCrossMap;

// shared variables
extern std::list<std::string> skipLibCalls;
extern cl::opt<bool> noMemReplicationFlag;
extern cl::opt<bool> verboseFlag;


// maps that describe different invalid use cases
static GlobalFunctionSetMap unPtWritesToPtGlbls;		/* Unprotected writes to protected globals */
static GlobalFunctionSetMap unPtReadsFromPtGlbls;		/* Unprotected reads from protected globals */
static GlobalFunctionSetMap ptWritesToUnPtGlbls;		/* Protected writes to unprotected globals */

static std::list< CallRecordType > ptCallsList;			/* Walk calls from protected functions that use unprotected globals */
static std::list< CallRecordType > unPtCallsList;		/* Walk calls from unprotected functions that use protected globals */

static GlobalFunctionSetMap ptCallsWithUnPtGlbls;		/* Protected function calls with unprotected globals as arguments */
static GlobalFunctionSetMap unPtCallsWithPtGlbls;		/* Unprotected function calls with protected globals as arguments */

static const std::set<Function*>* fnsToClone_ptr;

static bool verifyDebug = false;

/*
 * Helper function that looks for stores that inherit from loads.
 * Essentially verifying if a given memory reference is read-only or not.
 * Returns nullptr if it is SURE it is read-only,
 *  StoreInst that is in the use chain if not.
 * Call instructions will be tracked later, but still return them.
 *
 * Edited to allow looking at Values instead of just Instructions.
 * This lets us track CallInst Arguments.
 */
Instruction* hasStoreUsage(Value* i) {
	if (!i) {
		return nullptr;
	} else if (i->getNumUses() == 0) {
		return nullptr;
	}
	static std::set<PHINode*> seenPhiSet;

	// walk the users
	for (auto use : i->users()) {
		// we only care about the instructions
		if (auto instUse = dyn_cast<Instruction>(use)) {
//			if (verifyDebug) errs() << *instUse << "\n";

			// PHI nodes break the recursion, otherwise infinite loop
			if (PHINode* phiUse = dyn_cast<PHINode>(instUse)) {
				// if we haven't seen it yet, mark it as seen and fall through
				if (seenPhiSet.find(phiUse) == seenPhiSet.end()) {
					seenPhiSet.insert(phiUse);
				} else {
					// skip the one's we've seen already
					continue;
				}
			}

			// if we are loading a scalar value from this, then skip it
			if (LoadInst* loadUse = dyn_cast<LoadInst>(instUse)) {
				Type* loadType = loadUse->getType();
				if (!loadType->isPtrOrPtrVectorTy()) {
					continue;
				}
			}

			// if the user is a compare instruction, we don't have to keep tracking it
			//  because it fundamentally changes the type of data
			if (CmpInst* cmpUse = dyn_cast<CmpInst>(instUse)) {
				continue;
			}

			// if its a store, then we're done
			if (StoreInst* storeUse = dyn_cast<StoreInst>(instUse)) {
				return instUse;
			} else if (CallInst* callUse = dyn_cast<CallInst>(instUse)) {
				return instUse;
			} else {
				return hasStoreUsage(instUse);
			}
		}
	}
	// if there are no more users, then we've checked everything, return true
	return nullptr;
}

/*
 * Helper function that looks to see if a local pointer is used in stores or GEPs.
 * 'ignoreThis' means it's the original store, so we don't want to detect it again.
 * Returns nullptr if it is never used,
 * 	Instruction that is the user otherwise
 */
Instruction* isDereferenced(Instruction* i, Instruction* ignoreThis) {
	// walk the users
	for (auto use: i->users()) {
		if (auto instUse = dyn_cast<Instruction>(use)) {
			if (instUse == ignoreThis) {
				continue;
			}

			// look for stores or GEPs
			if (StoreInst* storeUse = dyn_cast<StoreInst>(instUse)) {
				return instUse;
			} else if (GetElementPtrInst* gepUse = dyn_cast<GetElementPtrInst>(instUse)) {
				return instUse;
			} else if (CallInst* callUse = dyn_cast<CallInst>(instUse)) {
				return instUse;
			} else {
				return isDereferenced(instUse, ignoreThis);
			}
		}
	}
	return nullptr;
}


/*
 * Helper function to see if name matches certain pattern that we think
 *  indicates the global is actually static inside a function.
 *
 * TODO: if the name of the variable is the original function which has been
 *  inlined, then the conditions don't match.  Can we look at debug info?
 */
bool globalIsStaticToFunction(GlobalVariable* gv, Function* parentF, Instruction* spot) {
	if (gv->getName().str().find(parentF->getName().str()) != std::string::npos) {
		return true;
	}
	// is it possible to get the original function name if it has been inlined?
	// DebugLoc dbgLoc = spot->getDebugLoc();

	return false;
}


/*
 * Helper function to check if it's in the cross map.
 * Before this was only a map of GlobalVariable -> Function.  Which meant that
 *  we could only mark one function per GV.  Now it's a set, so that's fixed.
 */
bool shouldSkipGlobalUsage(GlobalVariable* gv, Function* parentF) {
	auto found_iter = globalCrossMap.find(gv);
	// if the global is in the map
	if (found_iter != globalCrossMap.end()) {
		// returns a (key, value) pair
		if ((*found_iter).second.find(parentF) != (*found_iter).second.end()) {
			return true;
		}
	}
	return false;
}


/*
 * Helper function to make it easier to put a new value in one of the maps in the below function
 */
void writeToGlobalMap(GlobalFunctionSetMap &globalMap, GlobalVariable* gv, Function* parentF, Instruction* spot) {

	/* We want to skip "globals" that are actually just static
	 * variables inside functions.  For example, '_sbrk.heap' is a variable
	 * in the function '_sbrk' named 'heap', marked as 'static'.
	 * This should be skipped.
	 * We know this by:
	 * 1) internal linkage
	 * 2) unnamed_addr
	 * 3) name is parentF.varName (might only be true for C)
	 *
	 * Workaround: even though it's not a global, treat it as such and
	 *  add it to the correct list for the desired behavior.
	 */
	if ( (gv->hasInternalLinkage()) &&
		 (gv->hasGlobalUnnamedAddr()) &&
		 (gv->getName().str().find(".") != std::string::npos) &&
		 (globalIsStaticToFunction(gv, parentF, spot)) )
	{
		// errs() << "Found static global(?)\n\t" << *gv << "\n";
		// don't add to list
		return;
	}

	// have we been asked to skip it?
	if (shouldSkipGlobalUsage(gv, parentF)) {
		return;
	}

	if (globalMap.find(gv) == globalMap.end()) {
		FunctionDebugSet tempSet;
		globalMap[gv] = tempSet;
	}
	// debug
	// errs() << "Inserting '" << gv->getName() << "' from '"
	// 	   << parentF->getName() <<"'\n";
	globalMap[gv].insert(std::make_pair(parentF, spot));
}

/*
 * Helper function to walk backwards the instruction uses to find the AllocaInst.
 * If one cannot be found, return nullptr
 */
AllocaInst* findAllocaInst(Instruction* inst) {
	for (int i = 0; i < inst->getNumOperands(); i++) {
		Value* nextVal = inst->getOperand(i);
		if (AllocaInst* ai = dyn_cast<AllocaInst>(nextVal)) {
			return ai;
		} else if (Instruction* nextInst = dyn_cast<Instruction>(nextVal)) {
			return findAllocaInst(nextInst);
		}
	}
	return nullptr;
}

/*
 * Helper function to see if the call instructions calls a function that is marked
 *  to not be called more than once (skipLibCalls)
 */
bool fnToBeSkipped(Function* f) {
	if ((f != nullptr) && (f->hasName())) {
		auto found = std::find(skipLibCalls.begin(), skipLibCalls.end(), f->getName());
		if (found != skipLibCalls.end()) {
			return true;
		}
	}
	return false;
}


bool fnToBeCloned(Function* f) {
	if ((f != nullptr) && (f->hasName())) {
		auto found = fnsToClone_ptr->find(f);
		if (found != fnsToClone_ptr->end()) {
			return true;
		}
	}
	return false;
}

/*
 * Helper function to find the next store (if any) from a load
 *  that isn't storing to a local variable (comes from an AllocaInst).
 * Return value may be nullptr.
 */
Instruction* getNextNonAllocaStore(StoreInst* storeUse) {
	/*
	 * When this function starts, we have the first store instruction that inherits
	 *  from a load of a global.  We need to find out
	 *   1) is this a store to a local variable
	 *   2) when is the next store (if any) that's NOT to a local variable
	 */
	Instruction* storeSpot = dyn_cast<Instruction>(storeUse->getOperand(1));
	if (!storeSpot) {
//		errs() << "Couldn't get 2nd operand of " << *storeUse << "\n";
		// probably an inline GEP
		return storeUse;
	}

	/* If it isn't then this store is the first non-alloca. */
	if (! isa<AllocaInst>(storeSpot)) {
		return storeUse;
	}

	/* Init things */
	Instruction* toIgnore = dyn_cast<Instruction>(storeUse);
	Instruction* nextStore = nullptr;

	/* Otherwise, we need to look through all the uses until we find one.
	   But don't run forever, it has a limit. */
	for (int i = 0; i < 10; i++) {
		nextStore = isDereferenced(storeSpot, toIgnore);

		/* This is one exit condition, in that we didn't find any more uses. */
		if (nextStore == nullptr) {
			return nullptr;
		}

		if (nextStore) {
			if (isa<StoreInst>(nextStore)) {
				storeSpot = dyn_cast<Instruction>(nextStore->getOperand(1));
//				if (verifyDebug) errs() << " - nextStore     = " << *nextStore << "\n";
//				if (verifyDebug) errs() << " - nextStoreSpot = " << *storeSpot << "\n";
				if (! isa<AllocaInst>(storeSpot)) {
					return storeSpot;
				}
			}
			/* otherwise it's a GEP or CallInst, so definitely return it */
			else {
				return nextStore;
			}
		}
		toIgnore = nextStore;
	}
	return storeUse;
}

/*
 * Helper function that walks backwards to see if a stored value inherits from a single
 *  call to an unprotected function (skipLibCalls).
 * TODO: is there anything that could corrupt the value, even if it did inherit from here?
 */
bool dataflowProtection::comesFromSingleCall(Instruction* storeUse) {
	// default is failed
	bool returnVal = false;
	static std::set<PHINode*> seenPhiSet;

	for (int i = 0; i < storeUse->getNumOperands(); i++) {
		Value* nextVal = storeUse->getOperand(i);
		if (CallInst* ci = dyn_cast<CallInst>(nextVal)) {
			Function* calledF = ci->getCalledFunction();
			// need to handle intrinsic functions here
			if (calledF->getIntrinsicID() != Intrinsic::ID::not_intrinsic) {
				if (willBeCloned(ci)) {
					returnVal = false;
				} else {
					returnVal = true;
				}
				break;
			}

			// check that the function being called will only be called once
			if (calledF && fnToBeSkipped(calledF)) {
				returnVal = true;
				break;
			} else if (calledF && !fnToBeCloned(calledF)) {
				returnVal = true;
				break;
			} else {
				break;
			}
		}
		// the recursion is broken if we get into a PHI node loop
		else if (PHINode* nextPhi = dyn_cast<PHINode>(nextVal)) {
			// if we haven't seen it before, go ahead and follow
			if (seenPhiSet.find(nextPhi) == seenPhiSet.end()) {
				seenPhiSet.insert(nextPhi);
				return comesFromSingleCall(nextPhi);
			}
			// otherwise, problems, need to stop now
			else {
				break;
			}
		}
		// normal instructions can be followed
		else if (Instruction* nextInst = dyn_cast<Instruction>(nextVal)) {
//			if (verifyDebug) errs() << *nextInst << "\n";
			return comesFromSingleCall(nextInst);
		}
	}

	seenPhiSet.clear();
	return returnVal;
}

/*
 * Uses the first place a global is stored to determine which argument index
 *  in the call instruction inherits from the global load.
 * Although parameters are marked with 'unsigned int' type, use long for the
 *  return type here, so we can use negative error codes and still represent
 *  the entire range of integer values in 'unsigned int'.
 */
long getCallArgIndex(Instruction* instUse, CallInst* callUse) {
	static std::set<PHINode*> seenPhiSet;

	// because a StoreInst has no users (no return value), look at the users of the 2nd operand
	if (isa<StoreInst>(instUse)) {
		instUse = dyn_cast_or_null<Instruction>(instUse->getOperand(1));
		if (!instUse) {
			return -1;
		}
	}
//	if (verifyDebug) errs() << "nextStore:" << *instUse << "\n";

	/* What if the instUse is directly an operand of callUse? */
	for (unsigned int idx = 0; idx < callUse->getNumOperands(); idx += 1) {
		Value* nextOp = callUse->getOperand(idx);
		if (nextOp == instUse) {
			return idx;
		}
	}

	// look at all the uses of the instruction
	for (auto use: instUse->users()) {
		// does it match any of the the arguments?
		if (Instruction* nextUse = dyn_cast<Instruction>(use)) {
//			if (verifyDebug) errs () << "      use:" << *nextUse << "\n";
			for (unsigned int idx = 0; idx < callUse->getNumOperands(); idx += 1) {
				Value* nextOp = callUse->getOperand(idx);
//				if (verifyDebug && !isa<Function>(nextOp)) errs() << "   callop:" << *nextOp << "\n";
				if (use == nextOp) {
					return idx;
				}
			}
		}
	}

	// if we made it all the way through, look at the operands users (recursive tail call)
	for (auto op: instUse->users()) {
		if (Instruction* instNext = dyn_cast<Instruction>(op)) {
			// skip seen PHI nodes
			if (PHINode* nextPhi = dyn_cast<PHINode>(instNext)) {
				// if we haven't seen it before, go ahead and follow
				if (seenPhiSet.find(nextPhi) == seenPhiSet.end()) {
					// but mark as seen
					seenPhiSet.insert(nextPhi);
				} else {
					// skip if we've seen it before
					continue;
				}
			}

//			if (verifyDebug) errs() << "    useop:" << *op << "\n";
			long nextIdx = getCallArgIndex(instNext, callUse);
			if (nextIdx >= 0)
				return nextIdx;
		}
	}
	// if we can't find anything, error code
	return -1;
}


/*
 * Walks the uses of a load instruction to see if it's read-only,
 *  or if it's ever used in a store instruction.
 * Loading a protected global inside an unprotected function.
 *
 * Updated to also look at function arguments.
 */
void walkUnPtLoads(LoadRecordType &record) {
	Value* v = std::get<0>(record);
	LoadInst* li = dyn_cast<LoadInst>(v);

	GlobalVariable* gv = std::get<1>(record);
	Function* parentF = std::get<2>(record);

	// debug
	if (parentF->getName() == "xTimerGenericCommand" && gv->getName() == "xCtrlTimer") {
		// verifyDebug = true;
	}

	/* Have to walk the uses to see if it's ever used to store */
	Instruction* instUse = hasStoreUsage(v);
	StoreInst* storeUse = dyn_cast_or_null<StoreInst>(instUse);
	CallInst* callUse = dyn_cast_or_null<CallInst>(instUse);

	if (storeUse) {
		// get the address/register where the global is being put
		// TODO: make sure this is best way to tell if it's locally stored
		Instruction* storeSpot = getNextNonAllocaStore(storeUse);
		if (verifyDebug) errs() << "init inst:  " << *v << "\n";
		if (verifyDebug) errs() << "store use:  " << *storeUse << '\n';
//		if (verifyDebug && storeSpot) errs() << "store spot: " << *storeSpot << '\n';

		if (CallInst* callUse2 = dyn_cast_or_null<CallInst>(storeSpot)) {
			Type* storeType = storeUse->getOperand(0)->getType();

			if (storeType->isPtrOrPtrVectorTy()) {

				long argIdx = getCallArgIndex(storeUse, callUse2);
				CallRecordType newRecord = std::make_tuple(callUse2, gv, parentF, argIdx);
				unPtCallsList.push_back(newRecord);
			} else {
				;   // sync global stores?
			}
		}

		/* Otherwise, we know this is being used to write to */
		else if (storeSpot) {
			/* if the load type is a scalar value, it's fine */

			Type* loadType = v->getType();
			if (loadType->isPtrOrPtrVectorTy()) {
				if (li)
					writeToGlobalMap(unPtReadsFromPtGlbls, gv, parentF, li);
				else
					writeToGlobalMap(unPtReadsFromPtGlbls, gv, parentF, storeSpot);
			} else {
				;	// it's fine, can't be dereferenced
			}
		}
	}

	else if (callUse) {
		if (li) {
//			errs() << *li << "\n";
			long argIdx = getCallArgIndex(li, callUse);
			CallRecordType newRecord = std::make_tuple(callUse, gv, parentF, argIdx);
			unPtCallsList.push_back(newRecord);
		} else {
			writeToGlobalMap(unPtCallsWithPtGlbls, gv, parentF, callUse);
		}
	}

	verifyDebug = false;
}


/*
 * Same as above, but for loading unprotected globals by protected functions.
 */
// #define DBG_WALK_PT_LOADS
void walkPtLoads(LoadRecordType &record) {
	Value* v = std::get<0>(record);
	LoadInst* li = dyn_cast<LoadInst>(v);

	GlobalVariable* gv = std::get<1>(record);
	Function* parentF = std::get<2>(record);

	/* If we're loading a scalar, it's fine */
	Type* loadType = v->getType();
	if (!loadType->isPtrOrPtrVectorTy()) {
		return;
	}

	#ifdef DBG_WALK_PT_LOADS
	// debug
	if (parentF->getName() == "protectedPtrArrayWrite" && gv->getName() == "globalPtr") {
		verifyDebug = true;
	}
	#endif

	Instruction* instUse = hasStoreUsage(v);
	StoreInst* storeUse = dyn_cast_or_null<StoreInst>(instUse);
	CallInst* callUse = dyn_cast_or_null<CallInst>(instUse);

	if (storeUse) {
		Instruction* storeSpot = getNextNonAllocaStore(storeUse);
		#ifdef DBG_WALK_PT_LOADS
		// breaks for Argument* 's
		// if (verifyDebug) errs() << "loadUse:  " << *li << "\n";
		if (verifyDebug) errs() << "storeUse: " << *storeUse << "\n";
		if (verifyDebug && storeSpot) errs() << "storeSpot:" << *storeSpot << "\n\n";
		#endif

		if (CallInst* callUse2 = dyn_cast_or_null<CallInst>(storeSpot)) {
			Function* calledF = callUse2->getCalledFunction();

			/* Skip it if it's to a function which is only called once (skipLibCalls) */
			if (fnToBeSkipped(calledF)) {
				return;
			}

			/* Check the type of the argument being used */
			Type* argType = storeUse->getOperand(0)->getType();

			if (argType->isPtrOrPtrVectorTy()) {
				long argIdx = getCallArgIndex(storeUse, callUse2);
				CallRecordType newRecord = std::make_tuple(callUse2, gv, parentF, argIdx);
				ptCallsList.push_back(newRecord);
			} else {
				;   // sync global stores?
			}
		}

		else if (storeSpot) {
			Type* storeType = storeUse->getOperand(0)->getType();

			if (storeType->isPtrOrPtrVectorTy()) {
				if (li) {
					writeToGlobalMap(ptWritesToUnPtGlbls, gv, parentF, li);
				} else {
					writeToGlobalMap(ptWritesToUnPtGlbls, gv, parentF, storeUse);
				}
			} else {
				syncGlobalStores.insert(storeUse);
				storeUse->getDebugLoc();
			}
		}
	}

	else if (callUse) {
		Function* calledF = callUse->getCalledFunction();

		/* Skip it if it's to a function which is only called once (skipLibCalls) */
		if (fnToBeSkipped(calledF)) {
			return;
		}

		/* Add this to the list to report */
//		if (verifyDebug)
//			errs() << "call use: " << *callUse << "\n";
//		if (verifyDebug && li)
//			errs() << "load use: " << *li << "\n";
		if (li) {
			long argIdx = getCallArgIndex(li, callUse);
			CallRecordType newRecord = std::make_tuple(callUse, gv, parentF, argIdx);
			ptCallsList.push_back(newRecord);
		} else {
			writeToGlobalMap(ptCallsWithUnPtGlbls, gv, parentF, callUse);
		}
	}

	#ifdef DBG_WALK_PT_LOADS
	verifyDebug = false;
	#endif
}


/*
 * Storing to unprotected globals from protected functions.
 */
// #define DBG_WALK_UNPT_STORES
void dataflowProtection::walkUnPtStores(StoreRecordType &record) {
	StoreInst* si = std::get<0>(record);
	GlobalVariable* gv = std::get<1>(record);
	Function* parentF = std::get<2>(record);
	Type* storeType = si->getOperand(0)->getType();

	// have we been asked to skip it?
	if (shouldSkipGlobalUsage(gv, parentF)) {
		return;
	}

	#ifdef DBG_WALK_UNPT_STORES
	// debug
	if (parentF->getName() == "scalarMultiply" && gv->getName() == "matrix0") {
		verifyDebug = true;
		errs() << *si << "\n";
	}
	#endif

	/* There are some functions which are only called once (skipLibCalls).
	 * Writing from these return values is not an issue. */
	if (comesFromSingleCall(si)) {
		#ifdef DBG_WALK_UNPT_STORES
		if (verifyDebug) errs() << "but comes from single call\n";
		#endif
		return;
	}

	if (storeType->isPtrOrPtrVectorTy()) {
		/* This is actually OK if the thing being pointed to is const
		 * but LLVM doesn't have a nice way of checking this.
		 * Walk the uses and see if the pointer it's stored in is dereferenced */
//		if (verifyDebug) errs() << "storeRecord: " << *si << "\n";

		AllocaInst* ai = findAllocaInst(si);
		if (ai) {
			Instruction* laterUse = isDereferenced(ai, si);

			if (laterUse) {
				writeToGlobalMap(ptWritesToUnPtGlbls, gv, parentF, si);
			} else {
				;    // it is read-only
			}
		} else {
			writeToGlobalMap(ptWritesToUnPtGlbls, gv, parentF, si);
		}

	}

	/* We can vote on these values before storing */
	else {
		syncGlobalStores.insert(si);
	}

	#ifdef DBG_WALK_UNPT_STORES
	verifyDebug = false;
	#endif
}


/*
 * Verify that all of the options used to configure COAST for this pass are safe to follow.
 *
 * Here are the rules:
 *
 * +---------+-------------------------+-------------------------------------+
 * |  ====================  Protected -> Not Protected  ===================  |
 * +---------+-------------------------+-------------------------------------+
 * |         | Reading                 | Writing                             |
 * +---------+-------------------------+-------------------------------------+
 * | Value   | OK                      | OK                                  |
 * |         |                         | Vote first to preserve protection   |
 * +---------+-------------------------+-------------------------------------+
 * |         |                         | A pointer can only be stored        |
 * | Pointer | Only if it's never used | if the value it points to is const. |
 * |         | to write (const)        | No voting is allowed                |
 * |         |                         | and non-consts are not allowed.     |
 * +---------+-------------------------+-------------------------------------+
 * |  ====================  Not Protected -> Protected  ===================  |
 * +---------+-------------------------+-------------------------------------+
 * |         | Reading                 | Writing                             |
 * +---------+-------------------------+-------------------------------------+
 * | Value   | OK                      | Not OK                              |
 * +---------+-------------------------+-------------------------------------+
 * | Pointer | OK (?)                  | Not OK                              |
 * +---------+-------------------------+-------------------------------------+
 *
 * Since the LLVM IR does not contain information about a variable's
 *  const-ness, our pass looks for these usages itself.
 * The load of any protected pointer must be followed to the end of the
 *  use chain to make sure no attempts are made to write to this address.
 *
 * TODO: track pointers across function calls
 */
void dataflowProtection::verifyOptions(Module& M) {
	fnsToClone_ptr = &fnsToClone;

    // catalog all the loads across the replication boundary
    std::list< LoadRecordType > unPtLoadRecords;
    std::list< LoadRecordType > ptLoadRecords;
    std::list< StoreRecordType > unPtStoreRecords;

    // look through the protected global variables
    for (auto g : globalsToClone) {
		// get all the users
		for (auto u : g->users()) {
			// is it an instruction?
			if (Instruction* UI = dyn_cast<Instruction>(u)) {
				Function* parentF = UI->getParent()->getParent();

                // is the instruction in a protected function?
				if (fnsToClone.find(parentF) == fnsToClone.end()) {

                    /* Any stores in here are not allowed (non-protected function to protected global) */
					if (StoreInst* si = dyn_cast<StoreInst>(UI)) {
						// add it to the list of infractions
						writeToGlobalMap(unPtWritesToPtGlbls, g, parentF, si);
					}
                    
                    /* note any load instructions to track later */
                    else if (LoadInst* li = dyn_cast<LoadInst>(UI)) {
                        LoadRecordType newRecord = std::make_tuple(li, g, parentF);
                        unPtLoadRecords.push_back(newRecord);
                    }
                }

            }
			/* end instruction use */
            
            /* GEPs are often inline */
			else if (ConstantExpr* CE = dyn_cast<ConstantExpr>(u)) {
                if (CE->isGEPWithNoNotionalOverIndexing()) {
                    for (auto cu : CE->users()) {
						if (LoadInst* li = dyn_cast<LoadInst>(cu)) {
                            Function* parentF = li->getParent()->getParent();

                            // is the instruction in a protected function?
                            if (fnsToClone.find(parentF) == fnsToClone.end()) {
                            	LoadRecordType newRecord = std::make_tuple(li, g, parentF);
                            	unPtLoadRecords.push_back(newRecord);
                            }
                        }
                    }
                }
            }
			/* end GEP use */
        }
    }

    // Make sure that all unprotected globals are not used in protected functions
	auto moduleEnd = M.global_end();
	for (auto g = M.global_begin(); g != moduleEnd; g++) {
		GlobalVariable* gv = &(*g);
		if (willBeCloned(gv)) {
			continue;
		}

		/* Skip any globals that are constant, because we can't change them anyway. */
		else if (gv->isConstant()) {
			continue;
		}

        /* Now it's either in globalsToSkip, or not marked at all.
         * Either way, shouldn't be used in a protected function. */
        else {
			for (auto u : gv->users()) {
				// is it an instruction?
				if (Instruction* UI = dyn_cast<Instruction>(u)) {
					Function* parentF = UI->getParent()->getParent();

					// debug
					if (gv->getName() == "vTaskSwitchContext" && parentF->getName() == "pxCurrentTCB") {
						// verifyDebug = true;
					}

                    // is the instruction in a protected function?
					if (fnsToClone.find(parentF) != fnsToClone.end()) {
                        /* Stores to unprotected globals from protected functions are not allowed */
						if (fnsToSkip.find(parentF) != fnsToSkip.end()) {
							continue;
						}
						if (StoreInst* si = dyn_cast<StoreInst>(UI)) {
                            StoreRecordType newRecord = std::make_tuple(si, gv, parentF);
                            unPtStoreRecords.push_back(newRecord);
//                            if (verifyDebug) errs() << *si << '\n';
                        }

                        /* We want to walk the load uses here too to find any later stores */
						else if (LoadInst* li = dyn_cast<LoadInst>(UI)) {
                            LoadRecordType newRecord = std::make_tuple(li, gv, parentF);
                            ptLoadRecords.push_back(newRecord);
                        }
                    }
                }
				/* end instruction use */

                /* GEPs are often inline */
				else if (ConstantExpr* CE = dyn_cast<ConstantExpr>(u)) {
					if (CE->isGEPWithNoNotionalOverIndexing()) {

                        for (auto cu : CE->users()) {
							/* GEPs used by stores are what we're looking for */
							if (StoreInst* si = dyn_cast<StoreInst>(cu)) {
                                Function* parentF = si->getParent()->getParent();

                                // is the instruction in a protected function?
                                if (fnsToClone.find(parentF) != fnsToClone.end()) {
                                	StoreRecordType newRecord = std::make_tuple(si, gv, parentF);
                                	unPtStoreRecords.push_back(newRecord);
                                }
                            }
							else if (ConstantExpr* CE2 = dyn_cast<ConstantExpr>(cu)) {
								/* more casts hiding inside of things
								 * possible ugly things we have to deal with:
								 * store <4 x i32> %1, <4 x i32>* bitcast (i32* getelementptr inbounds ([2 x [8 x i32]], [2 x [8 x i32]]* @matrix0, i64 0, i64 0, i64 4) to <4 x i32>*)
								 */
								if (CE2->isCast()) {
									if (noMemReplicationFlag)
										continue;
									for (auto user : CE2->users()) {
										if (StoreInst* si = dyn_cast<StoreInst>(user)) {
											Function* parentF = si->getParent()->getParent();

											// is the instruction in a protected function?
											if (fnsToClone.find(parentF) != fnsToClone.end()) {
												StoreRecordType newRecord = std::make_tuple(si, gv, parentF);
												unPtStoreRecords.push_back(newRecord);
											}
										}
									}
								}
							}
                        }
                    }
					/* end GEP use */
					else if (CE->isCast()) {
						/* casts hiding inside things -
						 * see cloneConstantExprOperands in cloning.cpp */
						if (noMemReplicationFlag)
							continue;

						// have to see if any of it's users are instructions
						for (auto user : CE->users()) {
							// specifically Store instructions
							if (StoreInst* si = dyn_cast<StoreInst>(user)) {
								Function* parentF = si->getParent()->getParent();

								// is the instruction in a protected function?
								if (fnsToClone.find(parentF) != fnsToClone.end()) {
									StoreRecordType newRecord = std::make_tuple(si, gv, parentF);
									unPtStoreRecords.push_back(newRecord);
								}
							}
						}
					}
					/* end other ConstantExpr use */
                }

				else {
					PRINT_STRING("-- unidentified global user:");
					PRINT_VALUE(u);
				}
				verifyDebug = false;
            }
        }
    }

	/* Repeat these as long as the sizes keep increasing */
	while (true) {
		/* Each unprotected load should be traced to make sure it's read-only. */
		for (auto record : unPtLoadRecords) {
			walkUnPtLoads(record);
		}
		unPtLoadRecords.clear();

		for (auto record : ptLoadRecords) {
			walkPtLoads(record);
		}
		ptLoadRecords.clear();
		/* Clear afterwards for later checking. */

		/* Now we have to walk all of the calls */
		// protected functions using unprotected globals
//		if (ptCallsList.size() > 0)
//			errs() << "\nprotected functions using unprotected globals in calls:\n";
		for (auto record : ptCallsList) {
			CallInst* ci = std::get<0>(record);
			GlobalVariable* gv = std::get<1>(record);
			Function* parentF = std::get<2>(record);
			long argIdx = std::get<3>(record);

			/* Being used by protected or unprotected function is fine, as long as it's read-only.
			 * Track each argument use as if it was a load instruction of the global. */

			Function* calledFunction = ci->getCalledFunction();
			if (!calledFunction) {
				/* If this is null, then this is an indirect function call, which we can't track.
				 * Conservatively add this to the list of things that are not allowed. */
				writeToGlobalMap(unPtCallsWithPtGlbls, gv, parentF, ci);
				continue;
			}

			/* If the function being called is not protected, this is fine.
			 * Unless the callee then calls a protected function with the argument
			 *  and it's not read-only? */
			if (fnsToClone.find(calledFunction) == fnsToClone.end()) {
				continue;	// TODO: above comment
			}

			/* If it's a call to a replFnCalls function, then that's not allowed */
			if (isCoarseGrainedFunction(calledFunction->getName())) {
				writeToGlobalMap(ptCallsWithUnPtGlbls, gv, parentF, ci);
				continue;
			}

			if (argIdx < 0) {
				/* Then we couldn't find the relationship. Conservatively add to the list
				 * of things that are not allowed. */
				errs() << info_string << " Couldn't find argument index for call:\n" << *ci << "\n";
				errs() << "  (using unprotected global '" << gv->getName() << "' in basic block '"
					   << ci->getParent()->getName() << "' of function '" << parentF->getName() << "')\n";
				writeToGlobalMap(ptCallsWithUnPtGlbls, gv, parentF, ci);
				continue;
			}

			auto argIter = calledFunction->arg_begin() + argIdx;
			LoadRecordType newRecord = std::make_tuple(&(*argIter), gv, calledFunction);
			ptLoadRecords.push_back(newRecord);
		}
		ptCallsList.clear();

//		if (unPtCallsList.size() > 0)
//			errs() << "\nunprotected functions using protected globals in calls:\n";
		// unprotected functions using protected globals
		for (auto record : unPtCallsList) {
			CallInst* ci = std::get<0>(record);
			GlobalVariable* gv = std::get<1>(record);
			Function* parentF = std::get<2>(record);
			long argIdx = std::get<3>(record);

			if (argIdx < 0) {
				/* Then we couldn't find the relationship. Conservatively add to the list
				 * of things that are not allowed. */
				errs() << info_string << " Couldn't find argument index for call:\n" << *ci << "\n";
				errs() << "  (using protected global '" << gv->getName() << "' in basic block '"
					   << ci->getParent()->getName() << "' of function '" << parentF->getName() << "')\n";
				writeToGlobalMap(unPtCallsWithPtGlbls, gv, parentF, ci);
				continue;
			}
//			errs() << "CallInst: " << *ci << "\n";

			/* Being used by protected or unprotected function is fine, as long as it's read-only.
			 * Track each argument use as if it was a load instruction of the global. */

			Function* calledFunction = ci->getCalledFunction();
			if (!calledFunction) {
				/* If this is null, then this is an indirect function call, which we can't track.
				 * Conservatively add this to the list of things that are not allowed. */
				writeToGlobalMap(unPtCallsWithPtGlbls, gv, parentF, ci);
				continue;
			} else if (calledFunction->isVarArg()) {
				/* We also can't track functions that are variadic, because of
				 * the way LLVM does function argument lists.
				 */
				writeToGlobalMap(unPtCallsWithPtGlbls, gv, parentF, ci);
				continue;
			}
//			errs() << "Inside function '" << parentF->getName() << "'\n";

			auto argIter = calledFunction->arg_begin() + argIdx;
			// TODO: might want to use ->getArg() instead, but that function
			//  isn't available until LLVM version 10

// 			errs() << "operand number " << argIdx << " of function '" << calledFunction->getName() << "': ";
//			errs() << *argIter; // << "\n Of type: " << *argIter->getType();
//			errs() << "\nUses:\n";
//			for (auto use : argIter->users()) {
//				errs() << *use << "\n";
//			}

			if (argIdx >= calledFunction->arg_size()) {
				errs() << err_string
					   << " function doesn't have that many arguments! (0 indexed)\n"
					   << "  " << calledFunction->getName()
					   << " (" << argIdx << " >= "
					   << calledFunction->arg_size() << ")\n";
			}
			LoadRecordType newRecord = std::make_tuple(&(*argIter), gv, calledFunction);
			// put it in the right list
			if (fnsToClone.find(calledFunction) == fnsToClone.end()) {
				// not protected function
				unPtLoadRecords.push_back(newRecord);
			} else {
				// protected function
				ptLoadRecords.push_back(newRecord);
			}
		}
//		errs() << "\n";
		unPtCallsList.clear();

		/* Leaving conditions */
		if (unPtLoadRecords.size() == 0 && ptLoadRecords.size() == 0) {
			break;
		}
	}

	/* This is only done once, so outside the loop */
    for (auto record : unPtStoreRecords) {
    	walkUnPtStores(record);
    }

    /* Print scope crossing warning messages */
	// referencing protected globals from unprotected functions
	printGlobalScopeErrorMessage(unPtWritesToPtGlbls, true, "written in");
	printGlobalScopeErrorMessage(unPtReadsFromPtGlbls, true, "read in");
	if (unPtReadsFromPtGlbls.size() > 0) {
		errs() << " -- Please verify that these kinds of reads are read-only --\n";
	}

	// referencing unprotected globals from protected functions
	printGlobalScopeErrorMessage(ptWritesToUnPtGlbls, false, "read from and written to inside");

	// using globals across scope boundaries in function calls
	printGlobalScopeErrorMessage(ptCallsWithUnPtGlbls, false, "used in a function call in");
	printGlobalScopeErrorMessage(unPtCallsWithPtGlbls, true, "used in a function call in");
	if ( (ptCallsWithUnPtGlbls.size() > 0) || (unPtCallsWithPtGlbls.size() > 0) ) {
		errs() << " -- COAST currently does not support tracking global pointer crossings across function calls --\n";
	}

	// kill the compilation if we saw any of these errors
	if ( 	(unPtWritesToPtGlbls.size()  > 0)  ||
			(unPtReadsFromPtGlbls.size() > 0)  ||
			(ptWritesToUnPtGlbls.size()  > 0)  ||
			(ptCallsWithUnPtGlbls.size() > 0)  ||
			(unPtCallsWithPtGlbls.size() > 0)  )
	{
		errs() << "\nExiting...\n";
		// good place for debug
		dumpModule(M);
		std::exit(-1);
	}

	// print some more stats
	if (verboseFlag && syncGlobalStores.size() > 0) {
		errs() << info_string << " syncing before store\n";
		for (auto si : syncGlobalStores) {
			errs() << *si << "\n  in function '"
				   << si->getParent()->getParent()->getName() << "'\n";
		}
	}

	return;
}


void dataflowProtection::printGlobalScopeErrorMessage(GlobalFunctionSetMap &globalMap,
		bool globalPt, std::string directionMessage)
{
	// short circuit
	if (globalMap.size() == 0) {
		return;
	}

	// printing specific to this set map
	std::string firstMessage;
	std::string secondMessage;
	if (globalPt) {
		firstMessage = err_string + " protected global \"";
		secondMessage = "\" is being " + directionMessage + " unprotected functions:\n";
	} else {
		firstMessage = err_string + " unprotected global \"";
		secondMessage = "\" is being " + directionMessage + " protected functions:\n";
	}

	// look at all the items
	for (auto item : globalMap) {
		errs() << firstMessage << item.first->getName() << secondMessage;
		for (auto fnSet : item.second) {
			Function* f = fnSet.first;
			Instruction* i = fnSet.second;
			auto dbgInfo = i->getDebugLoc();
			errs() << "\t\"" << f->getName() << "\"";
			if (CallInst* callUse = dyn_cast<CallInst>(i)) {
				Function* calledF = callUse->getCalledFunction();
				if (calledF && calledF->hasName()) {
					errs() << " in call to \"" << calledF->getName() << "\"";
				}
			}
			errs() << " at ";
			if (dbgInfo) {
				dbgInfo.print(errs());
				errs() << ",\n";
			} else {
				errs() << "  " << *i << ",\n";
			}
		}
	}
}
