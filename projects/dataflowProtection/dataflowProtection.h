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

using namespace llvm;


// useful debugging helps
#define PRINT_VALUE(v) errs() << *(v) << "\n"
#define PRINT_STRING(s) errs() << (s) << "\n"


//----------------------------------------------------------------------------//
// Verification Types
//----------------------------------------------------------------------------//
typedef std::pair< Function*, Instruction* > FuncInstPair;
// for making the set only ordered by the function, not the debug information
// https://stackoverflow.com/questions/27893968/stdset-with-stdpair-how-to-write-comparator-for-elements
struct Comparator
{
    bool operator() (const FuncInstPair& lhs, const FuncInstPair& rhs) const
    {
        return lhs.first < rhs.first;
    }
};
// type which represents the two clones of an instruction
// type which maps a global variable to any functions in which it is used (improperly)
typedef std::set< FuncInstPair, Comparator > FunctionDebugSet;
typedef std::map< GlobalVariable*, FunctionDebugSet > GlobalFunctionSetMap;

// types for cloning
typedef std::pair<Value*, Value*> ValuePair;
typedef std::pair<Instruction*, Instruction*> InstructionPair;

// types for verification
typedef std::tuple< Value*, GlobalVariable*, Function* > LoadRecordType;
typedef std::tuple< StoreInst*, GlobalVariable*, Function* > StoreRecordType;
typedef std::tuple< CallInst*, GlobalVariable*, Function* , long > CallRecordType;

//----------------------------------------------------------------------------//
// Class definition
//----------------------------------------------------------------------------//
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
  const std::string skip_call_anno = "coast_call_once";
  const std::string default_xMR    = "set_xMR_default";
  const std::string default_no_xMR = "set_no_xMR_default";
  const std::string default_global = "__xMR_DEFAULT_BEHAVIOR__";
  const std::string repl_ret_anno  = "repl_return_val";
  const std::string isr_anno	     = "isr_function";
  const std::string prot_lib_anno  = "protected_lib";
  const std::string cloneAfterCallAnno = "clone-after-call-";

  //----------------------------------------------------------------------------//
  // Constant strings for fancy printing
  //----------------------------------------------------------------------------//
  #ifdef NO_COLOR_PRINTING
  const std::string err_string		= "ERROR:";
  const std::string warn_string		= "WARNING:";
  const std::string info_string		= "INFO:";
  const std::string blue_string		= "";
  const std::string no_color_string	= "";
  #else
  const std::string err_string		= "\033[0;31mERROR:\033[0m";
  const std::string warn_string		= "\033[0;33mWARNING:\033[0m";
  const std::string info_string		= "\033[0;35mINFO:\033[0m";
  const std::string blue_string		= "\033[0;34m";
  const std::string no_color_string	= "\033[0m";
  #endif

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
  std::set<GlobalVariable*> volatileGlobals;
  std::set<Function*> usedFunctions; 	    /* marked with __attribute__((used)) */
  std::set<Function*> isrFunctions;		    /* marked with directive as ISR */
  std::set<Function*> replReturn; 		    /* marked to replicate return values */
  std::set<Function*> cloneAfterFnCall;   /* marked to only call once */
  std::set<Function*> protectedLibList;   /* marked to protect w/o changing signature */
  std::set<GlobalVariable*> globalsToRuntimeInit;
  std::set<ConstantExpr*> constantExprToClone;

  std::set<Instruction*> instsToCloneAnno;
  std::set<Instruction*> wrapperInsts;
  std::map<CallInst*, std::vector<int> > cloneAfterCallArgMap;

  std::vector<Instruction*> syncPoints;
  std::vector<Instruction*> newSyncPoints;		// added while processing old ones
  std::map<Value*,ValuePair> cloneMap;
  std::map<Function*, BasicBlock*> errBlockMap;
  std::map<Function*, Function*> functionMap;
  std::map<Function*, SmallVector<ReturnInst*, 8>> replRetMap;

  // vector probably actually is faster in this case, since no find() being called
  std::vector<Function*> origFunctions;

  std::map<Function*, std::vector<unsigned int>> argNumsCloned;

  // For moving clones to end
  // Store the cmp instruction inserted when the blocks split
  std::map<BasicBlock*, Instruction*> syncCheckMap;
  // Map the above cmp to the logic it relies on
  std::map<BasicBlock*, std::vector<Instruction*> > syncHelperMap;
  // For TMR, map the sync instruction to the start of the logic chain
  std::map<Instruction*, Instruction*> startOfSyncLogic;
  // in the case of SIMD instructions, need special support for compare logic
  std::map<Instruction*, std::tuple<Instruction*, Instruction*, Instruction*> > simdMap;

  //----------------------------------------------------------------------------//
  // cloning.cpp
  //----------------------------------------------------------------------------//
  // Initialization
  void populateValuesToClone(Module& M);
  // Modify functions
  void populateFnWorklist(Module& M);
  void cloneFunctionArguments(Module& M);
  void cloneFunctionReturnVals(Module& M);
  void updateRRFuncs(Module& M);
  void validateRRFuncs(void);
  void updateCallInsns(Module& M);
  void updateInvokeInsns(Module& M);
  // Clone instructions
  bool cloneInsns();
  void cloneConstantExprOperands(ConstantExpr* ce, std::pair<Instruction *,Instruction *> clone, unsigned i);
  void cloneConstantVectorOperands(ConstantVector* constVec, InstructionPair clone, unsigned i);
  void verifyCloningSuccess();
  // Clone constants
  void cloneConstantExpr();
  // Clone globals
  void cloneGlobals(Module& M);
  GlobalVariable* copyGlobal(Module& M, GlobalVariable* copyFrom, std::string newName);
  void addGlobalRuntimeInit(Module& M);
  // cloning debug information
  void cloneMetadata(Module& M, Function* Fnew);
  // fix instruction lists
  void updateInstLists(Function* F, Function* Fnew);

  //----------------------------------------------------------------------------//
  // synchronization.cpp
  //----------------------------------------------------------------------------//
  // Obtain sync points
  void populateSyncPoints(Module& M);
  // Insert synchronization logic
  void processSyncPoints(Module& M, int numClones);
  bool syncGEP(GetElementPtrInst* currGEP, GlobalVariable* TMRErrorDetected);
  void syncStoreInst(StoreInst* currStoreInst, GlobalVariable* TMRErrorDetected, bool forceFlag = false);
  void processCallSync(CallInst* currCallInst, GlobalVariable* TMRErrorDetected);
  void syncTerminator(TerminatorInst* currTerminator, GlobalVariable* TMRErrorDetected);
  Instruction* splitBlocks(Instruction* I, BasicBlock* errBlock);
  // DWC error handling
  void insertErrorFunction(Module& M, int numClones);
  void createErrorBlocks(Module& M, int numClones);
  // TMR error detection
  void insertTMRDetectionFlag(Instruction* cmpInst, GlobalVariable* TMRErrorDetected);
  void insertTMRCorrectionCount(Instruction* cmpInst, GlobalVariable* TMRErrorDetected, bool updateSyncPoint = false);
  void insertVectorTMRCorrectionCount(Instruction* cmpInst, Instruction* cmpInst2, GlobalVariable* TMRErrorDetected);
  // stack protection
  void insertStackProtection(Module& M);

  //----------------------------------------------------------------------------//
  // utils.cpp
  //----------------------------------------------------------------------------//
  // Cleanup unused things
  int removeUnusedFunctions(Module& M);
  void removeOrigFunctions();
  void removeUnusedErrorBlocks(Module& M);
  void removeUnusedGlobals(Module& M);
  void checkForUnusedClones(Module& M);
  // Synchronization utilities
  void moveClonesToEndIfSegmented(Module& M);
  GlobalVariable* createGlobalVariable(Module& M, std::string name, unsigned int byteSz);
  // Run-time initialization of globals
  int getArrayTypeSize(Module& M, ArrayType * arrayType);
  int getArrayTypeElementBitWidth(Module& M, ArrayType * arrayType);
  void recursivelyVisitCalls(Module& M, Function* F, std::set<Function*> &functionList);
  // Miscellaneous
  void walkInstructionUses(Instruction* I, bool xMR);
  void updateFnWrappers(Module& M);
  std::string getRandomString(std::size_t len);
  void dumpModule(Module& M);

  //----------------------------------------------------------------------------//
  // verification.cpp
  //----------------------------------------------------------------------------//
  bool comesFromSingleCall(Instruction* storeUse);
  void walkUnPtStores(StoreRecordType &record);
  void verifyOptions(Module& M);
  void printGlobalScopeErrorMessage(GlobalFunctionSetMap &globalMap,
  		bool globalPt, std::string directionMessage);
  
  //----------------------------------------------------------------------------//
  // inspection.cpp
  //----------------------------------------------------------------------------//
  // Cloning utilities
  bool willBeSkipped(Instruction* I);
  bool willBeCloned(Value* v);
  bool isCloned(Value* v);
  ValuePair getClone(Value* I);
  Value* getCloneOrig(Value* v);
  bool isCoarseGrainedFunction(StringRef fnName);
  // Synchronization utilities
  bool isSyncPoint(Instruction* I);
  bool isStoreMovePoint(StoreInst* SI);
  bool isCallMovePoint(CallInst* ci);
  bool checkCoarseSync(StoreInst* inst);
  // Miscellaneous
  bool isIndirectFunctionCall(CallInst* CI, std::string errMsg, bool print=true);
  bool isISR(Function& F);

  //----------------------------------------------------------------------------//
  // interface.cpp
  //----------------------------------------------------------------------------//
  void getFunctionsFromCL();
  int getFunctionsFromConfig();
  void processCommandLine(Module& M, int numClones);
  void processAnnotations(Module& M);
  void processLocalAnnotations(Module& M);
  // cleanup
  void removeAnnotations(Module& M);
  void removeLocalAnnotations(Module& M);

};

#endif
