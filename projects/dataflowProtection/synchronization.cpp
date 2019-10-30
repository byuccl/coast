// This file holds all of the logic relevant to synchronization points, error functions, and voting

#include "dataflowProtection.h"

#include <deque>

#include <llvm/IR/Module.h>
#include "llvm/Support/CommandLine.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/IRBuilder.h>

// Command line options
extern cl::opt<bool> OriginalReportErrorsFlag;
extern cl::opt<bool> ReportErrorsFlag;
extern cl::opt<bool> noLoadSyncFlag;
extern cl::opt<bool> noStoreDataSyncFlag;
extern cl::opt<bool> noStoreAddrSyncFlag;
extern cl::opt<bool> noMemReplicationFlag;
extern cl::opt<bool> storeDataSyncFlag;
extern cl::opt<bool> verboseFlag;
extern cl::opt<bool> noMainFlag;

using namespace llvm;

// commonly used strings
std::string fault_function_name = "FAULT_DETECTED_DWC";
std::string tmr_vote_inst_name = "vote";
std::string tmr_global_count_name = "TMR_ERROR_CNT";

//----------------------------------------------------------------------------//
// Obtain synchronization points
//----------------------------------------------------------------------------//
void dataflowProtection::populateSyncPoints(Module& M) {
	for (auto F : fnsToClone) {
		if (F->getName().startswith("FAULT_DETECTED")) //Don't sync in err handler
			continue;

		for (auto & bb : *F) {
			for (auto & I : bb) {

				//Sync before branches
				if (I.isTerminator()) {
					//skip syncing on unreachable instructions
					if (UnreachableInst* unreach = dyn_cast<UnreachableInst>(&I))
						continue;
					syncPoints.push_back(&I);
				}

				//Sync at external function calls - they're only declared, not defined
				if (CallInst* CI = dyn_cast<CallInst>(&I)) {
					//Calling linkage on inline assembly causes errors, make this check first
					if(CI->isInlineAsm())
						continue;

					//Skip any thing that doesn't have a called function and print warning
					if(isIndirectFunctionCall(CI, "populateSyncPoints")){
						continue;
					}

					Function* calledF = CI->getCalledFunction();

					//skip debug function calls
					if (calledF->hasName()) {
						if (calledF->getName().startswith_lower("llvm.dbg.") ||
								calledF->getName().startswith_lower("llvm.lifetime."))
						{
							continue;
						}
					}

					// skip functions that are marked as "wrapper" functions
					//  see updateFnWrappers()
					if (wrapperInsts.find(CI) != wrapperInsts.end()) {
						continue;
					}

					if (calledF->hasExternalLinkage() && calledF->isDeclaration()) {
						syncPoints.push_back(&I);
//						errs() << "Adding " << CI->getCalledFunction()->getName() << " to syncpoints\n";
					}
				}

				//Sync data on all stores unless explicitly instructed not to
				if (StoreInst* SI = dyn_cast<StoreInst>(&I)) {
					//Don't sync pointers, they will be different
					if (SI->getOperand(0)->getType()->isPointerTy()) {
						continue;
					} else if(dyn_cast<PtrToIntInst>(SI->getOperand(0))) {
						//Likewise, don't check casted pointers
						continue;
					}
					// if this is not a cloned instruction
					else if ( ( (getClone(&I).first == &I) || (getClone(&I).second == &I) ) &&
							   !noMemReplicationFlag ) {
						continue;
					}
					//by default, we don't sync on stores, unless specifically told to
					//have to sync on stores, data and addr, if no mem replication
					else if (!noMemReplicationFlag && !storeDataSyncFlag) {
						continue;
					}
					//otherwise, go ahead and add it to the list of sync-points
					else {
						syncPoints.push_back(&I);
					}
				}

				//Sync offsets of GEPs
				if(GetElementPtrInst* GEP = dyn_cast<GetElementPtrInst>(&I)){
					if(willBeCloned(GEP) || isCloned(GEP)){
						syncPoints.push_back(&I);
					}
				}

			}
		}
	}
}

//----------------------------------------------------------------------------//
// Insert synchronization logic
//----------------------------------------------------------------------------//
void dataflowProtection::processSyncPoints(Module & M, int numClones) {
	if (syncPoints.size() == 0)
		return;

	GlobalVariable* TMRErrorDetected = M.getGlobalVariable(tmr_global_count_name);

	//Look for the variable first. If it doesn't exist, make one
	//If it is unneeded, it is erased at the end of this function
	if (!TMRErrorDetected) {
		if(TMR && ReportErrorsFlag && verboseFlag)
			errs() << "Could not find " << tmr_global_count_name << " flag! Creating one...\n";

		TMRErrorDetected = cast<GlobalVariable>(M.getOrInsertGlobal(tmr_global_count_name,
														IntegerType::getInt32Ty(M.getContext())));
		// if there is no main in this module, keep this global as extern
		if (noMainFlag) {
			TMRErrorDetected->setExternallyInitialized(true);
			TMRErrorDetected->setLinkage(GlobalValue::LinkageTypes::ExternalLinkage);
		} else {
			// otherwise, will be initialized to 0
			TMRErrorDetected->setConstant(false);
			TMRErrorDetected->setInitializer(ConstantInt::getNullValue(IntegerType::getInt32Ty(M.getContext())));
			TMRErrorDetected->setUnnamedAddr( GlobalValue::UnnamedAddr() );
			TMRErrorDetected->setAlignment(4);
		}
		globalsToSkip.insert(TMRErrorDetected);
	}
	assert(TMRErrorDetected != nullptr);

	// Some of the syncpoints may be invalidated during this process, but we can't remove them
	//  from this list we're iterating over.  Make a list to delete them later.
	std::vector<Instruction*> deleteItLater;

	for (auto I : syncPoints) {

		assert(I && "How did a null pointer get into syncpoints?");

		if (StoreInst* currStoreInst = dyn_cast<StoreInst>(I)) {
			if(!noStoreDataSyncFlag){
				syncStoreInst(currStoreInst, TMRErrorDetected);
			}
		} else if (CallInst* currCallInst = dyn_cast<CallInst>(I)) {
			processCallSync(currCallInst, TMRErrorDetected);

		} else if(TerminatorInst* currTerminator = dyn_cast<TerminatorInst>(I)) { //is a terminator
			syncTerminator(currTerminator, TMRErrorDetected);

		} else if(GetElementPtrInst* currGEP = dyn_cast<GetElementPtrInst>(I)) {

			// default is DON'T sync on addresses, can only do that when there is no second
			//  copy in memory
			if (!noMemReplicationFlag) {
				continue;
			}

			if (noLoadSyncFlag) {
				//Don't sync address of loads
				if ( dyn_cast<LoadInst>(currGEP->user_back()) ) {
					continue;
				} else if (GetElementPtrInst* nextGEP = dyn_cast<GetElementPtrInst>(currGEP->user_back())) {
					//Don't want to sync GEPs that feed GEPs of load inst
					if (nextGEP->getNumUses() == 1) {
						if ( dyn_cast<LoadInst>(nextGEP->user_back()) ) {
							continue;
						}
					}
				}
			}

			if (noStoreAddrSyncFlag) {
				//Don't address of stores
				if ( dyn_cast<StoreInst>(currGEP->user_back()) ) {
					continue;
				} else if (GetElementPtrInst* nextGEP = dyn_cast<GetElementPtrInst>(currGEP->user_back())) {
					//Don't want to sync GEPs that feed GEPs of store inst
					if (nextGEP->getNumUses() == 1) {
						if ( dyn_cast<StoreInst>(nextGEP->user_back()) ) {
							continue;
						}
					}
				}
			}

			//else there is noMemReplication
			if (syncGEP(currGEP, TMRErrorDetected)) {
				deleteItLater.push_back(I);
			}
		} else {
			// more detailed information about the failure
			if (BasicBlock* wrongBB = dyn_cast<BasicBlock>(I)) {
				errs() << "Something is wrong here...\n";
				errs() << wrongBB << ": " << *wrongBB << "\n";
				errs() << "\tin " << wrongBB->getParent()->getName() << "\n";
				errs() << "\tPrev node = " << wrongBB->getPrevNode()->getName() << "\n";
				errs() << *wrongBB->getParent() << '\n';
			} else {
				errs() << I << "\n";
				errs() << *I << "\n";
				errs() << "\tin " << I->getParent()->getName() << "\n";
			}
			assert(false && "Synchronizing at an unrecognized instruction type");
		}

	}

	// delete the now-invalid pointers
	for (auto it : deleteItLater) {
		syncPoints.erase(std::find(syncPoints.begin(), syncPoints.end(), it));
	}

	// we found some new ones while doing stuff above
	// these will be used for moving sync instructions around
	for (auto ns : newSyncPoints) {
		syncPoints.push_back(ns);
	}

	if(!TMR && TMRErrorDetected->getNumUses() < 1)
		TMRErrorDetected->eraseFromParent();
}

/*
 * Returns true if it invalidates the pointer to currGEP.  The calling function is responsible
 *  for handling this.
 */
bool dataflowProtection::syncGEP(GetElementPtrInst* currGEP, GlobalVariable* TMRErrorDetected) {
	//2 forms of GEP with different number of arguments
	//Offset is the last argument
	std::vector<Instruction*> syncInsts;
	Value* orig = currGEP->getOperand(currGEP->getNumOperands()-1);

	if (!isCloned(currGEP)) {
	/* Don't remove items from a vector we're currently iterating over.
	 * Calling function is responsible for this.
	 */
		return true;
	}

	if (!isCloned(orig)) {
		startOfSyncLogic[currGEP] = currGEP;
		return false;
	}

	Value* clone1 = getClone(orig).first;
	assert(clone1 && "Cloned value exists");
	Instruction::OtherOps cmp_op;
	CmpInst::Predicate cmp_eq;
	if (orig->getType()->isFPOrFPVectorTy()) {
		cmp_op = Instruction::OtherOps::FCmp;
		cmp_eq = CmpInst::FCMP_UEQ; //Should this be OEQ instead?
	} else {
		cmp_op = Instruction::OtherOps::ICmp;
		cmp_eq = CmpInst::ICMP_EQ;
	}

	Instruction* cmp = CmpInst::Create(cmp_op, cmp_eq, orig, clone1, "cmp", currGEP);
	cmp->removeFromParent();
	cmp->insertBefore(currGEP);

	startOfSyncLogic[currGEP] = cmp;

	if (TMR) {
		Value* clone2 = getClone(orig).second;
		assert(clone2 && "Clone exists when syncing at store");
		SelectInst* sel = SelectInst::Create(cmp,orig,clone2,tmr_vote_inst_name,currGEP);

		syncInsts.push_back(cmp);
		syncInsts.push_back(sel);

		GetElementPtrInst* currGEPClone1 = dyn_cast<GetElementPtrInst>(getClone(currGEP).first);
		GetElementPtrInst* currGEPClone2 = dyn_cast<GetElementPtrInst>(getClone(currGEP).second);

		currGEP->setOperand(currGEP->getNumOperands()-1,sel);
		currGEPClone1->setOperand(currGEPClone1->getNumOperands()-1,sel);
		currGEPClone2->setOperand(currGEPClone2->getNumOperands()-1,sel);

		//Too many cases to account for this, so assertion is removed for now
//		if(!isa<PHINode>(orig)){
//			assert(numUses == 2 &&  "Instruction only used in GEP synchronization");
//		}

		insertTMRCorrectionCount(cmp, TMRErrorDetected);
	} else {		//DWC
		Function* currFn = currGEP->getParent()->getParent();
		splitBlocks(cmp, errBlockMap[currFn]);
		//fix invalidated pointer - see note in processCallSync()
		startOfSyncLogic[currGEP] = currGEP;
	}

	return false;
}

void dataflowProtection::syncStoreInst(StoreInst* currStoreInst, GlobalVariable* TMRErrorDetected) {
	//Keep track of the inserted instructions
	std::vector<Instruction*> syncInsts;

	// Sync the value of the store instruction
	// If memory is cloned we shouldn't sync the address, as they will be different
	Value* orig = currStoreInst->getOperand(0);

	// No need to sync if value is not cloned
	//Additionally, makes sure we don't sync on copies, unless we are forced to sync here
	if (!isCloned(orig) && !noMemReplicationFlag) {
		return;
	}
	else if (noMemReplicationFlag) {
		//Make sure we don't sync on single return points when memory isn't duplicated
		if (!dyn_cast<StoreInst>(orig) && !isCloned(orig)) {
			return;
		}
	}

	Value* clone1 = getClone(orig).first;
	assert(clone1 && "Cloned value exists");

	//Disabling synchronization on constant store
	if (dyn_cast<ConstantInt>(orig)) {
		return;
	}

	Instruction::OtherOps cmp_op;
	CmpInst::Predicate cmp_eq;
	if (currStoreInst->getOperand(0)->getType()->isFPOrFPVectorTy()) {
		cmp_op = Instruction::OtherOps::FCmp;
		cmp_eq = CmpInst::FCMP_UEQ; //Should this be OEQ instead?
	} else {
		cmp_op = Instruction::OtherOps::ICmp;
		cmp_eq = CmpInst::ICMP_EQ;
	}

	Instruction* cmp = CmpInst::Create(cmp_op, cmp_eq, orig, clone1, "cmp", currStoreInst);
	cmp->removeFromParent();
	cmp->insertBefore(currStoreInst);

	syncInsts.push_back(cmp);
	startOfSyncLogic[currStoreInst] = cmp;

	if (TMR) {
		Value* clone2 = getClone(orig).second;
		assert(clone2 && "Clone exists when syncing at store");
		SelectInst* sel = SelectInst::Create(cmp,orig,clone2,tmr_vote_inst_name,currStoreInst);
		syncInsts.push_back(sel);

		assert(getClone(currStoreInst).first && "Store instruction has a clone");

		currStoreInst->setOperand(0,sel);
		dyn_cast<StoreInst>(getClone(currStoreInst).first)->setOperand(0,sel);
		dyn_cast<StoreInst>(getClone(currStoreInst).second)->setOperand(0,sel);

		//Make sure that the voted value is propagated downstream
		if (orig->getNumUses() != 2) {
			if (Instruction* origInst = dyn_cast<Instruction>(orig)) {
				DominatorTree DT = DominatorTree(*origInst->getParent()->getParent());
				for (auto u : origInst->users()) {
					//Find any and all instructions that were not updated
					if (std::find(syncInsts.begin(),syncInsts.end(),u) == syncInsts.end()) {
						//Get all operands that should be updated
						for (unsigned int opNum=0; opNum < u->getNumOperands(); opNum++) {
							//Update if and only if the instruction is dominated by sel
							if (u->getOperand(opNum) == orig && DT.dominates(sel,dyn_cast<Instruction>(u))) {
								u->setOperand(opNum,sel);
								if (isCloned(u)) {
									dyn_cast<Instruction>(getClone(u).first)->setOperand(opNum,sel);
									dyn_cast<Instruction>(getClone(u).second)->setOperand(opNum,sel);
								}
							}
						}
					}
				}
			}
		}

		insertTMRCorrectionCount(cmp, TMRErrorDetected);
	} else {		//DWC
		Function* currFn = currStoreInst->getParent()->getParent();
		splitBlocks(cmp, errBlockMap[currFn]);
		//fix invalidated pointer - see note in processCallSync()
		startOfSyncLogic[currStoreInst] = currStoreInst;
	}
}

void dataflowProtection::processCallSync(CallInst* currCallInst, GlobalVariable* TMRErrorDetected) {
	//Get a list of the non-constant/GEP arguments, no purpose in checking them
	//Don't compare pointer values either
	std::vector<Instruction*> syncInsts;

	/* We need to check if any of the parameters in the call instruction are actually arguments
	 * passed into the function that this CallInst is in.  We need to make a list of the arguments
	 * to compare easier later.
	 */
	Function* enclosingFunction = currCallInst->getParent()->getParent();
	assert(enclosingFunction && "function exists");
	std::list<Value*> argVals;
	for (auto arg = enclosingFunction->arg_begin(); arg != enclosingFunction->arg_end(); arg++) {
//		errs() << "  " << *arg << " (@ " << arg << ")\n";
		argVals.push_back(dyn_cast<Value>(arg));
	}

	std::deque<Value*> cloneableOperandsList;
	for (unsigned int it = 0; it < currCallInst->getNumArgOperands(); it++) {
		if (isa<Constant>(currCallInst->getArgOperand(it))
				|| isa<GetElementPtrInst>(currCallInst->getArgOperand(it)))
			continue;
		if(isa<PointerType>(currCallInst->getArgOperand(it)->getType()))
			continue;
		cloneableOperandsList.push_back(currCallInst->getArgOperand(it));
	}
	if (cloneableOperandsList.size() == 0) {
		startOfSyncLogic[currCallInst] = currCallInst;
		return;
	}

	//We now have a list of (an unknown number of) operands, insert comparisons for all of them
	std::deque<Value*> cmpInstList;
	std::vector<Instruction*> syncHelperList;
	BasicBlock* currBB = currCallInst->getParent();
	syncHelperMap[currBB] = syncHelperList;
	bool firstIteration = true;
	for (unsigned int i = 0; i < cloneableOperandsList.size(); i++) {

		Value* orig = cloneableOperandsList[i];

		if (!isCloned(orig)) {
			continue;
		}
		ValuePair clones = getClone(orig);

		//Make sure we're inserting the right type of comparison
		Instruction::OtherOps cmp_op;
		CmpInst::Predicate cmp_eq;
		if (orig->getType()->isFPOrFPVectorTy()) {
			cmp_op = Instruction::OtherOps::FCmp;
			cmp_eq = CmpInst::FCMP_UEQ; //Should this be OEQ instead?
		} else {
			cmp_op = Instruction::OtherOps::ICmp;
			cmp_eq = CmpInst::ICMP_EQ;
		}

		Instruction* cmp = CmpInst::Create(cmp_op, cmp_eq, orig, clones.first,"cmp", currCallInst);
		cmp->removeFromParent();
		cmp->insertBefore(currCallInst);
		if(firstIteration){
			startOfSyncLogic[currCallInst] = cmp;
			firstIteration = false;
		}

		syncInsts.push_back(cmp);

		if (TMR) {
			SelectInst* sel = SelectInst::Create(cmp, orig, clones.second, tmr_vote_inst_name, currCallInst);
			syncInsts.push_back(sel);

			currCallInst->replaceUsesOfWith(orig,sel);
			dyn_cast<CallInst>(getClone(currCallInst).first)->replaceUsesOfWith(clones.first, sel);
			dyn_cast<CallInst>(getClone(currCallInst).second)->replaceUsesOfWith(clones.second, sel);

			//If something fails this assertion, it means that it is used after the call synchronization
			//Might have to change it later in case we find a case where this is ok
			//But extensive tests haven't found a case where this is necessary
			// update: The condition does NOT hold if the operand is one that is passed in by an argument,
			// and it hasn't been alloca'd; then every reference is to the original argument.
			int useCount = orig->getNumUses();
			if (useCount != 2) {
				if (Instruction* origInst = dyn_cast<Instruction>(orig)) {
					DominatorTree DT = DominatorTree(*origInst->getParent()->getParent());
					std::vector<Instruction*> uses;
					for (auto uu : orig->users()) {
						uses.push_back(dyn_cast<Instruction>(uu));
					}
					for (auto u : uses) {
						//Find any and all instructions that were not updated
						if (std::find(syncInsts.begin(),syncInsts.end(),u) == syncInsts.end()) {
							if (!DT.dominates(sel, dyn_cast<Instruction>(u))) {
								useCount--;
							//Get all operands that should be updated
							} else {
								for (unsigned int opNum=0; opNum < u->getNumOperands(); opNum++) {
									//Update if and only if the instruction is dominated by sel
									if (u->getOperand(opNum) == orig && DT.dominates(sel, dyn_cast<Instruction>(u))) {
										u->setOperand(opNum, sel);
										if (isCloned(u)) {
											dyn_cast<Instruction>(getClone(u).first)->setOperand(opNum, sel);
											dyn_cast<Instruction>(getClone(u).second)->setOperand(opNum, sel);
										}
										useCount--;
									}
								}
							}
						}
					}
				}
			}

			// if it's not an argument, then we can assert that there can only be 2 uses
			if (std::find(argVals.begin(), argVals.end(), orig) == argVals.end()) {
				if (useCount != 2) {
					errs() << *currCallInst << "\n";
					errs() << *orig << "\n";
				}
				assert(useCount==2 && "Instruction only used in call sync");
				// TODO: examine what could cause this to fail
			}
			insertTMRCorrectionCount(cmp, TMRErrorDetected);
		} else {		//DWC
			cmpInstList.push_back(cmp);
			syncHelperMap[currBB].push_back(cmp);
		}
	}

	if (!TMR) {
		if (cmpInstList.size() == 0) {
			return;
		}

		//Reduce the comparisons to a single instruction
		while (cmpInstList.size() > 1) {
			Value* cmp0 = cmpInstList[0];
			Value* cmp1 = cmpInstList[1];

			Instruction* cmpInst = BinaryOperator::Create(Instruction::Or, cmp0, cmp1, "or", currCallInst);
			cmpInstList.push_back(cmpInst);
			syncHelperMap[currBB].push_back(cmpInst);

			cmpInstList.erase(cmpInstList.begin());
			cmpInstList.erase(cmpInstList.begin());
		}

		Value* tmpCmp = cmpInstList[0];
		Instruction* reducedCompare = dyn_cast<Instruction>(tmpCmp);
		assert(	reducedCompare && "Call sync compare reduced to a single instruction");
		syncHelperMap[currBB].pop_back();
		splitBlocks(reducedCompare,	errBlockMap[currCallInst->getParent()->getParent()]);
		/*
		 * NOTE:
		 * splitting the blocks invalidates the previously set value in the map
		 * startOfSyncLogic, set to be the instruction that compares the operands of the
		 * function called by the currCallInst. That instruction is deleted from its
		 * parent BasicBlock, and now the map points to some memory address that we don't
		 * know what's there. Need to update the map so it just points to the CallInst
		 * itself instead.
		 */
		startOfSyncLogic[currCallInst] = currCallInst;
	}
}

void dataflowProtection::syncTerminator(TerminatorInst* currTerminator, GlobalVariable* TMRErrorDetected) {
	assert(currTerminator);

	//Only sync if there are arguments to duplicate
	if (isa<BranchInst>(currTerminator)) {
		if (currTerminator->getNumSuccessors() < 2){ //1 successor, or none = unconditional
			startOfSyncLogic[currTerminator] = currTerminator;
			return;
		}
	} else if (isa<ResumeInst>(currTerminator)) {
		//Resume is only for exception handlers, sync always
	} else if (isa<InvokeInst>(currTerminator)) {
		//anything special need to go here?
	} else if (isa<ReturnInst>(currTerminator)) {
		if (currTerminator->getNumOperands() == 0){ //Returning nothing
			startOfSyncLogic[currTerminator] = currTerminator;
			return;
		}
	} else if (isa<SwitchInst>(currTerminator)) {
		if (currTerminator->getNumSuccessors() == 1){ // Don't check unconditional branches
			startOfSyncLogic[currTerminator] = currTerminator;
			return;
		}
	} else { //indirectbr, invoke, catchswitch, catchret, unreachable, cleanupret
		//Do nothing, the other branch types don't have arguments to clone
		startOfSyncLogic[currTerminator] = currTerminator;
		return;
	}

	if (TMR) {
		std::vector<Instruction*> syncInsts;
		Value* op = currTerminator->getOperand(0);

		if (!isCloned(op))
			return;

		Instruction* clone1 = dyn_cast<Instruction>(getClone(op).first);
		Instruction* clone2 = dyn_cast<Instruction>(getClone(op).second);
		assert(clone1 && clone2 && "Instruction has clones");
		// TODO: examine what could cause this assertion to fail

		//Make sure we're inserting the right type of comparison
		Instruction::OtherOps cmp_op;
		CmpInst::Predicate cmp_eq;
		Type* opType = op->getType();

		//if it's a pointer type, is it ever safe to compare return values?
		// could have been allocated with malloc()
		// you would have to dereference the pointer to compare the insides of it
		if (opType->isPointerTy()) {
			if (verboseFlag) {
				errs() << warn_string << " skipping synchronizing on return instruction of pointer type:\n";
				errs() << " in " << currTerminator->getParent()->getName() << "\n";
			}
			startOfSyncLogic[currTerminator] = currTerminator;
			return;
		}
		//seems to be a problem with the "this" pointer

		if (opType->isFPOrFPVectorTy()) {
			cmp_op = Instruction::OtherOps::FCmp;
			cmp_eq = CmpInst::FCMP_UEQ; //Should this be OEQ instead?
		} else if(opType->isIntOrIntVectorTy()) {
			cmp_op = Instruction::OtherOps::ICmp;
			cmp_eq = CmpInst::ICMP_EQ;
		} else if (opType->isStructTy()) {

//			cmp_op = Instruction::OtherOps::FCmp;
			//get the size of the struct
			StructType* sType = dyn_cast<StructType>(opType);
			uint64_t nTypes = sType->getStructNumElements();
			//load each of the inner values and get their types
			//need to get the actual struct value to operate on
			Value* op0 = currTerminator->getOperand(0);
			//NOTE: will there ever be more than one operand to worry about?
			Value* op1 = cloneMap[op0].first;
			Value* op2 = cloneMap[op0].second;
//			errs() << *op << "\n" << *op2 << "\n" << *op3 << "\n";
			unsigned arr[] = {0};

			//we'll need these later
			SelectInst* eSel[nTypes];
			int firstTime = 1;

			for (int i = 0; i < nTypes; i+=1) {
				//type to compare
				auto eType = sType->getStructElementType(i);
//				errs() << " Type " << i << ": " << *eType << "\n";

				//index to extract
				arr[0] = i;
				auto extractIdx = ArrayRef<unsigned>(arr);

				//names of instructions
				std::string extractName    = "getToCompare." + std::to_string(i);
				std::string extractNameDWC = extractName + ".DWC";
				std::string extractNameTMR = extractName + ".TMR";
				std::string cmpName = "cmpElement." + std::to_string(i);
				std::string selName = "selElement." + std::to_string(i);

				//create the ExtractValueInst's
				ExtractValueInst* extract0 = ExtractValueInst::Create(op0, extractIdx, extractName);
				ExtractValueInst* extract1 = ExtractValueInst::Create(op1, extractIdx, extractNameDWC);
				ExtractValueInst* extract2 = ExtractValueInst::Create(op2, extractIdx, extractNameTMR);

				//create the compare instructions
				if (eType->isFPOrFPVectorTy()) {
					cmp_op = Instruction::OtherOps::FCmp;
					cmp_eq = CmpInst::FCMP_UEQ;
					//TODO: what's with the "ordered" and "unordered" stuff?
				} else if (eType->isIntOrIntVectorTy()) {
					cmp_op = Instruction::OtherOps::ICmp;
					cmp_eq = CmpInst::ICMP_EQ;
					//compare equal - returns true (1) if equal
				} else if (eType->isPointerTy()) {
					//we'll have to skip syncing on this value
					//delete the extra instructions that aren't being used
					extract0->deleteValue();
					extract1->deleteValue();
					extract2->deleteValue();
					eSel[i] = nullptr;
					continue;
				} else {
					assert(cmp_op && "valid comparison type assigned");
				}

				//only set the logic point if it's still valid
				if (firstTime) {
					firstTime = 0;
					startOfSyncLogic[currTerminator] = extract0;
				}
				Instruction* eCmp = CmpInst::Create(cmp_op, cmp_eq, extract0, extract1, cmpName);
				eSel[i] = SelectInst::Create(eCmp, extract0, extract2, selName);

				//debug
//				errs() << *extract0 << "\n" << *extract1 << "\n" << *extract2 << "\n";
//				errs() << *eCmp << "\n" << *eSel[i] << "\n";

				//insert the instructions into the basic block
				extract0->insertBefore(currTerminator);
				extract1->insertAfter(extract0);
				extract2->insertAfter(extract1);
				eCmp->insertAfter(extract2);
				eSel[i]->insertAfter(eCmp);
			}

			//we use the results of the SelectInst's to populate the final return value
			// which we will just use the existing first copy of the struct
			for (int i = 0; i < nTypes; i+=1) {
				//only sync if these are non-pointer values
				if (eSel[i] == nullptr) {
					continue;
				}
				//insert the select values into the first copy of the struct
				arr[0] = i;
				auto insertIdx = ArrayRef<unsigned>(arr);
				std::string insertName = "voter.insert." + std::to_string(i);
				auto insVal = InsertValueInst::Create(op0, eSel[i], insertIdx, insertName);
				insVal->insertBefore(currTerminator);
//				errs() << *insVal << "\n";
			}
			//don't even have to change the Terminator!
			//this part is so different from the other kinds that we skip all the rest of the stuff below
			//TODO: what's up the with syncInsts list?
			return;

		} else if (opType->isAggregateType()) {
			errs() << "It's an aggregate type!\n";
		} else {
			errs() << "Unidentified type!\n";
			errs() << *currTerminator << "\n";
//			errs() << *opType << "\n";
//			if (opType->isPointerTy())
//				errs() << "Pointer type!\n";
//			for (auto x = opType->subtype_begin(); x != opType->subtype_end(); x++) {
//				errs() << **x << "\n";
//			}
			assert(false && "Return type not supported!\n");
		}
		if (!cmp_op) {
			errs() << err_string << " " << *opType << "\n";
			errs() << *currTerminator << "\n";
		}
		assert(cmp_op && "return type not supported!");

		Instruction* cmp = CmpInst::Create(cmp_op, cmp_eq, op, clone1, "vcmp", currTerminator);

		startOfSyncLogic[currTerminator] = cmp;

		SelectInst* sel = SelectInst::Create(cmp, op, clone2, tmr_vote_inst_name, currTerminator);

		currTerminator->replaceUsesOfWith(op, sel);

		syncInsts.push_back(cmp);
		syncInsts.push_back(sel);

		// Too many cases to account for each possibility, this is removed
		// assert(numUses == 2 && "Instruction only used in terminator synchronization");

		// This function invalidates the line that assigns "cmp" as the map value for currTerminator,
		//  because the same terminator instruction will no longer exist, if we are inserting
		//  TMR error count instructions.
		insertTMRCorrectionCount(cmp, TMRErrorDetected, true);

	} else {		//DWC
		if (!isCloned(currTerminator->getOperand(0))) {
			return;
		}

		Instruction* clone = dyn_cast<Instruction>(getClone(currTerminator->getOperand(0)).first);
		assert(clone && "Instruction has a clone");

		Instruction::OtherOps cmp_op;
		CmpInst::Predicate cmp_eq;
		auto opType = currTerminator->getOperand(0)->getType();

		//see comments in TMR section about synchronizing on pointer values
		if (opType->isPointerTy()) {
			if (verboseFlag) {
				errs() << warn_string << " skipping synchronizing on return instruction of pointer type:\n";
				errs() << " in " << currTerminator->getParent()->getName() << "\n";
			}
			return;
		}

		if (opType->isFPOrFPVectorTy()) {
			cmp_op = Instruction::OtherOps::FCmp;
			cmp_eq = CmpInst::FCMP_UEQ; //Should this be OEQ instead?
		} else if (opType->isIntOrIntVectorTy()) {
			cmp_op = Instruction::OtherOps::ICmp;
			cmp_eq = CmpInst::ICMP_EQ;
		} else if (opType->isStructTy()) {
			//get the size of the struct
			StructType* sType = dyn_cast<StructType>(opType);
			uint64_t nTypes = sType->getStructNumElements();
			//load each of the inner values and get their types
			Value* op0 = currTerminator->getOperand(0);
			Value* op1 = cloneMap[op0].first;

			//we'll need these later
			unsigned arr[] = {0};
//			CmpInst* eCmp[nTypes];
			std::vector<CmpInst*> eCmp;
			int firstTime = 1;
			Instruction* syncPointLater;

			for (int i = 0; i < nTypes; i+=1) {
				//type to compare
				auto eType = sType->getStructElementType(i);

				//index to extract
				arr[0] = i;
				auto extractIdx = ArrayRef<unsigned>(arr);

				//names of instructions
				std::string extractName    = "getToCompare." + std::to_string(i);
				std::string extractNameDWC = extractName + ".DWC";
				std::string cmpName = "cmpElement." + std::to_string(i);

				//create the ExtractValueInst's
				ExtractValueInst* extract0 = ExtractValueInst::Create(op0, extractIdx, extractName);
				ExtractValueInst* extract1 = ExtractValueInst::Create(op1, extractIdx, extractNameDWC);

				//create the compare instructions
				if (eType->isFPOrFPVectorTy()) {
					cmp_op = Instruction::OtherOps::FCmp;
					cmp_eq = CmpInst::FCMP_UNE;
				} else if (eType->isIntOrIntVectorTy()) {
					cmp_op = Instruction::OtherOps::ICmp;
					cmp_eq = CmpInst::ICMP_NE;
					//compare not equal - returns true (1) if not equal, so expect all to be false (0)
				} else if (eType->isPointerTy()) {
					//we'll have to skip syncing on this value
					//do the extract instructions just disappear if we don't insert them anywhere?
					//nope!
					extract0->deleteValue();
					extract1->deleteValue();
					continue;
				} else {
					errs() << "eType: " << *eType << "\n";
					assert(cmp_op && "valid comparison type assigned");
				}

				//only set the logic point if it's still valid
				if (firstTime) {
					firstTime = 0;
					syncPointLater = extract0;
				}
				eCmp.push_back(CmpInst::Create(cmp_op, cmp_eq, extract0, extract1, cmpName));

				//debug
//				errs() << *extract0 << "\n" << *extract1 << "\n" << *eCmp[i] << "\n";

				//insert the instructions into the basic block
				extract0->insertBefore(currTerminator);
				extract1->insertAfter(extract0);
				eCmp.back()->insertAfter(extract1);
			}

			//this doesn't help with the below anymore, but still a good check
			assert(nTypes > 1);

			Instruction* cmpInst;
			cmp_op = Instruction::OtherOps::ICmp;
			cmp_eq = CmpInst::ICMP_EQ;

			if (eCmp.size() > 2) {
				//final reduce & compare
				//use OR because if any one of them is 1, it will get set to 1 and stay that way
				//there will be at least 2, so OR them together first
				BinaryOperator* acc;
				int i = 0;
				do {
					std::string reduceName = "reduce." + std::to_string(i);
					acc = BinaryOperator::Create(Instruction::BinaryOps::Or, eCmp.at(i), eCmp.at(i+1),
							reduceName, currTerminator);
					i+=1;
//					errs() << *acc << "\n";
				} while (i < eCmp.size()-1);
				//compare against 0
				ConstantInt* compareAgainst =
						ConstantInt::get(dyn_cast<IntegerType>(acc->getType()), 0, false);
				cmpInst = CmpInst::Create(cmp_op, cmp_eq, acc, compareAgainst);
				cmpInst->insertBefore(currTerminator);
//				errs() << *cmpInst << "\n";		errs() << *cmpInst->getParent() << "\n";
			} else if (eCmp.size() == 1) {
				//only one element - then just compare the
				ConstantInt* compareAgainst =
						ConstantInt::get(dyn_cast<IntegerType>(eCmp.at(0)->getType()), 0, false);
				cmpInst = CmpInst::Create(cmp_op, cmp_eq, eCmp.at(0), compareAgainst);
				cmpInst->insertBefore(currTerminator);
			} else {
				//nothing compared because they're all pointers
				//so there's no synchronization necessary (?)
				syncPoints.push_back(currTerminator);
				return;
			}

			//split the block
			Function* currFn = currTerminator->getParent()->getParent();
			Instruction* lookAtLater = cmpInst->getPrevNode();
			assert(lookAtLater);
			splitBlocks(cmpInst, errBlockMap[currFn]);
			/*
			 * things that get invalidated:
			 *   the new terminator of the split block is not in syncpoint list
			 *   any special information about synclogic
			 */
			Instruction* newTerm = lookAtLater->getParent()->getTerminator();
			syncPoints.push_back(newTerm);
			startOfSyncLogic[newTerm] = syncPointLater;
			return;

		} else {
			errs() << "Unidentified type!\n";
			errs() << *currTerminator << "\n";
			assert(false && "Return type not supported!\n");
		}

		Instruction *cmpInst = CmpInst::Create(cmp_op,cmp_eq, currTerminator->getOperand(0),
				clone, "tmp",currTerminator);

		Function* currFn = currTerminator->getParent()->getParent();
		splitBlocks(cmpInst, errBlockMap[currFn]);
	}
}

void dataflowProtection::splitBlocks(Instruction* I, BasicBlock* errBlock) {
	//Split at I, return a pointer to the new error block

	//Create a copy of tmpCmpInst that will reside in the current basic block
	Instruction* newCmpInst = I->clone();
	newCmpInst->setName("syncCheck.");
	newCmpInst->insertBefore(I);

	//Create the continuation of the current basic block
	BasicBlock* originalBlock = I->getParent();
	const Twine& name = originalBlock->getParent()->getName() + ".cont";
	BasicBlock* newBlock = originalBlock->splitBasicBlock(I, name);

	//The compare instruction is copied to the new basicBlock by calling split, so we remove it
	I->eraseFromParent();

	//Delete originalBlock's terminator
	originalBlock->getTerminator()->eraseFromParent();
	//create conditional branch
	//there are some times it will try to branch on a vector value.
	//Instead need to insert additional compare logic. Only necessary with DWC.
	if (!newCmpInst->getType()->isIntegerTy(1) && !TMR) {
		//it is possible that the value being compared is a vector type instead of a basic type

		//need to sign extend the boolean vector
		int numElements = newCmpInst->getType()->getVectorNumElements();
		Type* newVecType = VectorType::get(IntegerType::getInt16Ty(originalBlock->getContext()), numElements);

		SExtInst* signExt = new SExtInst(dyn_cast<Value>(newCmpInst), newVecType);
		signExt->setName("syncExt");
		signExt->insertAfter(newCmpInst);

		//what size should the new type be? 16 bits * the number of elements
		int vecSize = numElements * 16;
		//get something to compare against
		IntegerType* intType = IntegerType::get(originalBlock->getContext(), vecSize);
		Constant* newIntVec = Constant::getAllOnesValue(intType);

		//bitcast the vector to a scalar value
		BitCastInst* vecToScalar = new BitCastInst(signExt, intType);
		vecToScalar->setName("b_cast");
		vecToScalar->insertAfter(signExt);

		//create one more compare instruction
		CmpInst* nextCmpInst = CmpInst::Create(Instruction::OtherOps::ICmp,CmpInst::ICMP_EQ,vecToScalar,newIntVec);
		nextCmpInst->setName("simdSync");
		nextCmpInst->insertAfter(vecToScalar);

		//create terminator
		BranchInst* newTerm;
		newTerm = BranchInst::Create(newBlock, errBlock, nextCmpInst, originalBlock);
		startOfSyncLogic[newTerm] = newCmpInst;
		//this map will help with moving things later if the code is segmented
		simdMap[newCmpInst] = std::make_tuple(signExt, vecToScalar, nextCmpInst);

	} else {
		BranchInst* newTerm;
		newTerm = BranchInst::Create(newBlock, errBlock, newCmpInst, originalBlock);
		startOfSyncLogic[newTerm] = newCmpInst;
	}

	syncCheckMap[originalBlock] = newCmpInst;
}

//----------------------------------------------------------------------------//
// DWC error handling function/blocks
//----------------------------------------------------------------------------//
void dataflowProtection::insertErrorFunction(Module &M, int numClones) {
	Type* t_void = Type::getVoidTy(M.getContext());

	//have to update fault detection block name so it's unique to this module
	// that way the output code can be included in a library file
//	TODO: check if it exists. If it does, use it, otherwise, make random
//	std::string random_suffix = getRandomString(12);
//	fault_function_name += random_suffix;

	Constant* c;
	if(numClones==2)
		c = M.getOrInsertFunction(fault_function_name, t_void, NULL);
	else
		return;

	Function* errFn = dyn_cast<Function>(c);
	assert(errFn && "Fault detection function is non-void");
	errFn->addFnAttr(Attribute::get(M.getContext(), "noinline"));

	//If the user has declared their own error handler, use that
	if( errFn->getBasicBlockList().size() != 0){
		if (verboseFlag) errs() << info_string << " Found existing DWC error handler function\n";
		return;
	}

	//If not, create our own
	Constant* abortC = M.getOrInsertFunction("abort", t_void, NULL);
	Function* abortF = dyn_cast<Function>(abortC);
	assert(abortF && "Abort function detected");

	//Create a basic block that calls abort
	BasicBlock* bb = BasicBlock::Create(M.getContext(), Twine("entry"), errFn, NULL);
	CallInst* new_abort = CallInst::Create(abortF, "", bb);
	UnreachableInst* term = new UnreachableInst(M.getContext(), bb);
}

void dataflowProtection::createErrorBlocks(Module &M, int numClones) {
	Type* t_void = Type::getVoidTy(M.getContext());

	//Create an error handler block for each function - they can't share one
	Constant* c;
	if(numClones == 2)
		c = M.getOrInsertFunction(fault_function_name, t_void, NULL);
	else
		return;

	Function* errFn = dyn_cast<Function>(c);
	assert(errFn && "error function exists");

	for (auto & F : M) {
		if (F.getBasicBlockList().size() == 0)
			continue;

		if(isISR(F))
			continue;

		BasicBlock* originalBlock = &(F.back());
		BasicBlock* errBlock = BasicBlock::Create(originalBlock->getContext(),
				"errorHandler." + Twine(originalBlock->getParent()->getName()),
				originalBlock->getParent(), originalBlock);
		errBlock->moveAfter(originalBlock);

		CallInst* dwcFailCall;
		dwcFailCall = CallInst::Create(errFn, "", errBlock);
		UnreachableInst* term = new UnreachableInst(errBlock->getContext(),
				errBlock);

		// have to give the call some debug info, or compilation issues
		Instruction* lastInst = originalBlock->getTerminator();
//		while (lastInst->is)
		if (lastInst->getDebugLoc()) {
			dwcFailCall->setDebugLoc(lastInst->getDebugLoc());
//			errs() << *dwcFailCall << "\n";
		} else {
//			errs() << *lastInst << "\n";
			;
		}

		errBlockMap[&F] = errBlock;
	}

}

//----------------------------------------------------------------------------//
// TMR error detection
//----------------------------------------------------------------------------//
void dataflowProtection::insertTMRDetectionFlag(Instruction* cmpInst, GlobalVariable* TMRErrorDetected) {
	if (!OriginalReportErrorsFlag) {
		return;
	}

	Instruction* nextInst = cmpInst->getNextNode();
	Value* orig = dyn_cast<Value>(cmpInst->getOperand(0));
	assert(orig && "Original operand exists");

	Value* clone1 = getClone(orig).first;
	Value* clone2 = getClone(orig).second;

	//Insert additional OR operations
	Instruction::OtherOps cmp_op;
	CmpInst::Predicate cmp_eq;
	if (orig->getType()->isFPOrFPVectorTy()) {
		cmp_op = Instruction::OtherOps::FCmp;
		cmp_eq = CmpInst::FCMP_UEQ; //Should this be OEQ instead?
	} else {
		cmp_op = Instruction::OtherOps::ICmp;
		cmp_eq = CmpInst::ICMP_EQ;
	}
	Instruction* cmpInst2 = CmpInst::Create(cmp_op, cmp_eq, orig, clone2, "cmp", nextInst);
	BinaryOperator* andCmps = BinaryOperator::CreateAnd(cmpInst, cmpInst2, "cmpReduction", nextInst);

	//Insert a load, or after the sel inst
	LoadInst* LI = new LoadInst(TMRErrorDetected, "errFlagLoad", nextInst);
	CastInst* castedCmp = CastInst::CreateZExtOrBitCast(andCmps, LI->getType(), "extendedCmp", LI);

	BinaryOperator* BI = BinaryOperator::CreateAdd(LI, castedCmp, "errFlagCmp", nextInst);
	StoreInst* SI = new StoreInst(BI, TMRErrorDetected, nextInst);
}

void dataflowProtection::insertTMRCorrectionCount(Instruction* cmpInst, GlobalVariable* TMRErrorDetected, bool updateSyncPoint) {
	assert(cmpInst && "valid compare instruction");
	assert(TMRErrorDetected && "valid TMR count global");

	if (OriginalReportErrorsFlag) {
		insertTMRDetectionFlag(cmpInst, TMRErrorDetected);
		return;
	} else if (!ReportErrorsFlag) {
		return;
	}

	Instruction* nextInst = cmpInst->getNextNode();
	// value being synchronized on
	Value* orig = dyn_cast<Value>(cmpInst->getOperand(0));
	assert(orig && "Original operand exists");

	Value* clone1 = getClone(orig).first;
	Value* clone2 = getClone(orig).second;

	//Insert additional OR operations
	Instruction::OtherOps cmp_op;
	CmpInst::Predicate cmp_eq;
	if (orig->getType()->isFPOrFPVectorTy()) {
		cmp_op = Instruction::OtherOps::FCmp;
		cmp_eq = CmpInst::FCMP_UEQ; //Should this be OEQ instead?
	} else {
		cmp_op = Instruction::OtherOps::ICmp;
		cmp_eq = CmpInst::ICMP_EQ;
	}
	// compare the original with the 2nd clone
	Instruction* cmpInst2 = CmpInst::Create(cmp_op, cmp_eq, orig, clone2, "cmp", nextInst);

	/* Trying to add support to detecting errors in vector types */
	if (cmpInst->getType()->isVectorTy()) {
		insertVectorTMRCorrectionCount(cmpInst, cmpInst2, TMRErrorDetected);
		return;
	}

	// AND the two compares together to see if either compare failed
	BinaryOperator* andCmps = BinaryOperator::CreateAnd(cmpInst, cmpInst2, "cmpReduction", nextInst);

	if (!andCmps->getType()->isIntegerTy(1)) {
		errs() << "TMR detector can't branch on " << *(andCmps->getType()) << ".  Disable vectorization? (-fno-vectorize) \n";
		errs() << *andCmps << "\n";
		errs() << *andCmps->getParent() << "\n";
		assert(false);
	}

	BasicBlock* originalBlock = cmpInst->getParent();
	// create a new basic block to increment the counter, if there was an error
	BasicBlock* errBlock = BasicBlock::Create(originalBlock->getContext(),
			"errorHandler." + Twine(originalBlock->getParent()->getName()),
			originalBlock->getParent(), originalBlock);

	//Populate new block -- load global counter, increment, store
	LoadInst* LI = new LoadInst(TMRErrorDetected, "errFlagLoad", errBlock);
	Constant* one = ConstantInt::get(LI->getType(), 1, false);
	BinaryOperator* BI = BinaryOperator::CreateAdd(LI, one, "errFlagAdd", errBlock);
	StoreInst* SI = new StoreInst(BI, TMRErrorDetected, errBlock);

	//Split blocks, deal with terminators
	const Twine& name = originalBlock->getParent()->getName() + ".cont";
	// the "vote" instruction is the first one in the new BB
	BasicBlock* originalBlockContinued = originalBlock->splitBasicBlock(nextInst, name);

	// splitting blocks adds an unconditional branch to the new BB; remove it
	originalBlock->getTerminator()->eraseFromParent();
	BranchInst* condGoToErrBlock = BranchInst::Create(originalBlockContinued, errBlock, andCmps, originalBlock);

	// add a branch instruction to the error block to unconditionally go to the continue block
	BranchInst* returnToBB = BranchInst::Create(originalBlockContinued, errBlock);
	errBlock->moveAfter(originalBlock);

	// if terminator for originalBlock was a sync point, be sure to mark the new terminator as such as well
	if (updateSyncPoint) {
		newSyncPoints.push_back(condGoToErrBlock);
	}

	//Update how to divide up blocks
	std::vector<Instruction*> syncHelperList;
	syncHelperMap[originalBlock] = syncHelperList;
	syncHelperMap[originalBlock].push_back(cmpInst);
	syncHelperMap[originalBlock].push_back(cmpInst2);
	syncHelperMap[originalBlock].push_back(andCmps);
	syncCheckMap[originalBlock] = condGoToErrBlock;
	startOfSyncLogic[condGoToErrBlock] = cmpInst;
}

// invalidates the first two arguments
void dataflowProtection::insertVectorTMRCorrectionCount(Instruction* cmpInst, Instruction* cmpInst2, GlobalVariable* TMRErrorDetected) {
	//don't support pointers (yet)
	if (cmpInst->getType()->isPtrOrPtrVectorTy()) {
		assert(false && "not supporting TMR detector with vectors of pointers");
	}

	//change the comparisons to be NotEqual so we can add the results for a total error count
	Instruction::OtherOps cmp_op;
	CmpInst::Predicate cmp_neq;
	Type* vType = cmpInst->getOperand(0)->getType();
	if (vType->isIntOrIntVectorTy()) {
		//integer type
		cmp_op = Instruction::OtherOps::ICmp;
		cmp_neq = CmpInst::ICMP_NE;
	} else if (vType->isFPOrFPVectorTy()) {
		//floating point type
		cmp_op = Instruction::OtherOps::FCmp;
		cmp_neq = CmpInst::FCMP_UNE;
	} else {
		assert(false && "unsupported vector type");
	}
	CmpInst* newCmpInst = CmpInst::Create(cmp_op, cmp_neq, \
			cmpInst->getOperand(0), cmpInst->getOperand(1), "ncmp");
	newCmpInst->insertAfter(cmpInst);
	CmpInst* newCmpInst2 = CmpInst::Create(cmp_op, cmp_neq, \
			cmpInst2->getOperand(0), cmpInst2->getOperand(1), "ncmp");
	newCmpInst2->insertAfter(cmpInst2);
	cmpInst2->replaceAllUsesWith(newCmpInst2);
	cmpInst2->eraseFromParent();

	//need to OR the two cmp's first
	BinaryOperator* cmpOr = BinaryOperator::Create(Instruction::BinaryOps::Or, \
			newCmpInst, newCmpInst2, "reduceOr");
	cmpOr->insertAfter(newCmpInst2);

	BasicBlock* thisBlock = newCmpInst->getParent();
	IRBuilder<> builder(thisBlock);

	VectorType* typ = dyn_cast<VectorType>(newCmpInst->getType());
	//have to extract each element
	uint64_t nTypes = typ->getNumElements();
	VectorType* newVType = VectorType::get(TMRErrorDetected->getValueType(), nTypes);

	//zero-extend the cmpOr result to be the same size as the TMR error counter
	CastInst* zext = CastInst::Create(Instruction::CastOps::ZExt, cmpOr, newVType);
	zext->insertAfter(cmpOr);

	//the alternative to this is to implement a similar approach as is found in how
	// syncTerminator() deals with struct types
	CallInst* redAdd = builder.CreateAddReduce(zext);
	redAdd->moveAfter(zext);

	//add this to the global
	//if there were no errors, then it's just adding 0
	LoadInst* LI = new LoadInst(TMRErrorDetected, "errFlagLoad", thisBlock);
	BinaryOperator* BI = BinaryOperator::CreateAdd(LI, redAdd, "errFlagAdd", thisBlock);
	StoreInst* SI = new StoreInst(BI, TMRErrorDetected, thisBlock);
	LI->moveAfter(redAdd);		BI->moveAfter(LI);		SI->moveAfter(BI);

	return;
}

