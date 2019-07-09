/*
 * This is an LLVM pass designed based on principles found in
 * Control-flow checking by software signatures
 * 	N. Oh, P.P Shirvani, E.J. McCluskey
 * http://ieeexplore.ieee.org/document/994926/?part=1
 *
 * This program reads through compiled LLVM IR files and assigns a unique signature to each Basic Block
 * This is to ensure that any Soft Errors will not cause control flow error
 *
 * BYU CCL
 * Begun May 2017
 */

#include "CFCSS.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include <string>
#include <tuple>
#include <utility>
#include <sstream>
#include <iostream>
#include <bitset>
#include <set>
#include <cstdlib>
#include <list>

using namespace llvm;

cl::list<std::string> skipFuncCl("skipFunc", cl::desc("Functions to not add signature checking. Defaults to none."), cl::CommaSeparated, cl::ZeroOrMore);

void CFCSS::getAnalysisUsage(AnalysisUsage& AU) const {
	ModulePass::getAnalysisUsage(AU);
}

CFCSS::BBNode::BBNode(BasicBlock* no, int nu){
    node = no;
    num = nu;
    sig = 0;
    sigDiff = 0;
    sigAdj = 0;
    isBranchFanIn = false;
}

void CFCSS::BBNode::addEdge(BasicBlockEdge* e, int n){
	edges.push_back(e);
	edgeNums.push_back(n);    //this is the number of the node the edge goes to
}

void CFCSS::BBNode::removeEdge(int n){
	int i;
	for(i = 0; i < edgeNums.size(); i++){
		//find the one you want
		if(edgeNums[i] == n){
			break;
		}
	}
	//remove edgeNum and corresponding edge
	//errs() << "Removing edge that points from \n" << *this->node
	//		<< " to \n" << *edges[i]->getEnd() << "\n";
	edgeNums.erase(edgeNums.begin()+i);
	edges.erase(edges.begin()+i);
	return;
}

std::string CFCSS::BBNode::printNode(){
	std::stringstream strm;
	strm << std::endl;
	strm << "Node name: " << node->getName().str() << std::endl;
	strm << "Node number: " << num << std::endl;
	strm << "  Edges (" << edgeNums.size() << "): ";
	auto en = edgeNums.begin();
	for(auto e : edges){
		strm << std::endl << "    " << "To " << e->getEnd()->getName().str();
		strm << "   edge to node# " << *en++;
	}
	strm << std::endl;
	strm << "  Signature: " << sig/*std::bitset<16>(sig)*/ << std::endl;
	strm << "   Sig Diff: " << sigDiff/*std::bitset<16>(sigDiff)*/ << std::endl;
	if(sigAdj != 0)
		strm << "    Sig Adj: " << sigAdj/*std::bitset<16>(sigAdj)*/ << std::endl;
	return strm.str();
}

void CFCSS::insertErrorFunction(Module &M, StringRef name) {
	Type* t_void = Type::getVoidTy(M.getContext());

	Constant* c = M.getOrInsertFunction(name, t_void, NULL);
	Function* errorFn = dyn_cast<Function>(c);
	assert(errorFn && "Error detection function is non-void");

	Constant* abortC = M.getOrInsertFunction("abort", t_void, NULL);
	Function* abortF = dyn_cast<Function>(abortC);
	assert(abortF && "Abort function detected");

	//Create a basic block that calls abort
	BasicBlock* bb = BasicBlock::Create(M.getContext(), Twine(name), errorFn,
	NULL);
	CallInst* new_abort = CallInst::Create(abortF, "", bb);
	UnreachableInst* term = new UnreachableInst(M.getContext(), bb);
}

void CFCSS::createErrorBlocks(Function &F) {
	//Create an error handler block for each function - they can't share one
	Module* M = F.getParent();
	Constant* c = M->getOrInsertFunction("FAULT_DETECTED_CFC",
			Type::getVoidTy(M->getContext()), NULL);
	Function* cfFn = dyn_cast<Function>(c);

	BasicBlock* lastBlock = &(F.back());
	BasicBlock* errBlock = BasicBlock::Create(lastBlock->getContext(),
			"CFerrorHandler." + Twine(F.getName()),	&F, lastBlock);
	errBlock->moveAfter(lastBlock);

	CallInst* cfFailCall;
	cfFailCall = CallInst::Create(cfFn, "", errBlock);
	UnreachableInst* term = new UnreachableInst(errBlock->getContext(),
			errBlock);
	errBlockMap[&F] = errBlock;
}

//some will be specified on the command line
bool CFCSS::skipFnCl(Function* F){
	std::string fName = F->getName().str();
//	errs() << "Checking if should skip " << fName << "\n";
	for(auto it = skipFuncCl.begin(); it != skipFuncCl.end(); it++){
		if(fName.compare(*it) == 0){
//			errs() << "It *is* in the list\n";
			return true;
		}
	}
	return false;
}

//certain functions we don't need to add stuff to because they only contain abort functions
bool CFCSS::shouldSkipF(StringRef name){
	//skip functions with no name (short circuit)
	if(name.size() == 0)
		return false;
	for(StringRef sr : skipFList){
		if(sr == name)
			return true;
	}
	return false;
}

void CFCSS::populateGraph(Module &M){
	int i = 0;
	for(auto &F : M){             //iterate through the functions in the module
		//insert an error block in each Function
		if(F.getBasicBlockList().size() != 0 && !shouldSkipF(F.getName()))
			createErrorBlocks(F);
		for(auto & BB : F){            //iterate through the BasicBlocks in the Function
			BBCount++;
			std::pair<BasicBlock*, int>* p = new std::pair<BasicBlock*, int>(&BB, i);
			//this is used later to find BBs by index
			workList.push_back(p);

			BBNode* BN = new BBNode(&BB, i++);
			graph.push_back(BN);
			for(auto &I : BB){
				//find all of the Call instructions and save them for later
				if(CallInst* CallI = dyn_cast<CallInst>(&I)){
					BN->callList.push_back(CallI);
					Function* calledF = CallI->getCalledFunction();
					calledFunctionList.insert(calledF);
					if(calledFunctionList.count(calledF)){
						//these are functions called from more than one place
						//we need to keep track of these
						multipleFunctionCalls.insert(calledF);
					}
				}
				//do the same for return instructions
				if(ReturnInst* RetI = dyn_cast<ReturnInst>(&I)){
					//but only if its not in main
					if(F.getName() != "main"){
						retInstList.push_back(RetI);
					}
				}
			}
		}
	}
}

void CFCSS::generateSignatures(){
	auto size = graph.size();
	int loopNum = 0;
	while(signatures.size() < size){
		unsigned short sig = rand()%MAX_16_BIT_INT_SIZE;
		if(sig != 0){
			signatures.insert(sig);
			loopNum++;
		}
		//just in case you do something dumb, like change the Macro to 0
		if(loopNum > 32000)
			break;
	}
	return;
}

void CFCSS::BubbleSort(){
	//use Bubblesort since the graph size will always be relatively small
	for(size_t i = 0; i < graph.size()-1; i++){
		for(size_t j = 0; j < graph.size()-1-i; j++){
			if((graph[j]->num) > (graph[j+1]->num)){
				CFCSS::BBNode* b = graph[j];
				graph[j] = graph[j+1];
				graph[j] = b;
			}
		}
	}
}

void CFCSS::sortGraph(){
	//some tricks rely on the graph being sorted by basic block number
	BubbleSort();
	//we know now how many signatures we will need
	generateSignatures();
	auto sigIt = signatures.begin();
	for(BBNode* bn : graph){
		TerminatorInst* TI = bn->node->getTerminator();
		//need to add the outbound edges to determine dependencies & hierarchy
		for(unsigned I = 0, NSucc = TI->getNumSuccessors(); I < NSucc; ++I){
			BasicBlock *Succ = TI->getSuccessor(I);
			BasicBlockEdge* e = new BasicBlockEdge(bn->node, Succ);
			int edgeNum = getIndex(Succ);
			bn->addEdge(e, edgeNum);
		}
		//each node needs a unique signature
		bn->sig = *sigIt++;
		// keep track of all branch fan in nodes
		if(bn->node->getSinglePredecessor() == NULL){
			bn->isBranchFanIn = true;
		}
	}
}

int CFCSS::getIndex(BasicBlock* bb){
	for(auto it : workList){
		if(it->first == bb){
			return it->second;
		}
	}
	errs() << "I didn't find " << *bb << "\n";
	//if you didn't find the basic block, it's not in the graph
	return -1;
}

void CFCSS::checkBuffSig(BBNode* parent, BBNode* buff, BBNode* child){
	//this function is used for debugging purposes in printing out the recently inserted
	//buffer block and signature calculations
	unsigned short parentSig, buffSig, childSig, buffSigDiff, childSigDiff, buffSigAdj,
			XOR1, XOR2, XOR3;
	parentSig = parent->sig;
	buffSig = buff->sig;
	childSig = child->sig;
	buffSigDiff = buff->sigDiff;
	childSigDiff = child->sigDiff;
	buffSigAdj = buff->sigAdj;
	//only need one XOR for buffer since it only has one parent
	XOR1 = parentSig ^ buffSigDiff;
	//now check the calculations from buffer to child
	XOR2 = buffSig ^ childSigDiff;
	XOR3 = XOR2 ^ buffSigAdj;
	/*errs() <<  "   ParentSig: " << parentSig << "\n"
			<< " BuffSigDiff: " << buffSigDiff << "\n"
			<< " XORed gives: " << XOR1 << "\n"
			<< "     BuffSig: " << buffSig << "\n"
			<< "ChildSigDiff: " << childSigDiff << "\n"
			<< " XORed gives: " << XOR2 << "\n"
			<< "  BuffSigAdj: " << buffSigAdj << "\n"
			<< " XORed gives: " << XOR3 << "\n"
			<< "    ChildSig: " << childSig << "\n";
	errs() << "The last two numbers should be equal\n";*/
	return;
}

void CFCSS::updateEdgeNums(BBNode* pred, BBNode* buff, BBNode* succ){
	//when we insert a buffer node, all of the edgeNum info in the corresponding blocks is outdated
	//remove the edge that used to point from pred to succ
	pred->removeEdge(getIndex(succ->node));
	//create a new edge that points from pred to buff
	BasicBlockEdge* e1 = new BasicBlockEdge(pred->node, buff->node);
	pred->addEdge(e1, (int)graph.size()-1);
	//errs() << "Adding new edge from pred " << *pred->node << " to buff " << *buff->node << "\n";
	//create a new edge that points from buff to succ
	BasicBlockEdge* e2 = new BasicBlockEdge(buff->node, succ->node);
	buff->addEdge(e2, getIndex(succ->node));
	//errs() << "Adding new edge from buff " << *buff->node << " to succ " << *succ->node << "\n";
	return;
}

void CFCSS::updatePhiNodes(BBNode* pred, BBNode* buff, BBNode* succ){
	//this function will change the phi node predecessors in succ from pred to buff
	Instruction* spot = succ->node->getFirstNonPHI();
	if(spot != &succ->node->front()){
		//then we need to go through the instructions from the beginning until we hit the firstNonPhi
		for(auto I = succ->node->begin(); &(*I) != spot; I++){
			PHINode* pn = cast<PHINode>(&(*I));
			assert(pn && "invalid phi node");
			//get the index related to the one we're removing
			int predIndex = pn->getBasicBlockIndex(pred->node);
			//replace with the buff block
			pn->setIncomingBlock(predIndex, buff->node);
		}
	}
	return;
}

void CFCSS::updateBranchInst(BBNode* pred, BBNode* buff, BBNode* succ){
	//this function changes the terminator of pred to point to buff instead of succ
	TerminatorInst* TI = pred->node->getTerminator();
	BasicBlock* Succ;
	unsigned I = 0;
	//first find the part of the instruction that points to succ
	for(unsigned NSucc = TI->getNumSuccessors(); I < NSucc; ++I){
		//match the one we're looking for
		Succ = TI->getSuccessor(I);
		if(Succ == succ->node){
			//then we found the right successor
			break;
		}
	}
	//change the successor pointer
	assert(Succ && "Invalid successor block");
	TI->setSuccessor(I, buff->node);
	return;
}

unsigned short CFCSS::getSingleSig(){
	unsigned short newSig;
	size_t newSetSize = signatures.size() + 1;
	while(signatures.size() < newSetSize){
		newSig = rand()%MAX_16_BIT_INT_SIZE;
		signatures.insert(newSig);
	}
	return newSig;
}

CFCSS::BBNode* CFCSS::insertBufferBlock(CFCSS::BBNode* pred, CFCSS::BBNode* succ){
	//in the event of a node that branches to multiple nodes that also have multiple parents,
	//the run time signature adjuster will cause problems
	//insert a buffer block that will not need an adjuster
	//errs() << "-Inserting a buffer-\n";
	//errs() << "  Between " << pred->node->getName() << " and " << succ->node->getName() << "\n";
	Function* parentF = pred->node->getParent();
	Twine name = "Buffer_" + pred->node->getName() + "_" + succ->node->getName();
	BasicBlock* bufferBB = BasicBlock::Create(parentF->getContext(), name, parentF, succ->node);
	//bufferBB->insertInto(parentF, succ->node);  //I don't think I need this
	BBNode* buff = new BBNode(bufferBB, graph.size());
	graph.push_back(buff);
	std::pair<BasicBlock*, int>* p = new std::pair<BasicBlock*, int>(bufferBB, (int)graph.size()-1);
	workList.push_back(p);

	//get a new signature for the new block
	buff->sig = getSingleSig();
	buff->sigDiff = calcSigDiff(pred, buff);
	succ->sigDiff = calcSigDiff(buff, succ);
	buff->isBuffer = true;

	//update the branch instruction in pred:
	updateBranchInst(pred, buff, succ);
	//make buff terminator point only to succ
	BranchInst* buffTerm = BranchInst::Create(succ->node, buff->node);

	//don't forget to change phi node targets in succ (if any exist)
	updatePhiNodes(pred, buff, succ);
	fixBranchCount++;
	succ->isBranchFanIn = true;

	//update node edgeNums
	updateEdgeNums(pred, buff, succ);

	//returns a pointer to the new node
	return buff;
}

bool CFCSS::verifySignatures(){
	//go through the whole graph and make sure all the signatures compute correctly
	unsigned short parentSig, childSig, childSigDiff, XOR1, parentSigAdj, XOR2;
	for(BBNode* bn : graph){
		for(int e : bn->edgeNums){
			if(shouldSkipBB(graph[e]->node->getName()))
				continue;
			parentSig = bn->sig;
			childSig = graph[e]->sig;
			childSigDiff = graph[e]->sigDiff;
			XOR1 = parentSig ^ childSigDiff;
			parentSigAdj = bn->sigAdj;
			XOR2 = XOR1 ^ parentSigAdj;
			if(bn->isBranchFanIn && XOR1 == childSig && parentSigAdj != 0 && !graph[e]->isBuffer){
				//then the RTSA will mess it up
				/*errs() << "RTSA: Found non-matching signature in " << bn->node->getName()
						<< " and " << graph[e]->node->getName() << "\n";
				errs() <<  "   ParentSig: " << parentSig << "\n"
						<< "ChildSigDiff: " << childSigDiff << "\n"
						<< " XORed gives: " << XOR1 << "\n"
						<< "ParentSigAdj: " << parentSigAdj << "\n"
						<< " XORed gives: " << XOR2 << "\n"
						<< "    ChildSig: " << childSig << "\n";
				errs() << "Thus the ParentSigAdj will cause problems\n";
				errs() << "Inserting buffer block: \n";*/
				BBNode* newBuff = insertBufferBlock(bn, graph[e]);
				checkBuffSig(bn, newBuff, graph[e]);
				return false;
			}else if(XOR2 != childSig && !graph[e]->isBuffer){
				//error: definitely won't work at run time
				/*errs() << "XOR2: Found non-matching signature in " << bn->node->getName()
						<< " and " << graph[e]->node->getName() << "\n";
				errs() <<  "   ParentSig: " << parentSig << "\n"
						<< "ChildSigDiff: " << childSigDiff << "\n"
						<< " XORed gives: " << XOR1 << "\n"
						<< "ParentSigAdj: " << parentSigAdj << "\n"
						<< " XORed gives: " << XOR2 << "\n"
						<< "    ChildSig: " << childSig << "\n";
				errs() << "Inserting buffer block: \n";*/
				BBNode* newBuff = insertBufferBlock(bn, graph[e]);
				checkBuffSig(bn, newBuff, graph[e]);
				return false;
			}
		}
	}
	return true;
}

bool CFCSS::shouldSkipBB(StringRef name){
	//there are some BBs we want to skip putting extra instructions into
	//because they are only errorHandler blocks
	for(StringRef sr : skipList){
		//use the startswith function because the IR has extra numbers added on
		if(name.startswith(sr))
			return true;
	}
	return false;
}

unsigned short CFCSS::calcSigDiff(CFCSS::BBNode* pred, CFCSS::BBNode* succ){
	//the difference is (the first time) pred->sig XOR succ->sig
	unsigned short sd = 0;
	//don't need to worry about the error handler blocks
	if(shouldSkipBB(succ->node->getName())){
		sd = 0;
	}else if(succ->sigDiff == 0){
		//then we haven't seen any predecessors before
		sd = pred->sig ^ succ->sig;
	}else{
		//we have seen a predecessor before and need to adjust the signature
		//keep sigDiff unchanged
		sd = succ->sigDiff;
		//make the predecessor adjuster whatever is needed to make
		//pn->sigAdj ^ pn->sig ^ sn->sigDiff = sn->sig
		pred->sigAdj = pred->sig ^ succ->sigDiff ^ succ->sig;
	}
	return sd;
}

void CFCSS::sigDiffGen(){
	for(BBNode* bn : graph){
		//calculate the signature differences
		for(auto e : bn->edgeNums){
			graph[e]->sigDiff = calcSigDiff(bn, graph[e]);
		}
	}
	bool verified = false;
	while(!verified){
		verified = verifySignatures();
	}
}

void CFCSS::printGraph(){
	for(BBNode* bn : graph){
		errs() << bn->printNode();
	}
}

GlobalVariable* CFCSS::setUpGlobal(Module &M, StringRef vName, IntegerType* IT1){
	ConstantInt* CI = ConstantInt::get(IT1, 0, false);
	GlobalVariable* RTS = cast<GlobalVariable>(
			M.getOrInsertGlobal(vName, IT1));
	RTS->setConstant(false);
	RTS->setInitializer(CI);
	RTS->setLinkage(GlobalVariable::CommonLinkage);
	RTS->setUnnamedAddr( GlobalValue::UnnamedAddr() );
	RTS->setAlignment(4);
	return RTS;
}

void CFCSS::insertStoreInsts(BBNode* bn, IntegerType* IT1, GlobalVariable* RTS,
				GlobalVariable* RTSA, Instruction* insertSpot){
	//this first thing is to update the current signature value
	assert(insertSpot && "insert spot is a null pointer!");
	ConstantInt* currentSig = ConstantInt::get(IT1, bn->sig, false);
	StoreInst* SI = new StoreInst(currentSig, RTS);
	SI->insertBefore(insertSpot);

	//update the signature adjuster
	ConstantInt* sigAdjVal = ConstantInt::get(IT1, bn->sigAdj, false);
	StoreInst* SI2 = new StoreInst(sigAdjVal, RTSA);
	SI2->insertAfter(SI);
}

void CFCSS::insertCompInsts(BBNode* bn, IntegerType* IT1, GlobalVariable* RTS,
				GlobalVariable* RTSA, Instruction* insertSpot, bool fromCallInst){
	ConstantInt* nextSig = ConstantInt::get(IT1, bn->sig, false);
	ConstantInt* nextSigDiff = ConstantInt::get(IT1, bn->sigDiff, false);

	LoadInst* RTSval = new LoadInst(RTS, Twine("LoadRTS_"));
	BinaryOperator* XOR = BinaryOperator::Create(Instruction::BinaryOps::Xor,
			RTSval, nextSigDiff, Twine("XOR1_"));

	//only need to worry about this next part if bn is a branch fan-in node
	CmpInst* CI;
	LoadInst* RTSAval;
	BinaryOperator* XOR2;
	if(bn->isBranchFanIn || fromCallInst){
		RTSAval = new LoadInst(RTSA, Twine("LoadRTSAdj_"));
		XOR2 = BinaryOperator::Create(Instruction::BinaryOps::Xor,
				dyn_cast<Value>(XOR), RTSAval, Twine("XOR2_"));
		CI = CmpInst::Create(Instruction::OtherOps::ICmp,
				CmpInst::Predicate::ICMP_EQ, XOR2, nextSig, Twine("CmpXORresult_"));
	}else{
		CI = CmpInst::Create(Instruction::OtherOps::ICmp,
				CmpInst::Predicate::ICMP_EQ, XOR, nextSig, Twine("CmpXORresult_"));
	}

	if(PHINode* pn = dyn_cast<PHINode>(insertSpot)){
		//if it's a Phi node, we need to skip it to keep it at the top
		RTSval->insertBefore(bn->node->getFirstNonPHI());
	}else if(insertSpot == &bn->node->front()){
		RTSval->insertBefore(insertSpot);
	}else{
		RTSval->insertAfter(insertSpot);
	}

	if(bn->isBranchFanIn || fromCallInst){
		RTSAval->insertAfter(RTSval);
		XOR->insertAfter(RTSAval);
		XOR2->insertAfter(XOR);
		CI->insertAfter(XOR2);
	}else{
		XOR->insertAfter(RTSval);
		CI->insertAfter(XOR);
	}
	visited[bn->num] = 1;
	//keep track of where to split blocks later
	splitList.push_back(CI);
	return;
}

Instruction* CFCSS::getInstructionBeforeOrAfter(Instruction* insertAfter, int steps){
	BasicBlock::iterator it = insertAfter->getParent()->begin();
	for(; &(*it) != insertAfter; it++){
		//wait until we've found the right spot
	}
	if(steps > 0){
		for(int j = 0; j < steps; j++){
			it++;
		}
	}
	if(steps < 0){
		for(int k = 0; k > steps; k--){
			it--;
		}
	}
	return &(*it);
}

Instruction* CFCSS::isInBB(BasicBlock* BB){
	for(auto & iter : BB->getInstList()){
		for(Instruction* retI : retInstList){
			if(retI == &iter){
				return retI;
			}
		}
	}
	errs() << "Couldn't find a return instruction in this block\n";
	return NULL;
}

std::list<CFCSS::BBNode*> CFCSS::getRetBBs(Function* F){
	std::list<CFCSS::BBNode*> retBBs;
	for(auto retI : retInstList){
		if(retI->getParent()->getParent() == F){
			retBBs.push_back(graph[getIndex(retI->getParent())]);
		}
	}
	return retBBs;
}

void CFCSS::updateCallInsts(CallInst* callI, BBNode* bn, IntegerType* IT1,
		GlobalVariable* RTS, GlobalVariable* RTSA){
	//assemble all the BBs in question:
	Function* calledF = callI->getCalledFunction();
	BBNode* funcBB = graph[getIndex(&calledF->getEntryBlock())];
	BBNode* callBB = graph[getIndex(callI->getParent())];

	//before we insert stuff, need to set up sigDiff for the entry block of the function
	unsigned short newSigDiff = calcSigDiff(callBB, funcBB);
	funcBB->sigDiff = newSigDiff;

	//there may be multiple return blocks
	std::list<BBNode*> retBBs = getRetBBs(calledF);
	for(BBNode* retBB : retBBs){
		//first make sure the signature adjuster is configured properly at each location
		newSigDiff = calcSigDiff(retBB, callBB);
		callBB->sigDiff = newSigDiff;
		//except this signature adjuster has not been implanted in a StoreInst yet
		Instruction* retI = isInBB(retBB->node);
		retAdjMap.insert(std::pair<Instruction*, unsigned short>(retI, retBB->sigAdj));
	}

	//need to insert extra storeInsts to update signatures before leaving for the call
	insertStoreInsts(callBB, IT1, RTS, RTSA, callI);
	//and then extra compInsts to check CF when returning from function
	if(multipleFunctionCalls.count(calledF)){
		//unless we find a better solution, hold off inserting the compares when coming back from a
		//function if we've already called the function from somewhere else before
		//instead insert more StoreInsts after we come back
		Instruction* insertSpot = getInstructionBeforeOrAfter(callI, 1);
		insertStoreInsts(callBB, IT1, RTS, RTSA, insertSpot);
	}else{
		insertCompInsts(callBB, IT1, RTS, RTSA, callI, true);
	}
	//then add the normal compInsts
	insertCompInsts(funcBB, IT1, RTS, RTSA, &funcBB->node->front(), false);
	return;
}

void CFCSS::verifyCallSignatures(IntegerType* IT1){
	//this function makes sure there are no aliasing issues with the signatures in
	//entry blocks of user-defined functions
	BBNode* funcBB;
	BBNode* callBB;
	Function* calledF;
	unsigned short parentSig, childSig, childSigDiff, XOR1, parentSigAdj, XOR2;
	for(auto callI : callInstList){
		//iterate through each of the call instructions
		calledF = callI->getCalledFunction();
		funcBB = graph[getIndex(&calledF->getEntryBlock())];
		callBB = graph[getIndex(callI->getParent())];
		parentSig = callBB->sig;
		childSig = funcBB->sig;
		childSigDiff = funcBB->sigDiff;
		XOR1 = parentSig ^ childSigDiff;
		Instruction* beforeCall = getInstructionBeforeOrAfter(callI, -1);
		ConstantInt* currSigAdjVal = dyn_cast<ConstantInt>(beforeCall->getOperand(0));
		assert(currSigAdjVal && "Cast not successful in verifyCallSignatures()\n");
		parentSigAdj = currSigAdjVal->getZExtValue();
		XOR2 = XOR1 ^ parentSigAdj;
		if(XOR2 != childSig){
			//error detected; will not work at run-time
			/*errs() << "Found non-matching signature in " << *callBB->node
					<< "\n and \n" << *funcBB->node << "\n";
			errs() <<  "   ParentSig: " << parentSig << "\n"
					<< "ChildSigDiff: " << childSigDiff << "\n"
					<< " XORed gives: " << XOR1 << "\n"
					<< "ParentSigAdj: " << parentSigAdj << "\n"
					<< " XORed gives: " << XOR2 << "\n"
					<< "    ChildSig: " << childSig << "\n";*/
			parentSigAdj = parentSig ^ childSigDiff ^ childSig;
			//errs() << "Setting signature adjuster before call = " << parentSigAdj << "\n";
			//the store of the signature adjuster happens right before the call every time
			Instruction* loadSpot = getInstructionBeforeOrAfter(callI, -1);
			ConstantInt* sigAdjVal = ConstantInt::get(IT1, parentSigAdj, false);
			loadSpot->setOperand(0, sigAdjVal);
		}
	}
	return;
}

void CFCSS::updateRetInsts(IntegerType* IT1){
	//need to insert the changes last once all the splits have been made
	for(auto retIn : retAdjMap){
		//except this signature adjuster has not been implanted in a StoreInst yet
		//the 2nd to last Instruction in the retBB is the StoreInst for sigAdj
		auto sigAdjInst = retIn.first->getParent()->end();
		sigAdjInst--;					//is there a better way to do this?
		//errs() << *sigAdjInst << "\n";  //why do you have to decrement before you can look at it?
		sigAdjInst--;
		//errs() << *sigAdjInst << "\n";
		ConstantInt* adjV = ConstantInt::get(IT1, retIn.second, false);
		(sigAdjInst)->setOperand(0, adjV);
	}
}

void CFCSS::splitBlocks(Instruction* I, BasicBlock* errBlock){
	//Split at I, point current to its own 2nd half as well as error block for this function

	//get a pointer to the current BB
	BasicBlock* currBB = I->getParent();
	//create copy of Inst because split function invalidates it
	Instruction* newCmpInst = I->clone();
	newCmpInst->insertBefore(I);
	newCmpInst->setName(I->getName());

	//name for the new BB
	const Twine& name = currBB->getParent()->getName() + ".split";
	//split and get pointer to new BB
	BasicBlock* newBB = currBB->splitBasicBlock(I, name);

	//make new terminator for the old BB
	currBB->getTerminator()->eraseFromParent();

	BranchInst* newTerm = BranchInst::Create(newBB, errBlock, newCmpInst, currBB);
	I->eraseFromParent();
	splitBlockCount++;
}

bool CFCSS::runOnModule(Module &M) {
	passTime = clock();
	insertErrorFunction(M, "FAULT_DETECTED_CFC");
	populateGraph(M);
	sortGraph();
	sigDiffGen();
	//comment out the next line to remove command line output
	//printGraph();

	//keep track of below which blocks have had stuff inserted
	visited.resize(long(graph.size()), 0);
	//constant stuff
	IntegerType* IT1 = IntegerType::get(M.getContext(), REGISTER_SIZE);

	//put in a global variable to keep track of the current signature
	StringRef vName = "BasicBlockSignatureTracker";
	GlobalVariable* RTS = setUpGlobal(M, vName, IT1);

	//use a run-time signature adjuster that will depend on if there is multiple branching
	StringRef vName2 = "RunTimeSignatureAdjuster";
	GlobalVariable* RTSA = setUpGlobal(M, vName2, IT1);

	for(BBNode* bn : graph){
		//if this is one to skip, then just continue to next iteration of for
		if(shouldSkipBB(bn->node->getName()))
			continue;
		//the user can also specify some functions to skip entirely, such as ISRs
		if(skipFnCl(bn->node->getParent()))
			continue;
		//we need to insert several LLVM IR instructions into each basic block
		insertStoreInsts(bn, IT1, RTS, RTSA, bn->node->getTerminator());

		for(auto e : bn->edgeNums){
			StringRef tsr = graph[e]->node->getName();
			//for each successor, we need to put the correct comparison operation in place
			if(visited[graph[e]->num] == 0 && !shouldSkipBB(tsr)){
				insertCompInsts(graph[e], IT1, RTS, RTSA, &graph[e]->node->front(), false);
			}
		}
		//also need to find any entry blocks in user defined functions and make sure we update
		//those signatures too
		for(auto callI : bn->callList){
			if(callI->isInlineAsm()) //Inline assembly is treated as a call inst
				continue;

			//find the basic block of the name of the one being called
			Function* calledF = callI->getCalledFunction();
			if(calledF == nullptr) {
				//TODO: add support for functions which are couched inside of bitcasts
				continue;
			}
//			assert(calledF && "Called function is valid");
			else if (!calledF->isDeclaration() && !shouldSkipF(calledF->getName())){
				updateCallInsts(callI, bn, IT1, RTS, RTSA);
				callInstList.push_back(callI);
				callCount[calledF->getName()] += 1;
			}
		}
	}
	verifyCallSignatures(IT1);
	for(auto splitInst : splitList){
		splitBlocks(splitInst, errBlockMap[splitInst->getParent()->getParent()]);
	}
	splitList.clear();

	updateRetInsts(IT1);
//	printGraph();
	passTime = clock() - passTime;
	return false;
}
