// This file contains utilities that help set up or clean up after the pass

#include "dataflowProtection.h"

#include <queue>
#include <list>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <llvm/IR/Module.h>
#include "llvm/Support/CommandLine.h"
#include <llvm/Support/Debug.h>
#include <llvm/Support/raw_ostream.h>

#include <llvm/IR/Constants.h>
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

//also, there are some functions that are not supported
//it is in here instead of the config file because we don't want users touching it
std::list<std::string> unsupportedFunctions = {"fscanf", "scanf", "fgets", "gets", "sscanf", "__isoc99_fscanf"};

using namespace llvm;

//----------------------------------------------------------------------------//
// Miscellaneous
//----------------------------------------------------------------------------//
bool dataflowProtection::isIndirectFunctionCall(CallInst* CI, std::string errMsg, bool print){
	//This partially handles bitcasts and other inline LLVM functions
	if(CI->getCalledFunction() == nullptr){
		if(print || verboseFlag)
			errs() << warn_string << " in " << errMsg << " skipping:\n\t" << *CI << "\n";
		return true;
	}else{
		return false;
	}
}

void dataflowProtection::getFunctionsFromCL(){
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
int dataflowProtection::getFunctionsFromConfig(){
	std::string filename;
	if(configFileLocation!=""){
		filename = configFileLocation;
	} else{
		std::string coast = std::getenv("COAST_ROOT");
		filename = coast + "/projects/dataflowProtection/functions.config";
	}
	std::ifstream ifs(filename, std::ifstream::in);

	if(!ifs.is_open()){
		errs() << "ERROR: No configuration file found at " << filename << '\n';
		errs() << "         Please pass one in using -configFile\n";
		return 0;
	}

	std::list<std::string>* lptr;
	std::string line;
	while(getline(ifs, line)){
		if(line.length() == 0){  //Blank line
			continue;
		}

		if(line[0] == '#'){ //# is the comment symbol
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
		while(iss.good()){
			getline(iss, substr, ',');
			if(substr.length() == 0)
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
void dataflowProtection::dumpModule(Module& M){
	if(!dumpModuleFlag)
		return;

	for(GlobalVariable& g : M.getGlobalList()){
			errs() << g << "\n";
	}
	errs() << "\n";
	for(auto &f : M){
		errs() << f << "\n";
	}
}

//----------------------------------------------------------------------------//
// Initialization code
//----------------------------------------------------------------------------//
void dataflowProtection::removeUnusedFunctions(Module& M) {
	//Populate a list of all functions in the module
	std::set<Function*> functionList;
	for(auto & F : M){
		//Ignore external function declarations
		if(F.hasExternalLinkage() && F.isDeclaration()){
			continue;
		}

		//Don't erase fault handlers
		if(F.getName().startswith("FAULT_DETECTED_")){
			continue;
		}

		//Don't erase ISRs
		if(isISR(F))
			continue;

		if(F.getNumUses() != 0)
			continue;

		functionList.insert(&F);
	}

	Function* mainFunction = M.getFunction("main");
	if(!mainFunction) { //If we don't have a main, don't remove any
		return;
	}

	recursivelyVisitCalls(M,mainFunction,functionList);

	if(functionList.size() == 0){
		return;
	}

	if(functionList.size()>0)
		if(verboseFlag) errs() << "The following functions are unused, removing them: \n";
	for(auto q : functionList){
		assert(fnsToClone.find(q)==fnsToClone.end() && "The specified function is not called, so is being removed");
		if(verboseFlag) errs() << "    " << q->getName() << "\n";
		q->eraseFromParent();
	}

}

void dataflowProtection::processCommandLine(Module& M, int numClones){
	if(InterleaveFlag == SegmentFlag){
		SegmentFlag = true;
	}
	TMR = (numClones==3);

	if(noMemReplicationFlag && noStoreDataSyncFlag){
		errs() << "WARNING: noMemDuplication and noStoreDataSync set simultaneously. Recommend not setting the two together.\n";
	}

	//copy command line lists to internal lists
	getFunctionsFromCL();

//	errs() << "Content of skipLibCalls (before loading config):\n";
//	for(auto li : skipLibCalls){
//		errs() << li << "\n";
//	}

	if(getFunctionsFromConfig()){
		assert("Configuration file error!" && false);
	}

//	errs() << "Content of skipLibCalls (after loading config):\n";
//	for(auto li : skipLibCalls){
//		errs() << li << "\n";
//	}

	if(skipFn.size() == 0){
		for (auto & fn_it : M){

			if (fn_it.isDeclaration()) { //Ignore library calls
				continue;
			}

			if(isISR(fn_it)){ //Don't erase ISRs
				continue;
			}

			if(xMR_default)
				fnsToClone.insert(&fn_it);
		}
	} else {
		for (auto fcn : skipFn) {
			Function* f = M.getFunction(StringRef(fcn));
			if(!f){
				errs() << "\nERROR:Specified function does not exist!\n";
				errs() << "Check the spelling, check if the optimizer inlined it\n\n";
				assert(f);
			}
			fnsToSkip.insert(f);
		}
	}

}

void dataflowProtection::processAnnotations(Module& M){
	//Inspired by http://bholt.org/posts/llvm-quick-tricks.html
	auto global_annos = M.getNamedGlobal("llvm.global.annotations");
	if(global_annos){
		auto a = cast<ConstantArray>(global_annos->getOperand(0));
		for(int i=0; i < a->getNumOperands(); i++){
			auto e = cast<ConstantStruct>(a->getOperand(i));

			auto anno = cast<ConstantDataArray>(cast<GlobalVariable>(e->getOperand(1)->getOperand(0))->getOperand(0))->getAsCString();

			//Function annotations
			if(auto fn = dyn_cast<Function>(e->getOperand(0)->getOperand(0))){
				if(anno == no_xMR_anno) {
					if(verboseFlag) errs() << "Directive: do not clone function '" << fn->getName() << "'\n";
					fnsToSkip.insert(fn);
					if(fnsToClone.find(fn)!=fnsToClone.end())
						fnsToClone.erase(fn);
				} else if(anno == xMR_anno) {
					if(verboseFlag) errs() << "Directive: clone function '" << fn->getName() << "'\n";
					fnsToClone.insert(fn);
				} else if(anno == xMR_call_anno){
					if(verboseFlag) errs() << "Directive: replicate calls to function '" << fn->getName() << "'\n";
					coarseGrainedUserFunctions.push_back(fn->getName());
				} else {
					assert(false && "Invalid option on function");
				}

			}
			//Global annotations
			else if(auto gv = dyn_cast<GlobalVariable>(e->getOperand(0)->getOperand(0))){
				if(anno == no_xMR_anno) {
					if(verboseFlag) errs() << "Directive: do not clone global variable '" << gv->getName() << "'\n";
					globalsToSkip.insert(gv);
				} else if(anno == xMR_anno) {
					if(verboseFlag) errs() << "Directive: clone global variable '" << gv->getName() << "'\n";
					globalsToClone.insert(gv);
				} else if(anno==default_xMR){
					if(verboseFlag) errs() << "Directive: set xMR as default\n";
				} else if(anno==default_no_xMR){
					if(verboseFlag) errs() << "Directive: set no xMR as default\n";
					xMR_default = false;
				} else {
					if(verboseFlag) errs() << "Directive: " << anno << "\n";
					assert(false && "Invalid option on global value");
				}
			}
			else{
				assert(false && "Non-function annotation");
			}
		}
	}

	//Local variables
	for(auto &F : M){
		for(auto &bb : F){
			for(auto &I : bb){
				if( auto CI = dyn_cast<CallInst>(&I) ){
					// have to skip any bitcasts in function calls because they aren't actually a function
					if(isIndirectFunctionCall(CI, "processAnnotations"))
						continue;
					if(CI->getCalledFunction()->getName() == "llvm.var.annotation"){
						//Get variable
						auto adr = dyn_cast<BitCastInst>(CI->getOperand(0));
						auto var = dyn_cast<AllocaInst>(adr->getOperand(0));

						auto ce = dyn_cast<ConstantExpr>(CI->getOperand(1));
						auto gv  = dyn_cast<GlobalVariable>(ce->getOperand(0));
						auto anno = dyn_cast<ConstantDataArray>(gv->getInitializer())->getAsCString();

						if(var){
							if(anno == no_xMR_anno){
								if(verboseFlag) errs() << "Directive: do not clone local variable '" << *var << "'\n";
								instsToSkip.insert(var);
							} else if(anno == xMR_anno){
								if(verboseFlag) errs() << "Directive: clone local variable '" << *var << "'\n";
								instsToClone.insert(var);
							} else{
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
void dataflowProtection::removeAnnotations(Module& M){
	auto global_annos = M.getNamedGlobal("llvm.global.annotations");
	if(!global_annos)
		return;

	std::set<GlobalVariable*> anno_strings;

	//Populate a list of global strings that are only used in annotations
	auto a = cast<ConstantArray>(global_annos->getOperand(0));
	for(int i=0; i < a->getNumOperands(); i++){
		auto e = cast<ConstantStruct>(a->getOperand(i)); //This is part of global_anno
		auto anno = cast<GlobalVariable>(e->getOperand(1)->getOperand(0)); //This is the global string

		for(int j=0; j < e->getNumOperands(); j++){
			if(e->getOperand(j)->getNumOperands() >= 1){
				if(auto cs = dyn_cast<GlobalVariable>(e->getOperand(j)->getOperand(0))){
					if(cs->getSection() == "llvm.metadata"){
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
				if(auto CI = dyn_cast<CallInst>(&I)){
					auto called = CI->getCalledFunction();
					if(called->getName() == "llvm.var.annotation"){
						lva = called;
						toRemove.insert(CI);
					}
				}
			}
		}
	}

	for(auto rm : toRemove){
		auto op0 = dyn_cast<Instruction>(rm->getOperand(0));
		rm->getParent()->getInstList().erase(rm);
		if(op0)
			op0->getParent()->getInstList().erase(op0);
	}

	if(lva){
		lva->removeFromParent();
	}

	//Remove global annotations
	M.getGlobalList().erase(global_annos);
	for(auto a_s : anno_strings){
		a_s->eraseFromParent();
	}

	if(auto default_behavior = M.getNamedGlobal(default_global)){
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

void dataflowProtection::removeUnusedGlobals(Module& M){
	std::vector<GlobalVariable*> unusedGlobals;

	for (GlobalVariable & g : M.getGlobalList()) {
		if (g.getNumUses() == 0) {
			StringRef gName = g.getName();
			//Don't touch ISR related variables
			if (!(gName.startswith("llvm") || gName.startswith("__vector") || gName.startswith("isr_"))) {
				unusedGlobals.push_back(&g);
			}
		} else if (g.getNumUses() == 1) {
			for (auto u : g.users()) {
				//Account for instructions that will be cleaned up at the end of the pass
				//it could also be a call instruction to a library function that has side effects, but
				// we ignore the return value
				if (u->getNumUses() == 0 && !isa<StoreInst>(u) && !isa<CallInst>(u)) {
					unusedGlobals.push_back(&g);
				}
			}
		}
	}

	for (auto ug : unusedGlobals) {
		if (verboseFlag) {
			errs() << "Removing unused global: " << ug->getName() << "\n";
		}
		ug->eraseFromParent();
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
				if (CallInst* CI = dyn_cast<CallInst>(orig->user_back())){
					if(isIndirectFunctionCall(CI, "checkForUnusedClones"))
						continue;
					else if(CI->getCalledFunction()->hasExternalLinkage())
						continue;
				}
			}

			//If original is only used in external function calls
			if(Instruction* inst = dyn_cast<Instruction>(orig)){
				//accumulator - proof by contradiction
				bool allExternal = true;
				for(auto u : inst->users()){
					if (CallInst* ci = dyn_cast<CallInst>(u)){
						//make sure we're not calling a function on a null pointer
						if(isIndirectFunctionCall(ci, "checkForUnusedClones"))
							continue;
						else if(ci->getCalledFunction()->hasExternalLinkage())
							continue;
						else{
							allExternal = false;
							break;
						}
					}
				}
				if(allExternal) continue;
			}

			//Global duplicated strings aren't used in uncloned printfs. Remove the unused clones
			if (ConstantExpr* ce = dyn_cast<ConstantExpr>(clone)) {
				if(verboseFlag) errs() << "Removing unused global string: " << *ce << "\n";
				ce->destroyConstant();
				if(TMR){
					ConstantExpr* ce2 = dyn_cast<ConstantExpr>(cloneM.second.second);
					if(verboseFlag) errs() << "Removing unused global string: " << *ce2 << "\n";
					ce2->destroyConstant();
				}
				continue;
			}

			if (GlobalVariable* GV = dyn_cast<GlobalVariable>(clone)) {
				continue;
			}

			//If using noMemDuplicationFlag then don't worry about unused arguments
			if(noMemReplicationFlag){
				if(dyn_cast<Argument>(orig)){
					continue;
				}
			}

			errs() << "ERROR when updating cloned instructions.\n";
			errs() << "More about " << *clone << ":\n";
			errs() << "  Orig:" << *orig << "\n";
			errs() << "  Orig has " << orig->getNumUses() << " uses\n";
			Instruction* tmp = dyn_cast<Instruction>(orig->user_back());
			errs() << "      " << *orig->user_back() << " in " << tmp->getParent()->getName() << "\n";
			errs() << "\n" << *clone << " has no users\n\n";
			assert(false && "Clone has no users");
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
	if(I){ 
		return instsToClone.find(I) != instsToClone.end();
	}

	GlobalVariable* g = dyn_cast<GlobalVariable>(v);
	if(g){
		return globalsToClone.find(g) != globalsToClone.end();
	}

	ConstantExpr* e = dyn_cast<ConstantExpr>(v);
	if(e){
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

void dataflowProtection::moveClonesToEndIfSegmented(Module & M){
	if(InterleaveFlag)
		return;

	for(auto F : fnsToClone) {
		for (auto & bb : *F) {

			//Populate list of things to move before
			std::queue<Instruction*> movePoints;
			for(auto &I : bb){
				if(CallInst* CI = dyn_cast<CallInst>(&I)){
					/* Fixed an issue where the clone was considered a syncPoint, but wasn't
					 * in the startOfSyncLogic map, so it was inserting a new element and
					 * putting in the default Instruction* value (whatever that is) into the
					 * movePoints map
					*/
					if(isSyncPoint(CI) && (startOfSyncLogic.find(&I) != startOfSyncLogic.end()) ){
//						errs() << "    Move point at CI sync" << *startOfSyncLogic[&I] << "\n";
						movePoints.push(startOfSyncLogic[&I]);
					} else{
//						errs() << "    Move point at CI " << I << "\n";
						movePoints.push(&I);
					}
				} else if(TerminatorInst* TI = dyn_cast<TerminatorInst>(&I)){
					if(isSyncPoint(TI)){
//						errs() << "    Move point at TI sync " << *startOfSyncLogic[&I] << "\n";
						movePoints.push(startOfSyncLogic[&I]);
					} else{
//						errs() << "    Move point at TI" << I << "\n";
						movePoints.push(&I);
					}
				} else if(StoreInst* SI = dyn_cast<StoreInst>(&I)){
					if(isSyncPoint(SI)){
//						errs() << "    Move point at SI" << *startOfSyncLogic[&I] << "\n";
#ifdef SYNC_POINT_FIX
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
						} else {
							movePoints.push(&I);
						}
#else					//old stuff
						movePoints.push(startOfSyncLogic[&I]);
#endif
					}
				} else if(GetElementPtrInst* GI = dyn_cast<GetElementPtrInst>(&I)){
#ifdef SYNC_POINT_FIX
					if (isSyncPoint(GI)) {
						//not all GEP syncpoints have a corresponding entry in the map
						if ( (startOfSyncLogic.find(&I) != startOfSyncLogic.end() ) &&
							 (startOfSyncLogic[&I]->getParent() == I.getParent()) ) {
							movePoints.push(startOfSyncLogic[&I]);
						} else {
							movePoints.push(&I);
						}
					}
#else
					if (isSyncPoint(GI)) {
//						errs() << "    Source instruct:" << I << "\n";
//						errs() << "    Move point at GI" << *startOfSyncLogic[&I] << "\n";
						movePoints.push(startOfSyncLogic[&I]);
					}
#endif
				}
			}

			std::vector<Instruction*> listI1;
			std::vector<Instruction*> listI2;

			//Move all clones before the sync points
			for(auto & I : bb) {
				//see if it's a clone
				if(PHINode* PN = dyn_cast<PHINode>(&I)){
					//don't move it, phi nodes must be at the start
#ifdef SYNC_POINT_FIX
				} else if ( (getClone(&I).first != &I) && !(isSyncPoint(&I)) ) {
#else
				} else if (getClone(&I).first != &I) {
#endif
					Instruction* cloneI1 = dyn_cast<Instruction>(getClone(&I).first);
					listI1.push_back(cloneI1);
					if(TMR){
						Instruction* cloneI2 = dyn_cast<Instruction>(getClone(&I).second);
						listI2.push_back(cloneI2);
					}

#ifdef SYNC_POINT_FIX
				}
				// this is a separate condition, not dependent on it being a cloned instruction
				if(&I == movePoints.front()){
#else
				} else if(&I == movePoints.front()){
#endif
					Instruction* inst = movePoints.front();
					for(auto it : listI1){
						it->moveBefore(movePoints.front());
					}
					listI1.clear();

					for(auto it2 : listI2){
						it2->moveBefore(movePoints.front());
					}
					listI2.clear();

					movePoints.pop();

				}
			}


			//Move all sync logic to before the branch
			if(!TMR || ReportErrorsFlag){
				if(syncCheckMap.find(&bb) != syncCheckMap.end()){ //If block has been split

					Instruction* cmpInst = syncCheckMap[&bb]; //Get instruction block split on
					assert(cmpInst && "Block split and the cmpInst stuck around");
					cmpInst->moveBefore(cmpInst->getParent()->getTerminator());

					if(syncHelperMap.find(&bb) != syncHelperMap.end()){ //Move logic before it
						for(auto I : syncHelperMap[&bb]){
							assert(I && "Moving valid instructions\n");
							I->moveBefore(cmpInst);
						}
					}

					//if there are SIMD instructions, need to move the special compare operators
					if(simdMap.find(cmpInst) != simdMap.end()){
						std::get<0>(simdMap[cmpInst])->moveBefore(cmpInst->getParent()->getTerminator());
						std::get<1>(simdMap[cmpInst])->moveBefore(cmpInst->getParent()->getTerminator());
						std::get<2>(simdMap[cmpInst])->moveBefore(cmpInst->getParent()->getTerminator());
					}
				}
			}
			//cleanup for some things
			//in case didn't get cleared earlier
			listI1.clear();
			listI2.clear();
			//empty the queue
			while (!movePoints.empty())
				movePoints.pop();

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

void dataflowProtection::recursivelyVisitCalls(Module& M, Function* F, std::set<Function*> &functionList){
	//If we've already deleted this function from the list
	if(functionList.find(F)==functionList.end())
		return;

	functionList.erase(F);

	for(auto & bb : *F){
		for(auto & I : bb){
			if(CallInst* CI = dyn_cast<CallInst>(&I)){
				recursivelyVisitCalls(M,CI->getCalledFunction(),functionList);
			}
		}
	}

}

bool dataflowProtection::isISR(Function& F){
	bool ans = F.getName().endswith("ISR") || F.getName().endswith("isr");
	return ans;
}

//----------------------------------------------------------------------------//
// Synchronization utilities
//----------------------------------------------------------------------------//
bool dataflowProtection::isSyncPoint(Instruction* I){
	if(isa<StoreInst>(I) || isa<CallInst>(I) || isa<TerminatorInst>(I) || isa<GetElementPtrInst>(I))
		return std::find(syncPoints.begin(), syncPoints.end(), I) != syncPoints.end();
	else
		return false;
}

