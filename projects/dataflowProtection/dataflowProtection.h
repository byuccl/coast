#ifndef PROJECTS_Dataflow_protection_H_
#define PROJECTS_Dataflow_protection_H_

#include <vector>
#include <map>
#include <set>
#include <string>
#include <utility>

#include <llvm/Pass.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>

//trying to fix the issue with instructions not segmenting correctly
#define SYNC_POINT_FIX

using namespace llvm;

typedef std::pair<Value*, Value*> ValuePair;

class dataflowProtection : public ModulePass {
public:
  static char ID;
  dataflowProtection() : ModulePass(ID) {}

  bool runOnModule(Module&M);
  bool run(Module&M, int numClones);
  void getAnalysisUsage(AnalysisUsage& AU) const ;

private:

  bool TMR = false;
  bool xMR_default = true;

  //----------------------------------------------------------------------------//
  // Constant strings matching COAST.h annotations
  //----------------------------------------------------------------------------//
  const std::string no_xMR_anno    = "no_xMR";
  const std::string xMR_anno       = "xMR";
  const std::string xMR_call_anno  = "xMR_call";
  const std::string default_xMR    = "set_xMR_default";
  const std::string default_no_xMR = "set_no_xMR_default";
  const std::string default_global = "__xMR_DEFAULT_BEHAVIOR__";

  //----------------------------------------------------------------------------//
  // Constant strings for fancy printing
  //----------------------------------------------------------------------------//
  const std::string err_string		= "\033[0;31mERROR:\033[0m";
  const std::string warn_string		= "\033[0;33mWARNING:\033[0m";
  const std::string info_string		= "\033[0;35mINFO:\033[0m";
  const std::string blue_string		= "\033[0;34m";
  const std::string no_color_string	= "\033[0m";

  //----------------------------------------------------------------------------//
  // Internal variables to keep track of the different mappings
  //----------------------------------------------------------------------------//
  std::set<Function*> fnsToClone;
  std::set<Function*> fnsToSkip;
  std::set<Function*> fnsToCloneAndSkip;
  std::set<Instruction*> instsToClone;
  std::set<Instruction*> instsToSkip;
  std::set<GlobalVariable*> globalsToClone;
  std::set<GlobalVariable*> globalsToSkip;
  std::set<GlobalVariable*> globalsToRuntimeInit;
  std::set<ConstantExpr*> constantExprToClone;
  std::set<Function*> fnsUsedIndirectly;
  std::set<Type*> indirectFnSignatures;

  std::vector<Instruction*> syncPoints;
  std::map<Value*,ValuePair> cloneMap;
  std::map<Function*, BasicBlock*> errBlockMap;
  std::map<Function*, Function*> functionMap;

  std::vector<Function*> origFunctions;
  std::vector<Function*> newFunctions;

  std::map<Function*, std::vector<unsigned int>> argNumsCloned;

  //For moving clones to end
  //Store the cmp instruction inserted when the blocks split
  std::map<BasicBlock*, Instruction*> syncCheckMap;
  //Map the above cmp to the logic it relies on
  std::map<BasicBlock*, std::vector<Instruction*> > syncHelperMap;
  //For TMR, map the sync instruction to the start of the logic chain
  std::map<Instruction*, Instruction*> startOfSyncLogic;
  //in the case of SIMD instructions, need special support for compare logic
  std::map<Instruction*, std::tuple<Instruction*, Instruction*, Instruction*> > simdMap;

  //----------------------------------------------------------------------------//
  // cloning.cpp
  //----------------------------------------------------------------------------//
  // Initialization
  void populateValuesToClone(Module& M);
  // Modify functions
  void populateFnWorklist(Module& M);
  void cloneFunctionArguments(Module& M);
  void updateCallInsns(Module& M);
  // Clone instructions
  bool cloneInsns();
  // Clone constants
  void cloneConstantExpr();
  // Clone globals
  void cloneGlobals(Module& M);
  GlobalVariable* copyGlobal(Module& M, GlobalVariable * g, std::string suffix);
  void addGlobalRuntimeInit(Module& M);

  //----------------------------------------------------------------------------//
  // synchronization.cpp
  //----------------------------------------------------------------------------//
  // Obtain sync points
  void populateSyncPoints(Module& M);
  // Insert synchronization logic
  void processSyncPoints(Module& M, int numClones);
  void syncGEP(GetElementPtrInst* currGEP, GlobalVariable* TMRErrorDetected);
  void syncStoreInst(StoreInst* currStoreInst, GlobalVariable* TMRErrorDetected);
  void processCallSync(CallInst* currCallInst, GlobalVariable* TMRErrorDetected);
  void syncTerminator(TerminatorInst* currTerminator, GlobalVariable* TMRErrorDetected);
  void splitBlocks(Instruction* I, BasicBlock* errBlock);
  // DWC error handling
  void insertErrorFunction(Module& M, int numClones);
  void createErrorBlocks(Module& M, int numClones);
  // TMR error detection
  void insertTMRCorrectionCount(Instruction* cmpInst, GlobalVariable* TMRErrorDetected);
  void insertTMRDetectionFlag(Instruction* cmpInst, GlobalVariable* TMRErrorDetected);

  //----------------------------------------------------------------------------//
  // utils.cpp
  //----------------------------------------------------------------------------//
  // Initialization
  void removeUnusedFunctions(Module& M);
  void processCommandLine(Module& M, int numClones);
  void processAnnotations(Module& M);
  // Cleanup
  void removeAnnotations(Module& M);
  void removeOrigFunctions();
  void removeUnusedErrorBlocks(Module& M);
  void removeUnusedGlobals(Module& M);
  void checkForUnusedClones(Module& M);
  // Cloning utilities
  bool willBeSkipped(Instruction* I);
  bool willBeCloned(Value* v);
  bool isCloned(Value* v);
  ValuePair getClone(Value* I);
  void moveClonesToEndIfSegmented(Module& M);
  int getArrayTypeSize(Module& M, ArrayType * arrayType);
  int getArrayTypeElementBitWidth(Module& M, ArrayType * arrayType);
  void recursivelyVisitCalls(Module& M, Function* F, std::set<Function*> &functionList);
  bool isISR(Function& F);
  void cloneMetadata(Module& M, Function* Fnew);
  // Synchronization utilities
  bool isSyncPoint(Instruction* I);
  // Miscellaneous
  bool isIndirectFunctionCall(CallInst* CI, std::string errMsg, bool print=true);
  int getFunctionsFromConfig();
  void getFunctionsFromCL();
  void dumpModule(Module& M);

};

#endif
