/*
 * utils.cpp
 *
 * This file contains utilities that help set up or clean up after the pass.
 */

#include "dataflowProtection.h"

// standard library includes
#include <queue>
#include <list>
#include <set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <cstdlib>

// LLVM includes
#include <llvm/Option/Option.h>
#include <llvm/IR/Module.h>
#include "llvm/Support/CommandLine.h"
#include <llvm/Support/Debug.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include "llvm/ADT/StringRef.h"

using namespace llvm;


// Command line options
extern cl::opt<bool> InterleaveFlag;
extern cl::opt<bool> noMemReplicationFlag;
extern cl::opt<bool> ReportErrorsFlag;
extern cl::opt<bool> dumpModuleFlag;
extern cl::opt<bool> verboseFlag;
extern std::set<ConstantExpr*> annotationExpressions;


//----------------------------------------------------------------------------//
// Cleanup unused things
//----------------------------------------------------------------------------//
// #define DEBUG_DELETE_FUNCTIONS
// returns the number of functions removed
int dataflowProtection::removeUnusedFunctions(Module& M) {
	int numRemoved = 0;

	// get reference to main() function
	Function* mainFunction = M.getFunction("main");
	// If we don't have a main, don't remove any functions
	if (!mainFunction) {
		return 0;
	}

	// Populate a list of all functions in the module
	std::set<Function*> functionList;
	for (auto & F : M) {
		// Ignore external function declarations
		if (F.hasExternalLinkage() && F.isDeclaration()) {
			continue;
		}

		// Don't erase fault handlers
		if (F.getName().startswith("FAULT_DETECTED_")) {
			continue;
		}

		// Don't erase ISRs
		if (isISR(F))
			continue;

		#ifdef DEBUG_DELETE_FUNCTIONS
		bool debugDeleteFlag = false;
		if (F.hasName() && (F.getName() == "vStdioWithCWDTest")) {
			errs() << " == " << F.getName() << " == \n";
			debugDeleteFlag = true;
		}
		#endif

		// Have to detect unused recursive functions
		bool noSkip = false;
		size_t userCount = 0, userRecurse = 0;
		for (User* U : F.users()) {
			userCount++;
			// calls for which the called function is the same as the parent function are recursive
			if (auto callUse = dyn_cast<CallInst>(U)) {
				Function* calledF = callUse->getCalledFunction();
				Function* parentF = callUse->getParent()->getParent();
				// if it calls the function that it's in
				if (calledF == parentF) {
					userRecurse++;
				}
				#ifdef DEBUG_DELETE_FUNCTIONS
				else if (debugDeleteFlag) {
					errs() << " call is in " << parentF->getName() << "\n";
				}
				#endif
			}
			else if (auto funcUse = dyn_cast<Function>(U)) {
				// strange issues with functions reported as using themselves, but not actually anywhere
				if (funcUse == &F) {
					userRecurse++;
				}
				else if (funcUse->getName() == F.getName()) {
					errs() << "why do pointers not match?\n"
						   << funcUse << " != " << &F << " ?\n";
					assert(false && "multiple copies of the same function?");
				}
			}
			#ifdef DEBUG_DELETE_FUNCTIONS
			// any other users?
			if (debugDeleteFlag) {
				if (auto funcUse = dyn_cast<Function>(U)) {
					PRINT_STRING(funcUse->getName());
				} else {
					PRINT_VALUE(U);
				}
			}
			#endif
		}
		#ifdef DEBUG_DELETE_FUNCTIONS
		// report how many uses were found
		if (debugDeleteFlag) {
			errs() << "Found " << userCount << " users\n";
		}
		#endif
		if (userCount != userRecurse) {
			continue;
		}

		// functions that we are told by the application programmer are used
		if (usedFunctions.find(&F) != usedFunctions.end())
			continue;

		// everything else may be considered unused
		functionList.insert(&F);
	}

	recursivelyVisitCalls(M, mainFunction, functionList);

	if (functionList.size() == 0) {
		return 0;
	}

	// It's possible for a xMR'd function to be in the list of no uses,
	//  if it's used as a function pointer only.
	// The other reason would be if the function was replicated by default, but it is used in as
	//  a function pointer, in which case the code would still just use the original version.
	for (auto q : functionList) {
		if (verboseFlag) errs() << "    " << q->getName() << "\n";
		q->eraseFromParent();
		numRemoved++;
	}

	return numRemoved;
}


void dataflowProtection::removeOrigFunctions() {
	if (verboseFlag)
		PRINT_STRING("Removing original & unused functions:");
	for (auto F : origFunctions) {
		// TODO: why is this not just fnsToClone?
		if (fnsToCloneAndSkip.find(F) == fnsToCloneAndSkip.end()) {
			/*
			 * If not all of the uses are gone, then this function likely is called from within
			 * and without the Scope Of Replication (SOR). We'll keep it around in that case.
			 */
			if (F->use_empty()) {
				if (verboseFlag && F->hasName()) {
					errs() << "    " << F->getName() << "\n";
				}
				F->eraseFromParent();
			} else if (F->hasOneUse()) {
				// also remove if the only user was in llvm.global.annotations
				// this was removed, but some of the expressions stuck around
				auto F_use = *(F->user_begin());
				if (ConstantExpr* ce = dyn_cast<ConstantExpr>(F_use)) {
					if (annotationExpressions.find(ce)
							!= annotationExpressions.end())
					{
						F->eraseFromParent();
					}
				}
			}
		}
	}
}

void dataflowProtection::removeUnusedErrorBlocks(Module & M) {
	for (auto &F : M) {
		BasicBlock * errorBlock = errBlockMap[&F];
		if (!errorBlock)
			continue;

		if (errorBlock->getNumUses() == 0) {
			errorBlock->eraseFromParent();
		}
	}
}

void dataflowProtection::removeUnusedGlobals(Module& M) {
	std::vector<GlobalVariable*> unusedGlobals;

	for (GlobalVariable & g : M.getGlobalList()) {
		if (volatileGlobals.find(&g) != volatileGlobals.end()) {
			/* Skip removing globals marked as volatile.
			 * COAST uses the GCC attribute "used", meaning to not remove the variable.
			 */
			continue;
		} else if (g.getNumUses() == 0) {
			StringRef gName = g.getName();
			// Don't touch ISR related variables
			if (!(gName.startswith("llvm")) ) {
				unusedGlobals.push_back(&g);
			}
		} else if (g.getNumUses() == 1) {
			for (auto u : g.users()) {
				if (Instruction* UI = dyn_cast<Instruction>(u)) {
					// If it's in a function marked as __attribute__((used)), then skip this
					BasicBlock* UIparentBB = UI->getParent();
					if (UIparentBB) {
						Function* parentF = UIparentBB->getParent();
						if (usedFunctions.find(parentF) != usedFunctions.end()) {
							continue;
						}
					}
				}
				// Account for instructions that will be cleaned up at the end of the pass
				// it could also be a call instruction to a library function that has side effects, but
				//  we ignore the return value
				if ( (u->getNumUses() == 0) && !isa<StoreInst>(u) && !isa<CallInst>(u) && !isa<InvokeInst>(u)) {
					unusedGlobals.push_back(&g);
				}
			}
		}
	}

	if (verboseFlag && (unusedGlobals.size() > 0)) {
		PRINT_STRING("Removing unused globals:");
	}
	for (auto ug : unusedGlobals) {
		if (verboseFlag) {
			errs() << "    " << ug->getName() << "\n";
		}
		if (ug->getParent()) {
			ug->eraseFromParent();
		} else {
			errs() << warn_string << " global parent doesn't exist?\n" << *ug << "\n";
		}
	}
}

void dataflowProtection::checkForUnusedClones(Module & M) {
	for (auto cloneM : cloneMap) {
		Value* orig = cloneM.first;
		Value* clone = cloneM.second.first;

		if (clone->getNumUses() == 0) {
			// Store instructions aren't cloned
			if (isa<StoreInst>(clone))
				continue;

			// If the original isn't used, the clone will not be either
			if (orig->getNumUses() == 0)
				continue;

			// Used only in a single external function call, eg printf
			if (orig->hasOneUse() && isa<CallInst>(orig->user_back())) {
				if (CallInst* CI = dyn_cast<CallInst>(orig->user_back())) {
					if (isIndirectFunctionCall(CI, "checkForUnusedClones"))
						continue;
					else if (CI->getCalledFunction()->hasExternalLinkage())
						continue;
				}
			}

			// If original is only used in external function calls
			if (Instruction* inst = dyn_cast<Instruction>(orig)) {
				// accumulator - proof by contradiction
				bool allExternal = true;
				for (auto u : inst->users()) {
					if (CallInst* ci = dyn_cast<CallInst>(u)) {
						// make sure we're not calling a function on a null pointer
						if (isIndirectFunctionCall(ci, "checkForUnusedClones"))
							continue;
						else if (ci->getCalledFunction()->hasExternalLinkage())
							continue;
						else {
							allExternal = false;
							break;
						}
					}
				}
				if (allExternal) continue;

				// sometimes clones are erroneously created when the instructions were supposed to be skipped
				if (willBeSkipped(inst)) {
					if (verboseFlag) errs() << "Removing unused local variable: " << *inst << "\n";
					inst->eraseFromParent();

					if (TMR) {
						Instruction* inst2 = dyn_cast<Instruction>(cloneM.second.second);
						if (verboseFlag) errs() << "Removing unused local variable: " << *inst2 << "\n";
						inst2->eraseFromParent();
					}
				}

				// TODO: add here, also when function calls are supposed to be skipped
			}

			// Global duplicated strings aren't used in uncloned printfs. Remove the unused clones
			if (ConstantExpr* ce = dyn_cast<ConstantExpr>(clone)) {
				if (verboseFlag) errs() << "Removing unused global string: " << *ce << "\n";
				ce->destroyConstant();
				if (TMR) {
					ConstantExpr* ce2 = dyn_cast<ConstantExpr>(cloneM.second.second);
					if (verboseFlag) errs() << "Removing unused global string: " << *ce2 << "\n";
					ce2->destroyConstant();
				}
				continue;
			}

			if (GlobalVariable* GV = dyn_cast<GlobalVariable>(clone)) {
				continue;
			}

			// If using noMemDuplicationFlag then don't worry about unused arguments
			if (noMemReplicationFlag) {
				if (dyn_cast<Argument>(orig)) {
					continue;
				}
			}

			// Doesn't work yet because have to get rid of all references to these instructions
			//  or move for segmenting breaks.
//			if(Instruction* inst = dyn_cast<Instruction>(clone)) {
//				if (verboseFlag)
//					errs() << "Removing unused clone: " << *inst << "\n";
//				inst->eraseFromParent();
//				if (TMR) {
//					Instruction* inst2 = dyn_cast<Instruction>(cloneM.second.second);
//					inst2->eraseFromParent();
//				}
//
//				continue;
//			}

			errs() << info_string << " unused clone: " << *clone << ":\n";
//			errs() << err_string << " when updating cloned instructions.\n";
//			errs() << "More about " << *clone << ":\n";
//			errs() << "  Orig:" << *orig << "\n";
//			errs() << "  Orig has " << orig->getNumUses() << " uses\n";
//			Instruction* tmp = dyn_cast<Instruction>(orig->user_back());
//			errs() << "      " << *orig->user_back() << " in " << tmp->getParent()->getName() << "\n";
//			errs() << "\n" << *clone << " has no users\n\n";
//			errs() << *tmp->getParent() << "\n";
//			assert(false && "Clone has no users");
		}
	}
}


//----------------------------------------------------------------------------//
// Synchronization utilities
//----------------------------------------------------------------------------//
// #define DEBUG_INST_MOVING
void dataflowProtection::moveClonesToEndIfSegmented(Module & M) {
	if (InterleaveFlag)
		return;

#ifdef DEBUG_INST_MOVING
	int flag = 0;
#endif
	for (auto F : fnsToClone) {
		for (auto & bb : *F) {

#ifdef DEBUG_INST_MOVING
			if (bb.getName() == "entry" && F->getName() == "returnTest.RR") {
				flag = 1;
			}

			if (flag) {
				errs() << F->getName() << "\n";
				errs() << "bb: " << bb << "\n";
			}
#endif

			// Populate list of things to move before
			std::queue<Instruction*> movePoints;
			for (auto &I : bb) {
				if (CallInst* CI = dyn_cast<CallInst>(&I)) {
					/* Fixed an issue where the clone was considered a syncPoint, but wasn't
					 * in the startOfSyncLogic map, so it was inserting a new element and
					 * putting in the default Instruction* value (whatever that is) into the
					 * movePoints map
					*/
					if (isSyncPoint(CI) && (startOfSyncLogic.find(&I) != startOfSyncLogic.end()) ) {
//						errs() << "    Move point at CI sync" << *startOfSyncLogic[&I] << "\n";
						movePoints.push(startOfSyncLogic[&I]);
					}
					else if (CI->getCalledFunction() != nullptr && CI->getCalledFunction()->isIntrinsic()) {
						;	// don't add intrinsics, because they will be expanded underneath (in assembly)
							//  to be a series of inline instructions, not an actual call
						// TODO: might want to look at getIntrinsicID() instead, because
						//  then we can compare enum ranges instead of just names
					}
					else {
//						errs() << "    Move point at CI " << I << "\n";
						movePoints.push(&I);
					}
				} else if (TerminatorInst* TI = dyn_cast<TerminatorInst>(&I)) {
					if (isSyncPoint(TI)) {
//						errs() << "    Move point at TI sync " << *startOfSyncLogic[&I] << "\n";
						movePoints.push(startOfSyncLogic[&I]);
					} else {
//						errs() << "    Move point at TI" << I << "\n";
						movePoints.push(&I);
					}
				} else if (StoreInst* SI = dyn_cast<StoreInst>(&I)) {
					if (isSyncPoint(SI)) {
						/*
						 * One problem we saw was when a basic block was split, the instruction which
						 * is the startOfSyncLogic for a following instruction would be in the block
						 * before the split.  So it was a valid instruction, but it never matched the
						 * check below because it was in a different basic block. Same check added to
						 * the GEP checker
						 */
						if ( (startOfSyncLogic.find(&I) != startOfSyncLogic.end() ) && \
							 (startOfSyncLogic[&I]->getParent() == I.getParent()) ) {
							movePoints.push(startOfSyncLogic[&I]);
//							errs() << "    Move point at SI" << *startOfSyncLogic[&I] << "\n";
						} else {
							movePoints.push(&I);
//							errs() << "    Move point at SI: " << *SI << "\n";
						}
					}
					/* There is a case where we need to keep the stores next to each other, as in the
					 * load-increment-store pattern.  For StoreInst's which aren't syncpoints, this would
					 * cause the variable to be incremented twice.  Check for if it has a clone and if
					 * the type being stored is not a pointer. */
					else if (isStoreMovePoint(SI)) {
						movePoints.push(&I);
					}
				} else if (GetElementPtrInst* GI = dyn_cast<GetElementPtrInst>(&I)) {
					if (isSyncPoint(GI)) {
						// not all GEP syncpoints have a corresponding entry in the map
						if ( (startOfSyncLogic.find(&I) != startOfSyncLogic.end() ) &&
							 (startOfSyncLogic[&I]->getParent() == I.getParent()) ) {
							movePoints.push(startOfSyncLogic[&I]);
						} else {
							movePoints.push(&I);
						}
					}
				}
			}

			std::vector<Instruction*> listI1;
			std::vector<Instruction*> listI2;

			// Move all clones before the sync points
			for (auto & I : bb) {
#ifdef DEBUG_INST_MOVING
				if (flag) {
					// errs() << I << "\n";
				}
#endif
				// see if it's a clone
				if (PHINode* PN = dyn_cast<PHINode>(&I)) {
					// don't move it, phi nodes must be at the start
				} else if ( (getClone(&I).first != &I) && !(isSyncPoint(&I))
							&& !(isStoreMovePoint(dyn_cast<StoreInst>(&I)))
							&& !(isCallMovePoint(dyn_cast<CallInst>(&I)))
						/* could also check if it's the head of the list */
				) {
					Instruction* cloneI1 = dyn_cast<Instruction>(getClone(&I).first);
					listI1.push_back(cloneI1);
					#ifdef DEBUG_INST_MOVING
					if (flag) {
						errs() << "to move: " << I << "\n";
					}
					#endif
					if (TMR) {
						Instruction* cloneI2 = dyn_cast<Instruction>(getClone(&I).second);
						listI2.push_back(cloneI2);
					}

				}

				if (&I == movePoints.front()) {
					Instruction* inst = movePoints.front();
					for (auto it : listI1) {
						it->moveBefore(movePoints.front());
					}
					listI1.clear();

					for (auto it2 : listI2) {
						it2->moveBefore(movePoints.front());
					}
					listI2.clear();

#ifdef DEBUG_INST_MOVING
					if (flag) {
						errs() << "moved before " << *movePoints.front() << "\n";
						errs() << "now bb: " << bb << "\n";
					}
#endif
					movePoints.pop();

				}
			}


			// Move all sync logic to before the branch
			if (!TMR || ReportErrorsFlag) {
				// If block has been split
				if (syncCheckMap.find(&bb) != syncCheckMap.end()) {

					// Get instruction that the block was split on
					Instruction* cmpInst = syncCheckMap[&bb];
					assert(cmpInst && "Block split and the cmpInst stuck around");
					cmpInst->moveBefore(cmpInst->getParent()->getTerminator());

					// Move logic before it
					if (syncHelperMap.find(&bb) != syncHelperMap.end()) {
						for (auto I : syncHelperMap[&bb]) {
							assert(I && "Moving valid instructions\n");
							I->moveBefore(cmpInst);
						}
					}

					// if there are SIMD instructions, need to move the special compare operators
					if (simdMap.find(cmpInst) != simdMap.end()) {
						std::get<0>(simdMap[cmpInst])->moveBefore(cmpInst->getParent()->getTerminator());
						std::get<1>(simdMap[cmpInst])->moveBefore(cmpInst->getParent()->getTerminator());
						std::get<2>(simdMap[cmpInst])->moveBefore(cmpInst->getParent()->getTerminator());
					}
				}
			}

#ifdef DEBUG_INST_MOVING
			if (flag) {
				flag = 0;
			}
#endif
		}
	}
}


/*
 * Gets or creates a global variable.
 */
GlobalVariable* dataflowProtection::createGlobalVariable(Module& M,
		std::string name, unsigned int byteSz)
{
	StringRef srName = StringRef(name);
	// see if it already exists
	GlobalVariable* newGV = M.getGlobalVariable(srName);
	if (newGV) {
		return newGV;
	}

	// Get a type of the right size
	Type* newGVtype = IntegerType::get(M.getContext(), byteSz * 8);
	// Insert as constant first
	Constant* newConstGV = M.getOrInsertGlobal(srName, newGVtype);
	// Cast to correct type
	newGV = cast<GlobalVariable>(newConstGV);

	// Set the properties
	newGV->setConstant(false);
	newGV->setInitializer(ConstantInt::getNullValue(newGVtype));
	newGV->setUnnamedAddr(GlobalValue::UnnamedAddr());
	newGV->setAlignment(byteSz);

	return newGV;
}


//----------------------------------------------------------------------------//
// Run-time initialization of globals
//----------------------------------------------------------------------------//
// Find the total size in bytes of a 1+ dimension array
int dataflowProtection::getArrayTypeSize(Module & M, ArrayType * arrayType) {
	Type * containedType = arrayType->getContainedType(0);

	if (ArrayType * containedArrayType = dyn_cast<ArrayType>(containedType)) {
		return arrayType->getNumElements() * getArrayTypeSize(M, containedArrayType);
	} else {
		DataLayout dataLayout(&M);
		return arrayType->getNumElements() * dataLayout.getTypeAllocSize(containedType);
	}

}

// Find the element bit width of a 1+ dimension array
int dataflowProtection::getArrayTypeElementBitWidth(Module & M, ArrayType * arrayType) {
	Type * containedType = arrayType->getContainedType(0);

	if (ArrayType * containedArrayType = dyn_cast<ArrayType>(containedType)) {
		return getArrayTypeElementBitWidth(M, containedArrayType);
	} else {
		DataLayout dataLayout(&M);
		return dataLayout.getTypeAllocSizeInBits(containedType);
	}

}

void dataflowProtection::recursivelyVisitCalls(Module& M, Function* F, std::set<Function*> &functionList) {
	// If we've already deleted this function from the list
	if (functionList.find(F) == functionList.end())
		return;

	functionList.erase(F);

	for (auto & bb : *F) {
		for (auto & I : bb) {
			if (CallInst* CI = dyn_cast<CallInst>(&I)) {
				recursivelyVisitCalls(M,CI->getCalledFunction(),functionList);
			}
		}
	}

}


//----------------------------------------------------------------------------//
// Miscellaneous
//----------------------------------------------------------------------------//
// visit all uses of an instruction and see if they are also instructions to add to clone list
void dataflowProtection::walkInstructionUses(Instruction* I, bool xMR) {

	// add it to clone or skip list, depending on annotation, passed through argument xMR
	std::set<Instruction*> * addSet;
	if (xMR) {
		addSet = &instsToCloneAnno;
	} else {
		addSet = &instsToSkip;
	}

	for (auto U : I->users()) {
		if (auto instUse = dyn_cast<Instruction>(U)) {
			CallInst* CI = dyn_cast<CallInst>(instUse);
			StoreInst* SI = dyn_cast<StoreInst>(instUse);
			PHINode* phiInst = dyn_cast<PHINode>(instUse);

			// should we add it to the list?
			if (phiInst) {
				;
			} else if (CI) {
				// skip all call instructions for now
				;
			} else if (TerminatorInst* TI = dyn_cast<TerminatorInst>(instUse)) {
				// this should become a syncpoint
				//  really? needs more testing
//				if (xMR) syncPoints.push_back(instUse);
			} else if (SI && (noMemReplicationFlag) ) {
				// don't replicate store instructions if flags
				// also, this will become a syncpoint
//				if (xMR) syncPoints.push_back(instUse);
			} else {
				// Check if any of the operands will be cloned
				bool safeToInsert = true;
				for (unsigned opNum = 0; opNum < I->getNumOperands(); opNum++) {
					auto op = I->getOperand(opNum);
					if ( (!xMR) && willBeCloned(op)) {
						// If they are, don't skip this instruction
						safeToInsert = false;
						break;
					}
				}
				if (safeToInsert) {
					addSet->insert(instUse);
					// errs() << *instUse << "\n";
				} else {
					// if not safe, don't bother following this one
					continue;
				}
			}

			// should we visit its uses?
			//  as long as it has more than 1 uses
			if ( (instUse->getNumUses() > 0) && !phiInst) {
				// recursive call
				walkInstructionUses(instUse, xMR);
			}

		}
	}
}


/*
 * Helper function which splits string on delimiter
 * https://www.fluentcpp.com/2017/04/21/how-to-split-a-string-in-c/
 * Modifed to return ints
 */
std::vector<int> splitOnDelim(const std::string& s, char delimiter) {
	std::vector<int> tokens;
	std::string token;
	std::istringstream tokenStream(s);
	while (std::getline(tokenStream, token, delimiter)) {
		tokens.push_back(std::stoi(token));
	}
	return tokens;
}


/*
 * Some function calls must be handled with specific attributes.
 * This is where we make those modifications and registrations.
 */
void dataflowProtection::updateFnWrappers(Module& M) {
	std::string wrapperFnEnding = "_COAST_WRAPPER";
	std::string cloneAfterName = "_CLONE_AFTER_CALL_";
	// have to create a map and edit afterwards; editing users while iterating over them is a bad idea
	std::map<Function*, Function*> wrapperMap;
	std::set<Function*> wrapperFns;
	std::map<Function*, std::vector<int> > tempCloneAfterCallArgMap;

	// update fn replication wrappers
	for (auto &fn : M) {
		StringRef fnName = fn.getName();
		// this should end with wrapperFnEnding
		if (fnName.endswith(wrapperFnEnding)) {
			wrapperFns.insert(&fn);

			// find the matching function name
			StringRef normalFnName = fnName.substr(0, fnName.size() - wrapperFnEnding.size());
			Constant* fnC = M.getOrInsertFunction(normalFnName, fn.getFunctionType());
			if (!fnC) {
				errs() << "Matching function call to '" << normalFnName << "' doesn't exist!\n";
				exit(-1);
			}
			else {
				if (verboseFlag)
					errs() << info_string << " Found wrapper match: '" << normalFnName << "'\n";
			}

			Function* normalFn = dyn_cast<Function>(fnC);
			wrapperMap[&fn] = normalFn;

		}
		else if (fnName.contains(cloneAfterName)) {
			wrapperFns.insert(&fn);

			// extract the name
			// where is the expected string?
			size_t firstCharIdx = fnName.find(cloneAfterName);
			StringRef normalFnName = fnName.substr(0, firstCharIdx);

			// extract the argument numbers
			std::string argNumStr = fnName.substr(
					firstCharIdx + cloneAfterName.size(), fnName.size()).str();
			std::vector<int> argNums = splitOnDelim(argNumStr, '_');
			tempCloneAfterCallArgMap[&fn] = argNums;

			Constant* fnC = M.getOrInsertFunction(normalFnName, fn.getFunctionType());
			if (!fnC) {
				errs() << "Matching function call to '" << normalFnName << "' doesn't exist!\n";
				exit(-1);
			}
			else {
				if (verboseFlag)
					errs() << info_string << " Found wrapper match: '" << normalFnName << "'\n";
			}

			Function* normalFn = dyn_cast<Function>(fnC);
			wrapperMap[&fn] = normalFn;
		}
	}

	for (auto &fn : M) {
		for (auto &bb: fn) {
			for (auto &I : bb) {

				// look for call instructions
				if (CallInst* ci = dyn_cast<CallInst>(&I)) {

					auto op0 = ci->getOperand(0);
					Function* calledF;

					Value* v = ci->getCalledValue();
					calledF = dyn_cast<Function>(v->stripPointerCasts());
					auto found = wrapperMap.find(calledF);

					if (found != wrapperMap.end()) {
//						errs() << "-" << *ci << "\n";

						if (dyn_cast<Function>(v)) {
							ci->setCalledFunction(found->second);
//							errs() << " -" << *ci << "\n";
							// duplicate this call, but only if it's in the list of functions to clone
							if (fnsToClone.find(&fn) != fnsToClone.end()) {
								// see if user has specified certain args to be cloned after call
								auto foundArgClone = tempCloneAfterCallArgMap.find(calledF);
								if (foundArgClone != tempCloneAfterCallArgMap.end()) {
									// These ones shouldn't be replicated
									cloneAfterCallArgMap[ci] = tempCloneAfterCallArgMap[calledF];
								} else {
									instsToCloneAnno.insert(ci);
								}
								wrapperInsts.insert(ci);
							}
						} else if (BitCastOperator* bco = dyn_cast<BitCastOperator>(v)) {
							errs() << err_string << " wrapper function has bad signature, it has been bitcasted in the call, which is not supported.\n";
							errs() << *bco << "\n";
							errs() << *bco->getOperand(0) << "\n";
							errs() << *(v->stripPointerCasts()) << "\n";
							ci->eraseFromParent();
						}
					}
				}
			}
		}
	}

	for (auto fn : wrapperFns) {
		// remove unused wrapper functions
		if (fn->getNumUses() > 0) {
			errs() << "Missed replacing function call for " << fn->getName() << "\n";
			errs() << *(*fn->user_begin()) << "\n";
			assert(false);
		}
		fn->eraseFromParent();
	}
}


// returns a string of random characters of the requested size
// used to name-mangle the DWC error handler block
std::string dataflowProtection::getRandomString(std::size_t len) {
	// init rand
	std::srand(time(0));

	const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	int charLen = sizeof(chars) - 1;
	std::string result = "";

	for (size_t i = 0; i < len; i+=1) {
		result += chars[rand() % charLen];
	}

	return result;
}


// helper function for dumpModule - not yet implemented
std::set<MDNode*> mdnSet;

void createMDSlot(MDNode* N) {
	// add to set
	mdnSet.insert(N);

	// call on all operands
	for (unsigned i = 0, e = N->getNumOperands(); i != e; i++) {
		if (MDNode* op = dyn_cast_or_null<MDNode>(N->getOperand(i))) {
			createMDSlot(op);
		}
	}
}

// helper function for dumpModule - not yet implemented
void getAllMDNFunc(Function& F) {
	SmallVector< std::pair<unsigned, MDNode*>, 4 > MDForInst;

	// iterate over basic blocks in function
	for (auto BB = F.begin(), E = F.end(); BB != E; BB++) {
		// iterate over instructions in basic block
		for (auto I = BB->begin(), E2 = BB->end(); I != E2; I++) {

			/*// first get metadata from intrinsic functions
			if (CallInst* CI = dyn_cast<CallInst>(I)) {
				if (Function* calledF = CI->getCalledFunction()) {
					if (F.hasName() && F.getName().startswith("llvm.")) {
						for (unsigned i = 0, e = I->getNumOperands(); i != e; i++) {
							auto op = I->getOperand(i);
							if (op) {
								if (MDNode* N = dyn_cast<MDNode>(op)) {
									createMDSlot(N);
								}
							}
//							if (MDNode* N = dyn_cast_or_null<MDNode>(I->getOperand(i))) {
//								createMDSlot(N);
//							}
						}
					}
				}
			}
*/
			// then look at instructions normally
			I->getAllMetadata(MDForInst);
			for (unsigned i = 0, e = MDForInst.size(); i != e; i++) {
				createMDSlot(MDForInst[i].second);
			}
			MDForInst.clear();
		}
	}
}

/*
 * If -dumpModule is passed in, then print the entire module out
 * This is helpful when the pass crashes on cleanup
 * It is in a format that can be pasted into an *.ll file and run
 */
void dataflowProtection::dumpModule(Module& M) {
	if (!dumpModuleFlag)
		return;

	for (GlobalVariable& g : M.getGlobalList()) {
		errs() << g << "\n";
	}
	errs() << "\n";
	for (auto &f : M) {
		errs() << f << "\n";
	}

	// print all the debug metadata
	for (auto md = M.named_metadata_begin(); md != M.named_metadata_end(); md++) {
		md->print(errs());
	}
	for (auto n : mdnSet) {
		errs() << *n << "\n";
	}
	errs() << "\n";
}
