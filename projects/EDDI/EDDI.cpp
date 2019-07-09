#define DEBUG_TYPE "EDDI"

#include "../dataflowProtection/dataflowProtection.h"

#include <llvm/Pass.h>
#include <llvm/PassSupport.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Debug.h>

using namespace llvm;

//--------------------------------------------------------------------------//
// Top level behavior
//--------------------------------------------------------------------------//
class EDDI : public ModulePass {
public:
  static char ID;
  EDDI() : ModulePass(ID) {}

  bool runOnModule(Module &M);
  void getAnalysisUsage(AnalysisUsage& AU) const ;
};

char EDDI::ID = 0;
static RegisterPass<EDDI> X("EDDI",
		"Error Detection by Duplication of Instructions", false, false);

bool EDDI::runOnModule(Module &M) {

	errs() << "\n------------------------------------------------\n";
	errs() << "---    WARNING: EDDI is deprecated.          ---\n";
	errs() << "---             Please use DWC instead.      ---\n";
	errs() << "------------------------------------------------\n\n";

	//dataflowProtection DP;

	//DP.run(M,2);

	assert(false && "Switch to DWC");
	return true;
}

//set pass dependencies
void EDDI::getAnalysisUsage(AnalysisUsage& AU) const {
	ModulePass::getAnalysisUsage(AU);
}
