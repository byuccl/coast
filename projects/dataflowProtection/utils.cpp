// This file contains utilities that help set up or clean up after the pass

#include "dataflowProtection.h"

#include <queue>
#include <list>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <cstdlib>

#include <llvm/IR/Module.h>
#include "llvm/Support/CommandLine.h"
#include <llvm/Support/Debug.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include "llvm/ADT/StringRef.h"

// Command line options
extern cl::opt<bool> InterleaveFlag;
extern cl::opt<bool> SegmentFlag;
extern cl::list<std::string> skipFnCl;
extern cl::list<std::string> skipLibCallsCl;
extern cl::list<std::string> replicateUserFunctionsCallCl;
extern cl::list<std::string> ignoreGlblCl;
extern cl::list<std::string> globalsToRuntimeInitCl;
extern cl::opt<bool> noMemReplicationFlag;
extern cl::opt<bool> noStoreDataSyncFlag;
extern cl::opt<bool> storeDataSyncFlag;
extern cl::opt<bool> ReportErrorsFlag;
extern cl::opt<std::string> configFileLocation;
extern cl::opt<bool> dumpModuleFlag;
extern cl::opt<bool> verboseFlag;

//these are the names of the above CL lists
//any changes to these must also be changed at the head of dataflowProtection.cpp
const std::string skipFnName = "ignoreFns";
const std::string ignoreGlblName = "ignoreGlbls";
const std::string skipLibCallsName = "skipLibCalls";
const std::string coarseFnsName = "replicateFnCalls";
const std::string runtimeGlblInitName = "runtimeInitGlobals";

//new global lists to be used to track function names
std::list<std::string> skipFn;
std::list<std::string> skipLibCalls;
std::list<std::string> coarseGrainedUserFunctions;
std::list<std::string> ignoreGlbl;
std::list<std::string> clGlobalsToRuntimeInit;

//track functions that we should ignore invalid SOR crossings
std::map<GlobalVariable*, Function*> globalCrossMap;

//also, there are some functions that are not supported
//it is in here instead of the config file because we don't want users touching it
std::list<std::string> unsupportedFunctions = {"fscanf", "scanf", "fgets", "gets", "sscanf", "__isoc99_fscanf"};

using namespace llvm;

//----------------------------------------------------------------------------//
// Miscellaneous
//----------------------------------------------------------------------------//
bool dataflowProtection::isIndirectFunctionCall(CallInst* CI, std::string errMsg, bool print) {
	//This partially handles bitcasts and other inline LLVM functions
	if (CI->getCalledFunction() == nullptr) {
		// probably don't want to hear about skipping inline assembly, clean up output
		if( (print || verboseFlag) && !CI->isInlineAsm())
			errs() << warn_string << " in " << errMsg << " skipping:\n\t" << *CI << "\n";
		return true;
	} else {
		return false;
	}
}

// returns a string of random characters of the requested size
// used to name-mangle the DWC error handler block (under construction)
std::string dataflowProtection::getRandomString(std::size_t len) {
	//init rand
	std::srand(time(0));

	const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	int charLen = sizeof(chars) - 1;
	std::string result = "";

	for (size_t i = 0; i < len; i+=1) {
		result += chars[rand() % charLen];
	}

	return result;
}

void dataflowProtection::getFunctionsFromCL() {
	//copy all (fixed) things from the command line to the internal, editable lists
	for(auto x : skipLibCallsCl){
		skipLibCalls.push_back(x);
	}

	for(auto x : skipFnCl){
		skipFn.push_back(x);
	}

	for(auto x : replicateUserFunctionsCallCl){
		coarseGrainedUserFunctions.push_back(x);
	}

	for(auto x : ignoreGlblCl){
		ignoreGlbl.push_back(x);
	}

	for(auto x : globalsToRuntimeInitCl){
		clGlobalsToRuntimeInit.push_back(x);
	}
}

//function to extract function names from the configuration file
//lists already exist, created in dataflowProtection.cpp
//return value indicates success or failure
int dataflowProtection::getFunctionsFromConfig() {
	std::string filename;
	if (configFileLocation!="") {
		filename = configFileLocation;
	} else {
		char* coast = std::getenv("COAST_ROOT");
		if (coast) {
			filename = std::string(coast) + "/projects/dataflowProtection/functions.config";
		} else {
			// just look in the current directory
			filename = "functions.config";
		}
	}
	std::ifstream ifs(filename, std::ifstream::in);

	if (!ifs.is_open()) {
		errs() << "ERROR: No configuration file found at " << filename << '\n';
		errs() << "         Please pass one in using -configFile\n";
		return -1;
	}

	std::list<std::string>* lptr;
	std::string line;
	while (getline(ifs, line)) {
		if (line.length() == 0) {  //Blank line
			continue;
		}

		if (line[0] == '#') { //# is the comment symbol
			continue;
		}

		//remove all whitespace
		line.erase(remove (line.begin(), line.end(), ' '), line.end());

		std::istringstream iss(line);
		std::string substr;
		getline(iss, substr, '=');

		//Find the option we're using
		if(substr == skipLibCallsName)
			lptr = &skipLibCalls;
		else if(substr == skipFnName)
			lptr = &skipFn;
		else if (substr == coarseFnsName)
			lptr = &coarseGrainedUserFunctions;
		else if (substr == ignoreGlblName)
			lptr = &ignoreGlbl;
		else if (substr == runtimeGlblInitName)
			lptr = &clGlobalsToRuntimeInit;
		else{
			errs() << "ERROR: unrecognized option " << substr;
			errs() <<" in configuration file " << filename << "\n\n";
			return 1;
		}

		//insert all options into vector
		while (iss.good()) {
			getline(iss, substr, ',');
			if (substr.length() == 0)
				continue;
			lptr->push_back(substr);
		}

	}
	ifs.close();
	return 0;
}

//If -dumpModule is passed in, then print the entire module out
//This is helpful when the pass crashes on cleanup
//It is in a format that can be pasted into an *.ll file and run
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
}

//----------------------------------------------------------------------------//
// Initialization code
//----------------------------------------------------------------------------//
void dataflowProtection::removeUnusedFunctions(Module& M) {

	//get reference to main() function
	Function* mainFunction = M.getFunction("main");
	//If we don't have a main, don't remove any functions
	if (!mainFunction) {
		return;
	}

	//Populate a list of all functions in the module
	std::set<Function*> functionList;
	for (auto & F : M) {
		//Ignore external function declarations
		if (F.hasExternalLinkage() && F.isDeclaration()) {
			continue;
		}

		//Don't erase fault handlers
		if (F.getName().startswith("FAULT_DETECTED_")) {
			continue;
		}

		//Don't erase ISRs
		if(isISR(F))
			continue;

		if(F.getNumUses() != 0)
			continue;

		if (usedFunctions.find(&F) != usedFunctions.end())
			continue;

		functionList.insert(&F);
	}

	recursivelyVisitCalls(M, mainFunction, functionList);

	if (functionList.size() == 0) {
		return;
	}

	// TODO: fix assertion - it's possible for a xMR'd function to be in the list of no uses,
	//  if it's used as a function pointer only
	if(functionList.size() > 0)
		if(verboseFlag) errs() << "The following functions are unused, removing them: \n";
	for (auto q : functionList) {
		if (fnsToClone.find(q) != fnsToClone.end()) {
			errs() << "Failed removing function '" << q->getName() << "'\n";
		}
		assert( (fnsToClone.find(q) == fnsToClone.end()) && "The specified function is not called, so is being removed");
		if(verboseFlag) errs() << "    " << q->getName() << "\n";
		q->eraseFromParent();
	}

}

void dataflowProtection::processCommandLine(Module& M, int numClones) {
	if (InterleaveFlag == SegmentFlag) {
		SegmentFlag = true;
	}
	TMR = (numClones==3);

	if (noMemReplicationFlag && noStoreDataSyncFlag) {
		errs() << warn_string << " noMemDuplication and noStoreDataSync set simultaneously. Recommend not setting the two together.\n";
	}

	if (noStoreDataSyncFlag && storeDataSyncFlag) {
		errs() << err_string << " conflicting flags for store and noStore!\n";
		exit(-1);
	}

	//copy command line lists to internal lists
	getFunctionsFromCL();

	if (getFunctionsFromConfig()) {
		assert("Configuration file error!" && false);
	}

	if (skipFn.size() == 0) {
		for (auto & fn_it : M) {

			if (fn_it.isDeclaration()) { //Ignore library calls
				continue;
			}

			if (isISR(fn_it)) { //Don't erase ISRs
				continue;
			}

			if (xMR_default)
				fnsToClone.insert(&fn_it);
		}
	} else {
		for (auto fcn : skipFn) {
			Function* f = M.getFunction(StringRef(fcn));
			if (!f) {
				errs() << "\n" << err_string << "Specified function " << fcn << " does not exist!\n";
				errs() << "Check the spelling, check if the optimizer inlined it, of if name was mangled\n\n";
				assert(f);
			}
			fnsToSkip.insert(f);
		}
	}

}

void dataflowProtection::processAnnotations(Module& M) {
	//Inspired by http://bholt.org/posts/llvm-quick-tricks.html
	auto global_annos = M.getNamedGlobal("llvm.global.annotations");
	if (global_annos) {
		auto a = cast<ConstantArray>(global_annos->getOperand(0));
		//check that it is the right type
		if (a) {
			for (int i=0; i < a->getNumOperands(); i++) {
				auto e = cast<ConstantStruct>(a->getOperand(i));

				//extract data
				auto anno = cast<ConstantDataArray>(cast<GlobalVariable>(e->getOperand(1)->getOperand(0))->getOperand(0))->getAsCString();

				//Function annotations
				if (auto fn = dyn_cast<Function>(e->getOperand(0)->getOperand(0))) {
					if (anno == no_xMR_anno) {
						if(verboseFlag) errs() << "Directive: do not clone function '" << fn->getName() << "'\n";
						fnsToSkip.insert(fn);
						if (fnsToClone.find(fn)!=fnsToClone.end())
							fnsToClone.erase(fn);
					} else if (anno == xMR_anno) {
						if(verboseFlag) errs() << "Directive: clone function '" << fn->getName() << "'\n";
						fnsToClone.insert(fn);
					} else if (anno == xMR_call_anno) {
						if(verboseFlag) errs() << "Directive: replicate calls to function '" << fn->getName() << "'\n";
						coarseGrainedUserFunctions.push_back(fn->getName());
					} else if (anno == skip_call_anno) {
						if(verboseFlag) errs() << "Directive: do not clone calls to function '"  << fn->getName() << "'\n";
						skipLibCalls.push_back(fn->getName());
						//TODO: do we need to worry about duplicates?
					} else if (anno.startswith("no-verify-")) {
						StringRef global_name = anno.substr(10, anno.size() - 10);

						GlobalValue* glbl = M.getNamedValue(global_name);
						if (glbl) {
							GlobalVariable* glblVar = dyn_cast<GlobalVariable>(glbl);
							if (glblVar) {
								globalCrossMap[glblVar] = fn;
								errs() << "Directive: ignoring global \"" << global_name
										<< "\" being used in unprotected function \"" << fn->getName() << "\"\n";
							}
						} else {
							errs() << warn_string << " global " << global_name << " doesn't exist\n";
						}

					} else {
						assert(false && "Invalid option on function");
					}

				}
				//Global annotations
				else if (auto gv = dyn_cast<GlobalVariable>(e->getOperand(0)->getOperand(0))) {
					if (anno == no_xMR_anno) {
						if(verboseFlag) errs() << "Directive: do not clone global variable '" << gv->getName() << "'\n";
						globalsToSkip.insert(gv);
					} else if (anno == xMR_anno) {
						if(verboseFlag) errs() << "Directive: clone global variable '" << gv->getName() << "'\n";
						globalsToClone.insert(gv);
					} else if (anno == default_xMR) {
						if(verboseFlag) errs() << "Directive: set xMR as default\n";
					} else if (anno == default_no_xMR) {
						if(verboseFlag) errs() << "Directive: set no xMR as default\n";
						xMR_default = false;
					} else if (anno == coast_volatile) {
						if(verboseFlag) errs() << "Directive: don't remove '" << gv->getName() << "'\n";
						volatileGlobals.insert(gv);
					} else {
						if(verboseFlag) errs() << "Directive: " << anno << "\n";
						assert(false && "Invalid option on global value");
					}
				}
				else {
					assert(false && "Non-function annotation");
				}
			}
		} else {
			errs() << warn_string << " global annotations of wrong type!\n" << *global_annos << "\n";
		}
	}

	// get the data from the list of "used" globals, and add it to volatileGlobals
	auto used_annos = M.getNamedGlobal("llvm.used");
	if (used_annos) {
		auto ua = cast<ConstantArray>(used_annos->getOperand(0));
		if (ua) {
			for (int i=0; i < ua->getNumOperands(); i++) {
				auto element = ua->getOperand(i);
				if (BitCastOperator* bc = dyn_cast<BitCastOperator>(element)) {
					errs() << " >>> Hooray, found a bitcast!\n";
					if (GlobalVariable* gv = dyn_cast<GlobalVariable>(bc->getOperand(0))) {
						errs() << *gv << "\n";
						volatileGlobals.insert(gv);
					} else if (Function* fn = dyn_cast<Function>(bc->getOperand(0))) {
						errs() << " <<< found a used function:\n";
						errs() << fn->getName() << "\n";
						usedFunctions.insert(fn);
					}
				}	// TODO: what if it doesn't have to be bit-casted?
			}
		}
	}

	//Local variables
	for(auto &F : M){
		for(auto &bb : F){
			for(auto &I : bb){
				if( auto CI = dyn_cast<CallInst>(&I) ){
					// have to skip any bitcasts in function calls because they aren't actually a function
					if(isIndirectFunctionCall(CI, "processAnnotations", false))
						continue;
					if(CI->getCalledFunction()->getName() == "llvm.var.annotation"){
						//Get variable
						auto adr = dyn_cast<BitCastInst>(CI->getOperand(0));
						AllocaInst* var;
						if (!adr) {
							//there could be no bitcast if the alloca is already of type i8
							var = dyn_cast<AllocaInst>(CI->getOperand(0));
						} else {
							var = dyn_cast<AllocaInst>(adr->getOperand(0));
						}
						assert(var && "valid alloca");

						auto ce = dyn_cast<ConstantExpr>(CI->getOperand(1));
						auto gv  = dyn_cast<GlobalVariable>(ce->getOperand(0));
						auto anno = dyn_cast<ConstantDataArray>(gv->getInitializer())->getAsCString();

						if(var){
							if(anno == no_xMR_anno){
								if(verboseFlag) errs() << "Directive: do not clone local variable '" << *var << "'\n";
								instsToSkip.insert(var);
								walkInstructionUses(var, false);
							} else if(anno == xMR_anno){
								if(verboseFlag) errs() << "Directive: clone local variable '" << *var << "'\n";
								instsToCloneAnno.insert(var);
								//if this is all we do, it will only clone the `alloca` instruction, but
								// we want it to clone all instructions that use the same variable
								walkInstructionUses(var, true);
								//how do we get the syncpoints to happen?
								//have to add them manually
							} else{
								errs() << anno << "\n";
								assert(false && "Unrecognized variable annotation");
							}
						} else{
							assert(false && "Local variable not alloca");
						}
					}
				}
			}
		}
	}
}

//----------------------------------------------------------------------------//
// Cleanup
//----------------------------------------------------------------------------//
void dataflowProtection::removeAnnotations(Module& M) {
	auto global_annos = M.getNamedGlobal("llvm.global.annotations");
	if (!global_annos)
		return;
	auto a = cast<ConstantArray>(global_annos->getOperand(0));
	if (!a)
		return;

	std::set<GlobalVariable*> anno_strings;

	//Populate a list of global strings that are only used in annotations
	for (int i=0; i < a->getNumOperands(); i++) {
		auto e = cast<ConstantStruct>(a->getOperand(i)); //This is part of global_anno
		auto anno = cast<GlobalVariable>(e->getOperand(1)->getOperand(0)); //This is the global string

		for (int j=0; j < e->getNumOperands(); j++) {
			if (e->getOperand(j)->getNumOperands() >= 1) {
				if (auto cs = dyn_cast<GlobalVariable>(e->getOperand(j)->getOperand(0))) {
					if (cs->getSection() == "llvm.metadata") {
						anno_strings.insert(cs);
					}
				}
			}
		}
	}

	//Remove llvm.var.annotation calls
	std::set<Instruction*> toRemove;
	Function* lva = NULL;
	for (auto &F : M) {
		for (auto & bb : F) {
			for (auto & I : bb) {
				if (auto CI = dyn_cast<CallInst>(&I)) {
					auto called = CI->getCalledFunction();
					if ( (called != nullptr) && (called->getName() == "llvm.var.annotation") ) {
						lva = called;
						toRemove.insert(CI);
					}
				}
			}
		}
	}

	for (auto rm : toRemove) {
		auto op0 = dyn_cast<Instruction>(rm->getOperand(0));
		if (rm->getNumUses() < 1) {
			if (rm->getParent()) {
				rm->eraseFromParent();
			}
		}
		//do this 2nd so that the one possible user is removed first
		if (op0 && op0->getNumUses() < 1) {
			if (op0->getParent()) {
				op0->eraseFromParent();
			}
			//we probably added this (which is probably a bitcast) to the list of instructions to clone
			if (std::find(instsToCloneAnno.begin(), instsToCloneAnno.end(), op0) != instsToCloneAnno.end()) {
				instsToCloneAnno.erase(op0);
			}
		}
	}

	if (lva) {
		lva->removeFromParent();
	}

	//Remove global annotations
	M.getGlobalList().erase(global_annos);
	for (auto a_s : anno_strings) {
		a_s->eraseFromParent();
	}

	if (auto default_behavior = M.getNamedGlobal(default_global)) {
		default_behavior->eraseFromParent();
	}
}

void dataflowProtection::removeOrigFunctions() {
	for (auto F : origFunctions) {
		if (fnsToCloneAndSkip.find(F)==fnsToCloneAndSkip.end()) {
			/*
			 * If not all of the uses are gone, then this function likely is called from within
			 * and without the Scope Of Replication (SOR). We'll keep it around in that case
			 */
			if (F->use_empty()) {
				F->eraseFromParent();
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
			// skip removing globals marked as volatile
			// it's possible the same feature could be implemented by marking variables with
			//  the attribute "used", instead of an annotation
			continue;
		} else if (g.getNumUses() == 0) {
			StringRef gName = g.getName();
			//Don't touch ISR related variables
			if (!(gName.startswith("llvm") || gName.startswith("__vector") || gName.startswith("isr_"))) {
				unusedGlobals.push_back(&g);
			}
		} else if (g.getNumUses() == 1) {
			for (auto u : g.users()) {
				if (Instruction* UI = dyn_cast<Instruction>(u)) {
					//If it's in a function marked as __attribute__((used)), then skip this
					Function* parentF = UI->getParent()->getParent();
					if (usedFunctions.find(parentF) != usedFunctions.end()) {
						continue;
					}
				}
				//Account for instructions that will be cleaned up at the end of the pass
				//it could also be a call instruction to a library function that has side effects, but
				// we ignore the return value
				if ( (u->getNumUses() == 0) && !isa<StoreInst>(u) && !isa<CallInst>(u) && !isa<InvokeInst>(u)) {
					unusedGlobals.push_back(&g);
				}
			}
		}
	}

	for (auto ug : unusedGlobals) {
		if (verboseFlag) {
			errs() << "Removing unused global: " << ug->getName() << "\n";
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
			//Store instructions aren't cloned
			if (isa<StoreInst>(clone))
				continue;

			//If the original isn't used, the clone will not be either
			if (orig->getNumUses() == 0)
				continue;

			//Used only in a single external function call, eg printf
			if (orig->hasOneUse() && isa<CallInst>(orig->user_back())) {
				if (CallInst* CI = dyn_cast<CallInst>(orig->user_back())) {
					if (isIndirectFunctionCall(CI, "checkForUnusedClones"))
						continue;
					else if (CI->getCalledFunction()->hasExternalLinkage())
						continue;
				}
			}

			//If original is only used in external function calls
			if (Instruction* inst = dyn_cast<Instruction>(orig)) {
				//accumulator - proof by contradiction
				bool allExternal = true;
				for (auto u : inst->users()) {
					if (CallInst* ci = dyn_cast<CallInst>(u)) {
						//make sure we're not calling a function on a null pointer
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
				if(allExternal) continue;

				// sometimes clones are erroneously created when the instructions were supposed to be skipped
				if (instsToSkip.find(inst) != instsToSkip.end()) {
					if (verboseFlag) errs() << "Removing unused local variable: " << *inst << "\n";
					inst->eraseFromParent();

					if (TMR) {
						Instruction* inst2 = dyn_cast<Instruction>(cloneM.second.second);
						if (verboseFlag) errs() << "Removing unused local variable: " << *inst2 << "\n";
						inst2->eraseFromParent();
					}
				}

				//TODO: add here, also when function calls are supposed to be skipped
			}

			//Global duplicated strings aren't used in uncloned printfs. Remove the unused clones
			if (ConstantExpr* ce = dyn_cast<ConstantExpr>(clone)) {
				if(verboseFlag) errs() << "Removing unused global string: " << *ce << "\n";
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

			//If using noMemDuplicationFlag then don't worry about unused arguments
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
// Cloning utilities
//----------------------------------------------------------------------------//
bool dataflowProtection::willBeSkipped(Instruction* I){
	return instsToSkip.find(I) != instsToSkip.end();
}

bool dataflowProtection::willBeCloned(Value* v) {
	Instruction* I = dyn_cast<Instruction>(v);
	if (I) {
		return instsToClone.find(I) != instsToClone.end();
	}

	GlobalVariable* g = dyn_cast<GlobalVariable>(v);
	if (g) {
		return globalsToClone.find(g) != globalsToClone.end();
	}

	ConstantExpr* e = dyn_cast<ConstantExpr>(v);
	if (e) {
		return constantExprToClone.find(e) != constantExprToClone.end();
	}

	if (Argument* a = dyn_cast<Argument>(v)) {
		Function * f = a->getParent();
		return fnsToClone.find(f) != fnsToClone.end();
	}	

	return false;
}

bool dataflowProtection::isCloned(Value * v) {
	return cloneMap.find(v) != cloneMap.end();
}

ValuePair dataflowProtection::getClone(Value* I) {
	if (cloneMap.find(I) == cloneMap.end()){
		return ValuePair(I,I);
	} else
		return cloneMap[I];
}

//helper function
//#define DEBUG_INST_MOVING
void dataflowProtection::moveClonesToEndIfSegmented(Module & M){
	if (InterleaveFlag)
		return;

#ifdef DEBUG_INST_MOVING
	int flag = 0;
#endif
	for (auto F : fnsToClone) {
		for (auto & bb : *F) {

#ifdef DEBUG_INST_MOVING
			if (bb.getName() == "entry" && F->getName() == "main") {
				flag = 1;
			}

			if (flag) {
				errs() << F->getName() << "\n";
				errs() << bb << "\n";
			}
#endif

			//Populate list of things to move before
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
						;	//don't add intrinsics, because they will be expanded underneath (in assembly)
							// to be a series of inline instructions, not an actual call
					}
					else {
//						errs() << "    Move point at CI " << I << "\n";
						movePoints.push(&I);
					}
				} else if(TerminatorInst* TI = dyn_cast<TerminatorInst>(&I)) {
					if (isSyncPoint(TI)) {
//						errs() << "    Move point at TI sync " << *startOfSyncLogic[&I] << "\n";
						movePoints.push(startOfSyncLogic[&I]);
					} else {
//						errs() << "    Move point at TI" << I << "\n";
						movePoints.push(&I);
					}
				} else if(StoreInst* SI = dyn_cast<StoreInst>(&I)) {
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
#ifdef FIX_STORE_SEGMENTING
					/* There is a case where we need to keep the stores next to each other, as in the
					 * load-increment-store pattern.  For StoreInst's which aren't syncpoints, this would
					 * cause the variable to be incremented twice.  Check for if it has a clone and if
					 * the type being stored is not a pointer. */
					else if (isStoreMovePoint(SI)) {
						movePoints.push(&I);
					}
#endif
				} else if(GetElementPtrInst* GI = dyn_cast<GetElementPtrInst>(&I)) {
					if (isSyncPoint(GI)) {
						//not all GEP syncpoints have a corresponding entry in the map
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

			//Move all clones before the sync points
			for(auto & I : bb) {
#ifdef DEBUG_INST_MOVING
				if (flag) {
					errs() << I << "\n";
				}
#endif
				//see if it's a clone
				if (PHINode* PN = dyn_cast<PHINode>(&I)) {
					//don't move it, phi nodes must be at the start
				} else if ( (getClone(&I).first != &I) && !(isSyncPoint(&I))
#ifdef FIX_STORE_SEGMENTING
						&& !(isStoreMovePoint(dyn_cast<StoreInst>(&I)))
#endif
						&& !(isCallMovePoint(dyn_cast<CallInst>(&I)))
						/* could also check if it's the head of the list */
				) {
					Instruction* cloneI1 = dyn_cast<Instruction>(getClone(&I).first);
					listI1.push_back(cloneI1);
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

					movePoints.pop();

#ifdef DEBUG_INST_MOVING
					if (flag) {
						errs() << bb << "\n";
					}
#endif
				}
			}


			//Move all sync logic to before the branch
			if (!TMR || ReportErrorsFlag) {
				if (syncCheckMap.find(&bb) != syncCheckMap.end()) { //If block has been split

					Instruction* cmpInst = syncCheckMap[&bb]; //Get instruction block split on
					assert(cmpInst && "Block split and the cmpInst stuck around");
					cmpInst->moveBefore(cmpInst->getParent()->getTerminator());

					if (syncHelperMap.find(&bb) != syncHelperMap.end()) { //Move logic before it
						for (auto I : syncHelperMap[&bb]) {
							assert(I && "Moving valid instructions\n");
							I->moveBefore(cmpInst);
						}
					}

					//if there are SIMD instructions, need to move the special compare operators
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
	//If we've already deleted this function from the list
	if (functionList.find(F)==functionList.end())
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

//TODO: this is not sound logic
bool dataflowProtection::isISR(Function& F) {
	bool ans = F.getName().endswith("ISR") || F.getName().endswith("isr");
	return ans;
}

//----------------------------------------------------------------------------//
// Synchronization utilities
//----------------------------------------------------------------------------//
bool dataflowProtection::isSyncPoint(Instruction* I) {
	if (isa<StoreInst>(I) || isa<CallInst>(I) || isa<TerminatorInst>(I) || isa<GetElementPtrInst>(I))
		return std::find(syncPoints.begin(), syncPoints.end(), I) != syncPoints.end();
	else
		return false;
}

#ifdef FIX_STORE_SEGMENTING
bool dataflowProtection::isStoreMovePoint(StoreInst* SI) {
	if ( 	(getClone(SI).first == SI) ||						/* Doesn't have a clone */
			(SI->getOperand(0)->getType()->isPointerTy()) ||	/* Storing a pointer type */
			(dyn_cast<PtrToIntInst>(SI->getOperand(0))) ) 		/* Casted pointer */
	{
		return false;
	}
	// otherwise, we need to segment them together
	else
		return true;
}
#endif

bool dataflowProtection::isCallMovePoint(CallInst* ci) {
	if ( (getClone(ci)).first == ci) {
		return false;
	} else {
		return true;
	}
}

/*
 * returns true if this will try to sync on a coarse-grained function return value
 * these should be avoided for things like the case of malloc()
 * if returns false, then it's OK to sync on the value
 */
bool dataflowProtection::checkCoarseSync(StoreInst* inst) {
	//need to check for if this value came from a replicated function call
	Value* op0 = inst->getOperand(0);
	if (CallInst* CI = dyn_cast<CallInst>(op0)) {
		Function* calledF = CI->getCalledFunction();
		if (calledF && (std::find(coarseGrainedUserFunctions.begin(), coarseGrainedUserFunctions.end(),
				calledF->getName()) != coarseGrainedUserFunctions.end()) ) {
			//then we've got a coarse-grained value
			return true;
		}
	} else if (InvokeInst* II = dyn_cast<InvokeInst>(op0)) {
		Function* calledF = II->getCalledFunction();
		if (calledF && (std::find(coarseGrainedUserFunctions.begin(), coarseGrainedUserFunctions.end(),
				calledF->getName()) != coarseGrainedUserFunctions.end()) ) {
			//again
			return true;
		}
	}
	return false;
}

//visit all uses of an instruction and see if they are also instructions to add to clone list
void dataflowProtection::walkInstructionUses(Instruction* I, bool xMR) {

	//add it to clone or skip list, depending on annotation, passed through argument xMR
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

			//should we add it to the list?
			if (phiInst) {
				;
			} else if (CI) {
				//skip all call instructions for now
				;
			} else if (TerminatorInst* TI = dyn_cast<TerminatorInst>(instUse)) {
				//this should become a syncpoint
				// really? needs more testing
//				if (xMR) syncPoints.push_back(instUse);
			} else if (SI && (noMemReplicationFlag) ) {
				//don't replicate store instructions if flags
				//also, this will become a syncpoint
//				if (xMR) syncPoints.push_back(instUse);
			} else {
				addSet->insert(instUse);
//				errs() << *instUse << "\n";
			}

			//should we visit its uses?
			//as long as it has more than 1 uses
			if ( (instUse->getNumUses() > 0) && !phiInst) {
				//recursive call
				walkInstructionUses(instUse, xMR);
			}

		}
	}
}

/*
 * verify that all of the options used to configure COAST for this pass are safe to follow
 */
void dataflowProtection::verifyOptions(Module& M) {
	std::map< GlobalVariable*, std::set<Function*> > glblFnMap;

	// check that the globals being cloned are only used in protected functions
	for (auto g : globalsToClone) {
		// get all the users
		for (auto u : g->users()) {
			// is it an instruction?
			if (Instruction* UI = dyn_cast<Instruction>(u)) {
				Function* parentF = UI->getParent()->getParent();

				// have we been asked to skip it?
				if (globalCrossMap.find(g) != globalCrossMap.end()) {
					if (globalCrossMap[g] == parentF) {
						// skip if it's the marked function
						continue;
					}
				}

				// is the instruction in a protected function?
				if (fnsToClone.find(parentF) == fnsToClone.end()) {
					if (glblFnMap.find(g) == glblFnMap.end()) {
						std::set<Function*> tempSet;
						glblFnMap[g] = tempSet;
					}

					glblFnMap[g].insert(parentF);
				}

			}
		}
	}

	// print warning messages
	for (auto item : glblFnMap) {
		errs() << err_string << " global \"" << item.first->getName() << "\"\n\tused in functions: ";
		for (auto fns : item.second) {
			errs() << "\"" << fns->getName() << "\", ";
		}
		errs() << "\nwhich are not protected\n";
	}

	if (glblFnMap.size() > 0) {
		std::exit(-1);
	}

}


void dataflowProtection::updateFnWrappers(Module& M) {
	std::string wrapperFnEnding = "_COAST_WRAPPER";
	// have to create a map and edit afterwards; editing users while iterating over them is a bad idea
	std::map<Function*, Function*> wrapperMap;
	std::set<Function*> wrapperFns;

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

			// find all CallInsts with target of fnName function
//			for (auto u : fn.users()) {
//				if (CallInst* uc = dyn_cast<CallInst>(u)) {
//					wrapperMap[uc] = normalFn;
//				} else if (BitCastInst* bc = dyn_cast<BitCastInst>(u)) {
//					wrapperMap[uc] = normalFn;
//				}
//			}
		}
	}

	for (auto &fn : M) {
		for (auto &bb: fn) {
			for (auto &I : bb) {

				//look for call instructions
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
								instsToCloneAnno.insert(ci);
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
