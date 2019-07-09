#define DEBUG_TYPE "DWC"

#include "../dataflowProtection/dataflowProtection.h"

#include <llvm/Pass.h>
#include <llvm/PassSupport.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Debug.h>

using namespace llvm;

//--------------------------------------------------------------------------//
// Top level behavior
//--------------------------------------------------------------------------//
class DWC : public ModulePass {
public:
  static char ID;
  DWC() : ModulePass(ID) {}

  bool runOnModule(Module &M);
  void getAnalysisUsage(AnalysisUsage& AU) const ;
};

char DWC::ID = 0;
static RegisterPass<DWC> X("DWC",
		"Full DWC coverage pass", false, false);

bool DWC::runOnModule(Module &M) {

	dataflowProtection DP;

	DP.run(M,2);

	return true;
}

//set pass dependencies
void DWC::getAnalysisUsage(AnalysisUsage& AU) const {
	ModulePass::getAnalysisUsage(AU);
}
