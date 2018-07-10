//This file holds all of the logic relevant to synchronization points - error functions or voting

#include "dataflowProtection.h"

#include <deque>

#include <llvm/IR/Module.h>
#include "llvm/Support/CommandLine.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>

//Command line option
extern cl::opt<bool> OriginalReportErrorsFlag;
extern cl::opt<bool> ReportErrorsFlag;
extern cl::opt<bool> noLoadSyncFlag;
extern cl::opt<bool> noStoreDataSyncFlag;
extern cl::opt<bool> noStoreAddrSyncFlag;
extern cl::opt<bool> noMemReplicationFlag;
extern cl::opt<bool> verboseFlag;

using namespace llvm;

//----------------------------------------------------------------------------//
// Obtain synchronization points
//----------------------------------------------------------------------------//
void dataflowProtection::populateSyncPoints(Module& M) {
	for(auto F : fnsToClone) {
		if(F->getName().startswith("FAULT_DETECTED")) //Don't sync in err handler
			continue;

		for (auto & bb : *F) {
			for (auto & I : bb) {

				//Sync before branches
				if (I.isTerminator()){
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

					if (CI->getCalledFunction()->hasExternalLinkage()
							&& CI->getCalledFunction()->isDeclaration()) {
						syncPoints.push_back(&I);
					}
				}

				//Sync data on all stores unless explicitly instructed not to
				if (StoreInst* SI = dyn_cast<StoreInst>(&I)) {
					//Don't sync pointers, they will be different
					if(SI->getOperand(0)->getType()->isPointerTy()){
						continue;
					} else if(dyn_cast<PtrToIntInst>(SI->getOperand(0))){
						//Likewise, don't check casted pointers
						continue;
					}
					syncPoints.push_back(&I);
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

	GlobalVariable* TMRErrorDetected = M.getGlobalVariable("TMR_ERROR_CNT");

	//Look for the variable first. If it doesn't exist, make one
	//If it is unneeded, it is erased at the end of this function
	if(!TMRErrorDetected){
		if(TMR && ReportErrorsFlag && verboseFlag) errs() << "Could not find TMR_ERROR_CNT flag! Creating one...\n";
		TMRErrorDetected = cast<GlobalVariable>(M.getOrInsertGlobal("TMR_ERROR_CNT",
														IntegerType::getInt32Ty(M.getContext())));
		TMRErrorDetected->setConstant(false);
		TMRErrorDetected->setInitializer(ConstantInt::getNullValue(IntegerType::getInt32Ty(M.getContext())));
		TMRErrorDetected->setUnnamedAddr( GlobalValue::UnnamedAddr() );
		TMRErrorDetected->setAlignment(4);
		globalsToSkip.insert(TMRErrorDetected);
	}
	assert(TMRErrorDetected != nullptr);

	for (auto I : syncPoints) {
		if (StoreInst* currStoreInst = dyn_cast<StoreInst>(I)) {
			if(!noStoreDataSyncFlag){
				syncStoreInst(currStoreInst, TMRErrorDetected);
			}
		} else if (CallInst* currCallInst = dyn_cast<CallInst>(I)) {
			processCallSync(currCallInst, TMRErrorDetected);

		} else if(TerminatorInst* currTerminator = dyn_cast<TerminatorInst>(I)) { //is a terminator
			syncTerminator(currTerminator, TMRErrorDetected);

		} else if(GetElementPtrInst* currGEP = dyn_cast<GetElementPtrInst>(I)) {

			if(noMemReplicationFlag){
				//Goal: Don't duplicate memory -> don't sync GEPs, should only be one
				//Double check that the GEP isn't used in an unexpected way
				if(currGEP->getNumUses()!=1){ //Single load, easy
					if(currGEP->getNumUses() != numClones){ //If they match we are ok
						for(auto u : currGEP->users()){
							if(!dyn_cast<StoreInst>(u) && !dyn_cast<LoadInst>(u) && !dyn_cast<CallInst>(u) && !dyn_cast<PHINode>(u)){
								assert(false && "GEP unknown uses");
							}
						}
					}
				}
				continue;
			}

			if(noLoadSyncFlag){
				//Don't sync address of loads
				if( dyn_cast<LoadInst>(currGEP->user_back()) ){
					continue;
				} else if(GetElementPtrInst* nextGEP = dyn_cast<GetElementPtrInst>(currGEP->user_back())){
					//Don't want to sync GEPs that feed GEPs of load inst
					if(nextGEP->getNumUses() == 1){
						if( dyn_cast<LoadInst>(nextGEP->user_back()) ){
							continue;
						}
					}
				}
			}

			if(noStoreAddrSyncFlag){
				//Don't address of stores
				if( dyn_cast<StoreInst>(currGEP->user_back()) ){
					continue;
				} else if(GetElementPtrInst* nextGEP = dyn_cast<GetElementPtrInst>(currGEP->user_back())){
					//Don't want to sync GEPs that feed GEPs of load inst
					if(nextGEP->getNumUses() == 1){
						if( dyn_cast<StoreInst>(nextGEP->user_back()) ){
							continue;
						}
					}
				}
			}

			syncGEP(currGEP, TMRErrorDetected);
		} else{
			assert(false && "Synchronizing at an unrecognized instruction type");
		}
	}

	if(!TMR)
		TMRErrorDetected->eraseFromParent();
}

void dataflowProtection::syncGEP(GetElementPtrInst* currGEP, GlobalVariable* TMRErrorDetected) {
	//2 forms of GEP with different number of arguments
	//Offset is the last argument
	std::vector<Instruction*> syncInsts;
	Value* orig = currGEP->getOperand(currGEP->getNumOperands()-1);

	if(!isCloned(currGEP)){
		//Spell the type out so eclipse doesn't think there is an error
		std::vector<Instruction*>::const_iterator loc = std::find(syncPoints.begin(),syncPoints.end(),currGEP);
			if(loc != syncPoints.end()){
//				errs() << "Erasing " << *currGEP << " from syncPoints\n";
				syncPoints.erase(loc);
			} else{
//				errs() << "COULDNT FIND " << *currGEP << "\n";
			}
		return;
	}

	if (!isCloned(orig)){
		startOfSyncLogic[currGEP] = currGEP;
		return;
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

	if(TMR){
		Value* clone2 = getClone(orig).second;
		assert(clone2 && "Clone exists when syncing at store");
		SelectInst* sel = SelectInst::Create(cmp,orig,clone2,"sel",currGEP);

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

		insertTMRCorrectionCount(cmp,TMRErrorDetected);
	} else{
		Function* currFn = currGEP->getParent()->getParent();
		splitBlocks(cmp, errBlockMap[currFn]);
	}

}

void dataflowProtection::syncStoreInst(StoreInst* currStoreInst, GlobalVariable* TMRErrorDetected) {
	//Keep track of the inserted instructions
	std::vector<Instruction*> syncInsts;

	// Sync the value of the store instruction
	// If memory is cloned we shouldn't sync the address, as they will be different
	Value* orig = currStoreInst->getOperand(0);

	// No need to sync if value is not cloned
	//Additionally, makes sure we don't sync on copies, unless we are forced to sync here
	if (!isCloned(orig) && !noMemReplicationFlag){
		return;
	}
	else if(noMemReplicationFlag){
		//Make sure we don't sync on single return points when memory isn't duplicated
		if(!dyn_cast<StoreInst>(orig) && !isCloned(orig)){
			return;
		}
	}

	Value* clone1 = getClone(orig).first;
	assert(clone1 && "Cloned value exists");

	//Disabling synchronization on constant store
	if(dyn_cast<ConstantInt>(orig)){
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

	if(TMR){
		Value* clone2 = getClone(orig).second;
		assert(clone2 && "Clone exists when syncing at store");
		SelectInst* sel = SelectInst::Create(cmp,orig,clone2,"sel",currStoreInst);
		syncInsts.push_back(sel);

		assert(getClone(currStoreInst).first && "Store instruction has a clone");

		currStoreInst->setOperand(0,sel);
		dyn_cast<StoreInst>(getClone(currStoreInst).first)->setOperand(0,sel);
		dyn_cast<StoreInst>(getClone(currStoreInst).second)->setOperand(0,sel);

		//Make sure that the voted value is propagated downstream
		if(orig->getNumUses() != 2){
			if(Instruction* origInst = dyn_cast<Instruction>(orig)){
				DominatorTree DT = DominatorTree(*origInst->getParent()->getParent());
				for(auto u : origInst->users()){
					//Find any and all instructions that were not updated
					if(std::find(syncInsts.begin(),syncInsts.end(),u) == syncInsts.end()){
						//Get all operands that should be updated
						for(unsigned int opNum=0; opNum < u->getNumOperands(); opNum++){
							//Update if and only if the instruction is dominated by sel
							if(u->getOperand(opNum) == orig && DT.dominates(sel,dyn_cast<Instruction>(u))){
								u->setOperand(opNum,sel);
								if(isCloned(u)){
									dyn_cast<Instruction>(getClone(u).first)->setOperand(opNum,sel);
									dyn_cast<Instruction>(getClone(u).second)->setOperand(opNum,sel);
								}
							}
						}
					}
				}
			}
		}

		insertTMRCorrectionCount(cmp,TMRErrorDetected);
	} else{
		Function* currFn = currStoreInst->getParent()->getParent();
		splitBlocks(cmp, errBlockMap[currFn]);
	}
}

void dataflowProtection::processCallSync(CallInst* currCallInst, GlobalVariable* TMRErrorDetected) {
	//Get a list of the non-constant/GEP arguments, no purpose in checking them
	//Don't compare pointer values either
	std::vector<Instruction*> syncInsts;

	std::deque<Value*> cloneableOperandsList;
	for (unsigned int it = 0; it < currCallInst->getNumArgOperands(); it++) {
		if (isa<Constant>(currCallInst->getArgOperand(it))
				|| isa<GetElementPtrInst>(currCallInst->getArgOperand(it)))
			continue;
		if(isa<PointerType>(currCallInst->getArgOperand(it)->getType()))
			continue;
		cloneableOperandsList.push_back(currCallInst->getArgOperand(it));
	}
	if (cloneableOperandsList.size() == 0){
		startOfSyncLogic[currCallInst] = currCallInst;
		return;
	}

	//We now have a list of (an unknown number) operands, insert comparisons for all of them
	std::deque<Value*> cmpInstList;
	std::vector<Instruction*> syncHelperList;
	BasicBlock* currBB = currCallInst->getParent();
	syncHelperMap[currBB] = syncHelperList;
	bool firstIteration = true;
	for (unsigned int i = 0; i < cloneableOperandsList.size(); i++) {
		Value* orig = cloneableOperandsList[i];

		if (!isCloned(orig))
			continue;
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

		if(TMR){
			SelectInst* sel = SelectInst::Create(cmp,orig,clones.second,"sel",currCallInst);
			syncInsts.push_back(sel);

			currCallInst->replaceUsesOfWith(orig,sel);
			dyn_cast<CallInst>(getClone(currCallInst).first)->replaceUsesOfWith(clones.first,sel);
			dyn_cast<CallInst>(getClone(currCallInst).second)->replaceUsesOfWith(clones.second,sel);

			//If something fails this assertion, it means that it is used after the call synchronization
			//Might have to change it later in case we find a case where this is ok
			//But extensive tests haven't found a case where this is necessary
			int useCount = orig->getNumUses();
			if(useCount != 2){
				if(Instruction* origInst = dyn_cast<Instruction>(orig)){
					DominatorTree DT = DominatorTree(*origInst->getParent()->getParent());
					std::vector<Instruction*> uses;
					for(auto uu : orig->users()){
						uses.push_back(dyn_cast<Instruction>(uu));
					}
					for(auto u : uses){
						//Find any and all instructions that were not updated
						if(std::find(syncInsts.begin(),syncInsts.end(),u) == syncInsts.end()){
							if(!DT.dominates(sel,dyn_cast<Instruction>(u))){
								useCount--;
							//Get all operands that should be updated
							} else{
								for(unsigned int opNum=0; opNum < u->getNumOperands(); opNum++){
									//Update if and only if the instruction is dominated by sel
									if(u->getOperand(opNum) == orig && DT.dominates(sel,dyn_cast<Instruction>(u))){
										u->setOperand(opNum,sel);
										if(isCloned(u)){
											dyn_cast<Instruction>(getClone(u).first)->setOperand(opNum,sel);
											dyn_cast<Instruction>(getClone(u).second)->setOperand(opNum,sel);
										}
										useCount--;
									}
								}
							}
						}
					}
				}
			}
			assert(useCount==2 && "Instruction only used in call sync");
			insertTMRCorrectionCount(cmp,TMRErrorDetected);
		} else{
			cmpInstList.push_back(cmp);
			syncHelperMap[currBB].push_back(cmp);
		}
	}

	if(!TMR){
		if(cmpInstList.size() == 0){
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
	}
}

void dataflowProtection::syncTerminator(TerminatorInst* currTerminator, GlobalVariable* TMRErrorDetected) {
	assert(currTerminator);

	//Only sync if there are arguments to duplicate
	if (isa<BranchInst>(currTerminator)) {
		if (currTerminator->getNumSuccessors() < 2){ //1 successor, or none - unconditional
			startOfSyncLogic[currTerminator] = currTerminator;
			return;
		}
	} else if (isa<ResumeInst>(currTerminator)) {
		//Resume is only for exception handlers, sync always
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

	if(TMR){
		std::vector<Instruction*> syncInsts;
		Value* op = currTerminator->getOperand(0);

		if (!isCloned(op))
				return;

		Instruction* clone1 = dyn_cast<Instruction>(getClone(op).first);
		Instruction* clone2 = dyn_cast<Instruction>(getClone(op).second);
		assert(clone1 && clone2 && "Instruction has clones");

		//Make sure we're inserting the right type of comparison
		Instruction::OtherOps cmp_op;
		CmpInst::Predicate cmp_eq;
		if (op->getType()->isFPOrFPVectorTy()) {
			cmp_op = Instruction::OtherOps::FCmp;
			cmp_eq = CmpInst::FCMP_UEQ; //Should this be OEQ instead?
		} else {
			cmp_op = Instruction::OtherOps::ICmp;
			cmp_eq = CmpInst::ICMP_EQ;
		}
		Instruction* cmp = CmpInst::Create(cmp_op, cmp_eq, op, clone1,
			"cmp", currTerminator);

		startOfSyncLogic[currTerminator] = cmp;

		SelectInst* sel = SelectInst::Create(cmp,op,clone2,"sel",currTerminator);

		currTerminator->replaceUsesOfWith(op,sel);

		syncInsts.push_back(cmp);
		syncInsts.push_back(sel);

		// Too many cases to account for each possiblity, this is removed
		// assert(numUses == 2 && "Instruction only used in terminator synchronization");
		insertTMRCorrectionCount(cmp,TMRErrorDetected);
	} else{
		if (!isCloned(currTerminator->getOperand(0))){
			return;
		}

		Instruction* clone = dyn_cast<Instruction>(getClone(currTerminator->getOperand(0)).first);
		assert(clone && "Instruction has a clone");

		Instruction::OtherOps cmp_op;
		CmpInst::Predicate cmp_eq;
		if (currTerminator->getOperand(0)->getType()->isFPOrFPVectorTy()) {
			cmp_op = Instruction::OtherOps::FCmp;
			cmp_eq = CmpInst::FCMP_UEQ; //Should this be OEQ instead?
		} else {
			cmp_op = Instruction::OtherOps::ICmp;
			cmp_eq = CmpInst::ICMP_EQ;
		}

		Instruction *cmpInst = CmpInst::Create(cmp_op,cmp_eq, currTerminator->getOperand(0),
				clone, "tmp",currTerminator);

		Function* currFn = currTerminator->getParent()->getParent();
		splitBlocks(cmpInst, errBlockMap[currFn]);
	}
}

//#define DEBUG_SIMD_SYNCING
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
	//there are some times it will try to branch on a vector value. This is not supported
	//Instead need to insert additional compare logic. Only necessary with DWC.
	if(!newCmpInst->getType()->isIntegerTy(1) && !TMR){
		//it is possible that the value being compared is a vector type instead of a basic type
//		errs() << "Not a boolean branch!\n" << *newCmpInst << "\n";
//		assert(newCmpInst->getType()->isIntegerTy(1) && "Incorrect branching!\n");

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

#ifdef DEBUG_SIMD_SYNCING
		errs() << "SExt: " << *signExt << "\n";
		errs() << "Bcast: " << *vecToScalar << "\n";
#endif

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
	}else{
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

	Constant* c;
	if(numClones==2)
		c = M.getOrInsertFunction("FAULT_DETECTED_DWC", t_void, NULL);
	else
		return;

	Function* errFn = dyn_cast<Function>(c);
	assert(errFn && "Fault detection function is non-void");

	//If the user has declared their own error handler, use that
	if( errFn->getBasicBlockList().size() != 0){
		return;
	}

	//If not, create our own
	Constant* abortC = M.getOrInsertFunction("abort", t_void, NULL);
	Function* abortF = dyn_cast<Function>(abortC);
	assert(abortF && "Abort function detected");

	//Create a basic block that calls abort
	BasicBlock* bb = BasicBlock::Create(M.getContext(), Twine("entry"), errFn,
	NULL);
	CallInst* new_abort = CallInst::Create(abortF, "", bb);
	UnreachableInst* term = new UnreachableInst(M.getContext(), bb);
}

void dataflowProtection::createErrorBlocks(Module &M, int numClones) {
	Type* t_void = Type::getVoidTy(M.getContext());

	//Create an error handler block for each function - they can't share one
	Constant* c;
	if(numClones == 2)
		c = M.getOrInsertFunction("FAULT_DETECTED_DWC",t_void, NULL);
	else
		return;

	Function* errFn = dyn_cast<Function>(c);

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

		errBlockMap[&F] = errBlock;
	}

}

//----------------------------------------------------------------------------//
// TMR error detection
//----------------------------------------------------------------------------//
void dataflowProtection::insertTMRDetectionFlag(Instruction* cmpInst, GlobalVariable* TMRErrorDetected) {
	if(!OriginalReportErrorsFlag){
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
	Instruction* cmpInst2 = CmpInst::Create(cmp_op, cmp_eq, orig, clone2, "cmp",nextInst);
	BinaryOperator* andCmps = BinaryOperator::CreateAnd(cmpInst,cmpInst2,"cmpReduction",nextInst);

	//Insert a load, or after the sel inst
	LoadInst* LI = new LoadInst(TMRErrorDetected, "errFlagLoad", nextInst);
	CastInst* castedCmp = CastInst::CreateZExtOrBitCast(andCmps,LI->getType(),"extendedCmp",LI);

	BinaryOperator* BI = BinaryOperator::CreateAdd(LI,castedCmp,"errFlagCmp",nextInst);
	StoreInst* SI = new StoreInst(BI,TMRErrorDetected,nextInst);
}

void dataflowProtection::insertTMRCorrectionCount(Instruction* cmpInst, GlobalVariable* TMRErrorDetected) {
	if(OriginalReportErrorsFlag){
		insertTMRDetectionFlag(cmpInst,TMRErrorDetected);
		return;
	} else if(!ReportErrorsFlag){
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
	Instruction* cmpInst2 = CmpInst::Create(cmp_op, cmp_eq, orig, clone2, "cmp",nextInst);
	BinaryOperator* andCmps = BinaryOperator::CreateAnd(cmpInst,cmpInst2,"cmpReduction",nextInst);

	BasicBlock* originalBlock = cmpInst->getParent();
	BasicBlock* errBlock = BasicBlock::Create(originalBlock->getContext(),
			"errorHandler." + Twine(originalBlock->getParent()->getName()),
			originalBlock->getParent(), originalBlock);

	//Populate new block -- load global counter, increment, store
	LoadInst* LI = new LoadInst(TMRErrorDetected, "errFlagLoad", errBlock);
	Constant* one = ConstantInt::get(LI->getType(),1,false);
	BinaryOperator* BI = BinaryOperator::CreateAdd(LI,one,"errFlagAdd",errBlock);
	StoreInst* SI = new StoreInst(BI,TMRErrorDetected,errBlock);

	//Split blocks, deal with terminators
	const Twine& name = originalBlock->getParent()->getName() + ".cont";
	BasicBlock* originalBlockContinued = originalBlock->splitBasicBlock(nextInst, name);

	originalBlock->getTerminator()->eraseFromParent();
	BranchInst* condGoToErrBlock = BranchInst::Create(originalBlockContinued,errBlock,andCmps,originalBlock);

	BranchInst* returnToBB = BranchInst::Create(originalBlockContinued,errBlock);
	errBlock->moveAfter(originalBlock);

	//Update how to divide up blocks
	std::vector<Instruction*> syncHelperList;
	syncHelperMap[originalBlock] = syncHelperList;
	syncHelperMap[originalBlock].push_back(cmpInst);
	syncHelperMap[originalBlock].push_back(cmpInst2);
	syncHelperMap[originalBlock].push_back(andCmps);
	syncCheckMap[originalBlock] = condGoToErrBlock;
	startOfSyncLogic[condGoToErrBlock] = cmpInst;
}
