#define DEBUG_TYPE "TMR"

#include "../dataflowProtection/dataflowProtection.h"

#include <llvm/Pass.h>
#include <llvm/PassSupport.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Debug.h>

using namespace llvm;

//--------------------------------------------------------------------------//
// Top level behavior
//--------------------------------------------------------------------------//
class TMR : public ModulePass {
public:
  static char ID;
  TMR() : ModulePass(ID) {}

  bool runOnModule(Module &M);
  void getAnalysisUsage(AnalysisUsage& AU) const ;
};

char TMR::ID = 0;
static RegisterPass<TMR> X("TMR",
		"Full TMR coverage pass", false, false);

bool TMR::runOnModule(Module &M) {

	dataflowProtection DP;

	DP.run(M,3);

	return true;
}

//set pass dependencies
void TMR::getAnalysisUsage(AnalysisUsage& AU) const {
	ModulePass::getAnalysisUsage(AU);
}
