/*
 * CFCSS.h
 *
 *  Created on: May 17, 2017
 *      Author: B James
 *
 *  requires that -ErrorBlocks be run before this pass
 */

#ifndef PROJECTS_CFCSS_CFCSS_H_
#define PROJECTS_CFCSS_CFCSS_H_

#define DEBUG_TYPE "CFCSS"

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/BasicBlock.h"
#include <llvm/PassAnalysisSupport.h>
#include <list>
#include <vector>

using namespace llvm;

STATISTIC(BBCount, "The number of Basic Blocks in the program");
STATISTIC(passTime, "How long it took to run this pass");
STATISTIC(fixBranchCount, "How many buffer blocks were inserted");
STATISTIC(splitBlockCount, "How many times a block was split");

#define MAX_16_BIT_INT_SIZE 65536
#define REGISTER_SIZE 16
//change this ^ if you have something bigger than a 16-bit processor


class CFCSS : public ModulePass {

public:
	static char ID;


	struct BBNode {
	      BasicBlock* node;
	      int num;
	      SmallVector<BasicBlockEdge*, 5> edges;
	      SmallVector<int, 5> edgeNums;
	      SmallVector<CallInst*, 5> callList;
	      unsigned short sig;   		//signature
	      unsigned short sigDiff;		//difference between signature and the one before it
	      unsigned short sigAdj;		//run-time adjusting signature constant
	      bool isBranchFanIn;			//true for nodes with multiple predecessors

	      bool isBuffer = false;

	      BBNode(BasicBlock* no, int nu);
	      void addEdge(BasicBlockEdge* e, int n);
	      void removeEdge(int n);
	      std::string printNode();
	};

	//blacklist of blocks/functions we should skip if discovered
	std::list<StringRef> skipList = {StringRef("EDDI_FAULT_DETECTED"),
			StringRef("errorHandler"), StringRef("CF_FAULT_DETECTED"),
			StringRef("CFerrorHandler")};
	std::list<StringRef> skipFList = {StringRef("EDDI_FAULT_DETECTED"),
			StringRef("CF_FAULT_DETECTED")};
	std::vector< std::pair<BasicBlock*, int>* > workList;
	std::vector<BBNode*> graph;
	std::set<Function*> calledFunctionList;
	std::set<Function*> multipleFunctionCalls;
	std::vector<Instruction*> retInstList;
	std::set<unsigned short> signatures;
	std::vector<int> visited;
	std::map< Instruction*, unsigned short > retAdjMap;
	std::vector<CallInst*> callInstList;
	std::map<StringRef, int> callCount;
	std::vector<Instruction*> splitList;
	std::map<Function*, BasicBlock*> errBlockMap;

	CFCSS() : ModulePass(ID) {BBCount = 0; passTime = 0; fixBranchCount = 0; splitBlockCount = 0;}

	void getAnalysisUsage(AnalysisUsage& AU) const override ;
	void insertErrorFunction(Module &M, StringRef name);
	void createErrorBlocks(Function &F);
	bool skipFnCl(Function* F);
	bool shouldSkipF(StringRef name);
	void populateGraph(Module &M);
	void generateSignatures();
	void BubbleSort();
	void sortGraph();
	int getIndex(BasicBlock* bb);
	void checkBuffSig(BBNode* parent, BBNode* buff, BBNode* child);
	void updateEdgeNums(BBNode* pred, BBNode* buff, BBNode* succ);
	void updatePhiNodes(BBNode* pred, BBNode* buff, BBNode* succ);
	void updateBranchInst(BBNode* pred, BBNode* buff, BBNode* succ);
	unsigned short getSingleSig();
	BBNode* insertBufferBlock(CFCSS::BBNode* pred, CFCSS::BBNode* succ);
	bool verifySignatures();
	bool shouldSkipBB(StringRef name);
	unsigned short calcSigDiff(CFCSS::BBNode* pn, CFCSS::BBNode* sn);
	void sigDiffGen();
	void printGraph();
	GlobalVariable* setUpGlobal(Module &M, StringRef vName, IntegerType* IT1);
	void insertStoreInsts(BBNode* b1, IntegerType* IT1, GlobalVariable* RTS,
			GlobalVariable* RTSA, Instruction* insertSpot);
	void insertCompInsts(BBNode* b1, IntegerType* IT1, GlobalVariable* RTS,
			GlobalVariable* RTSA, Instruction* insertSpot, bool fromCallInst);
	Instruction* getInstructionBeforeOrAfter(Instruction* insertAfter, int steps);
	Instruction* isInBB(BasicBlock* BB);
	std::list<BBNode*> getRetBBs(Function* F);
	void updateCallInsts(CallInst* callI, BBNode* bn, IntegerType* IT1,
			GlobalVariable* RTS, GlobalVariable* RTSA);
	void verifyCallSignatures(IntegerType* IT1);
	void updateRetInsts(IntegerType* IT1);
	void splitBlocks(Instruction* I, BasicBlock* b1);
	bool runOnModule(Module &M);
};

char CFCSS::ID = 0;
static RegisterPass< CFCSS > X("CFCSS", "Control Flow checker", false, false);

#endif /* PROJECTS_CFCSS_CFCSS_H_ */
