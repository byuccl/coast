/*
 * interface.cpp
 *
 * This file contains functions that deal with the interface into COAST.
 * This includes command line options and in-code directives.
 */

#include "dataflowProtection.h"

// standard library includes
#include <string>
#include <list>
#include <fstream>
#include <sstream>

// LLVM includes
#include <llvm/Option/Option.h>
#include <llvm/IR/Module.h>
#include "llvm/Support/CommandLine.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>

using namespace llvm;


// Shared variables
extern cl::list<std::string> skipFnCl;
extern cl::list<std::string> skipLibCallsCl;
extern cl::list<std::string> replicateUserFunctionsCallCl;
extern cl::list<std::string> ignoreGlblCl;
extern cl::list<std::string> globalsToRuntimeInitCl;
extern cl::list<std::string> isrFunctionListCl;
extern cl::list<std::string> cloneFnCl;
extern cl::list<std::string> cloneGlblCl;
extern cl::list<std::string> replReturnCl;
extern cl::list<std::string> cloneAfterCallCl;
extern cl::list<std::string> protectedLibCl;

extern cl::opt<std::string> configFileLocation;
extern cl::opt<bool> SegmentFlag;
extern cl::opt<bool> storeDataSyncFlag;
extern cl::opt<bool> noStoreDataSyncFlag;
extern cl::opt<bool> InterleaveFlag;
extern cl::opt<bool> noMemReplicationFlag;
extern cl::opt<bool> verboseFlag;

extern std::string tmr_global_count_name;


// New global lists to be used to track function names
std::list<std::string> skipFn;
std::list<std::string> skipLibCalls;
std::list<std::string> coarseGrainedUserFunctions;
std::list<std::string> ignoreGlbl;
std::list<std::string> clGlobalsToRuntimeInit;
std::list<std::string> isrFuncNameList;
std::list<std::string> tempCloneFnList;
std::list<std::string> tempCloneGlblList;
std::list<std::string> tempReplReturnList;
std::list<std::string> cloneAfterCallList;
std::list<std::string> tempProtectedLibList;
std::map<Function*, std::set<int> > noXmrArgList;
// see removeAnnotations()
std::set<ConstantExpr*> annotationExpressions;


// These are the names of the above CL lists.
// Any changes to these must also be changed at the head of dataflowProtection.cpp
const std::string skipFnName = "ignoreFns";
const std::string ignoreGlblName = "ignoreGlbls";
const std::string skipLibCallsName = "skipLibCalls";
const std::string coarseFnsName = "replicateFnCalls";
const std::string runtimeGlblInitName = "runtimeInitGlobals";
const std::string isrFuncListString = "isrFunctions";
const std::string cloneAfterCallString = "cloneAfterCall";

// track functions that we should ignore invalid SOR crossings
extern std::map<GlobalVariable*, std::set<Function*> > globalCrossMap;


// Copy all (fixed) things from the command line to the internal, editable lists
void dataflowProtection::getFunctionsFromCL() {
	// The order these lists are parsed in is pretty much reverse priority
	//  if names show up in multiple lists

	for (auto x : skipLibCallsCl) {
		if (verboseFlag)
			errs() << "CL: do not replicate calls to function '" << x << "'\n";
		skipLibCalls.push_back(x);
	}

	for (auto x : skipFnCl) {
		if (verboseFlag)
			errs() << "CL: do not clone function '" << x << "'\n";
		skipFn.push_back(x);
	}

	for (auto x : replicateUserFunctionsCallCl) {
		if (verboseFlag)
			errs() << "CL: replicate calls to function '" << x << "'\n";
		coarseGrainedUserFunctions.push_back(x);
		// check if it needs to be removed from a skipping list
		if (std::find(skipLibCalls.begin(), skipLibCalls.end(), x) != skipLibCalls.end()) {
			skipLibCalls.remove(x);
		}
	}

	for (auto x : ignoreGlblCl) {
		if (verboseFlag)
			errs() << "CL: do not clone global variable '" << x << "'\n";
		ignoreGlbl.push_back(x);
	}

	for (auto x : globalsToRuntimeInitCl) {
		clGlobalsToRuntimeInit.push_back(x);
	}

	for (auto x : isrFunctionListCl) {
		if (verboseFlag)
			errs() << "CL: function '" << x << "' is an ISR\n";
		isrFuncNameList.push_back(x);
	}

	for (auto x : cloneFnCl) {
		if (verboseFlag)
			errs() << "CL: clone function '" << x << "'\n";
		tempCloneFnList.push_back(x);
		// check if it needs to be removed from a skipping list
		if (std::find(skipFn.begin(), skipFn.end(), x) != skipFn.end()) {
			skipFn.remove(x);
		}
	}

	for (auto x : cloneGlblCl) {
		if (verboseFlag)
			errs() << "CL: clone global '" << x << "'\n";
		tempCloneGlblList.push_back(x);
		// check if it needs to be removed from skipping list
		if (std::find(ignoreGlbl.begin(), ignoreGlbl.end(), x) != ignoreGlbl.end()) {
			ignoreGlbl.remove(x);
		}
	}

	for (auto x : replReturnCl) {
		if (verboseFlag)
			errs() << "CL: clone function '" << x << "' return value\n";
		tempReplReturnList.push_back(x);
	}

	for (auto x : cloneAfterCallCl) {
		if (verboseFlag)
			errs() << "CL: clone function '" << x << "' args after call\n";
		cloneAfterCallList.push_back(x);
		// also, don't touch the insides, or make more than one call
		skipLibCalls.push_back(x);
		skipFn.push_back(x);
	}

	for (auto x : protectedLibCl) {
		if (verboseFlag)
			errs() << "CL: treat function '" << x << "' as a protected library\n";
		tempProtectedLibList.push_back(x);
	}
}


/*
 * This function extracts function names from the configuration file
 * The lists it writes to already exist: they were created in dataflowProtection.cpp
 * The return value indicates successor failure.
 */
int dataflowProtection::getFunctionsFromConfig() {
	std::string filename;
	if (configFileLocation != "") {
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
		errs() << "ERROR: No configuration file found at '" << filename << "'\n";
		errs() << "         Please pass one in using -configFile\n";
		return -1;
	}

	std::list<std::string>* lptr;
	std::string line;
	while (getline(ifs, line)) {
		if (line.length() == 0) {  	// Blank line
			continue;
		}

		if (line[0] == '#') { 		// # is the comment symbol
			continue;
		}

		// Remove all whitespace
		line.erase(remove (line.begin(), line.end(), ' '), line.end());

		std::istringstream iss(line);
		std::string substr;
		getline(iss, substr, '=');

		// Find the option we're using
		if (substr == skipLibCallsName) {
			lptr = &skipLibCalls;
		} else if (substr == skipFnName) {
			lptr = &skipFn;
		} else if (substr == coarseFnsName) {
			lptr = &coarseGrainedUserFunctions;
		} else if (substr == ignoreGlblName) {
			lptr = &ignoreGlbl;
		} else if (substr == runtimeGlblInitName) {
			lptr = &clGlobalsToRuntimeInit;
		} else if (substr == isrFuncListString) {
			lptr = &isrFuncNameList;
		} else {
			errs() << "ERROR: unrecognized option '" << substr;
			errs() << "' in configuration file '" << filename << "'\n\n";
			return 1;
		}

		// Insert all options into vector
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

	// Parse information from config file
	if (getFunctionsFromConfig()) {
		assert("Configuration file error!" && false);
	}

	// Copy command line lists to internal lists
	// This should be able to override config file
	getFunctionsFromCL();

	// convert function names to actual pointers
	for (Function & F : M) {
		if (std::find(isrFuncNameList.begin(), isrFuncNameList.end(), F.getName()) != isrFuncNameList.end()) {
			isrFunctions.insert(&F);
		}
		else if (std::find(tempCloneFnList.begin(), tempCloneFnList.end(), F.getName()) != tempCloneFnList.end()) {
			fnsToClone.insert(&F);
		}
		else if (std::find(tempReplReturnList.begin(), tempReplReturnList.end(), F.getName()) != tempReplReturnList.end()) {
			replReturn.insert(&F);
		}
		else if (std::find(cloneAfterCallList.begin(), cloneAfterCallList.end(), F.getName()) != cloneAfterCallList.end()) {
			cloneAfterFnCall.insert(&F);
		}
		else if (std::find(tempProtectedLibList.begin(), tempProtectedLibList.end(), F.getName()) != tempProtectedLibList.end()) {
			protectedLibList.insert(&F);
			// it needs to be added to clone list as well
			fnsToClone.insert(&F);
		}
	}

	// more useful missing function information
	std::set<std::string> missingFuncNames;

	if (skipFn.size() == 0) {
		for (auto & fn_it : M) {

			// Ignore library calls
			if (fn_it.isDeclaration()) {
				continue;
			}

			// Don't erase ISRs
			if (isISR(fn_it)) {
				continue;
			}

			if (xMR_default) {
				if (fnsToSkip.find(&fn_it) == fnsToSkip.end()) {
					// This should yield to the fnsToSkip list, as it
					//  should be fully populated by now
					fnsToClone.insert(&fn_it);
				}
			}
		}
	} else {
		for (auto fcn : skipFn) {
			Function* f = M.getFunction(StringRef(fcn));
			if (!f) {
				// If the name doesn't exist, stick it in a list for later
				// This way, we can report missing ones all at once
				missingFuncNames.insert(fcn);
				continue;
			}
			fnsToSkip.insert(f);
		}
	}

	// Report missing function names from command line
	if (missingFuncNames.size() > 0) {
		errs() << "\n" << err_string << " The following function names do not exist!\n";
		for (auto fcn : missingFuncNames) {
			errs() << "  '" << fcn << "'\n";
		}
		errs() << "Check the spelling, check if the optimizer inlined it, or if the name was mangled\n\n";
		exit(-1);
	}

	// more useful missing global information
	std::set<std::string> missingGlblNames;

	// convert global names to references
	for (auto glblName : tempCloneGlblList) {
		// allowInteral = true; this can then detect static variables
		GlobalVariable* glbl = M.getGlobalVariable(StringRef(glblName), true);
		if (!glbl) {
			missingGlblNames.insert(glblName);
		} else {
			globalsToClone.insert(glbl);
		}
	}

	if (missingGlblNames.size() > 0) {
		errs() << "\n" << err_string 
			   << " The following global variable names do not exist!\n";
		for (auto glblName : missingGlblNames) {
			errs() << "  '" << glblName << "'\n";
		}
		errs() << "Check the spelling, or if the name was mangled\n\n";
		exit(-1);
	}

	// special case
	ignoreGlbl.push_back(tmr_global_count_name);
}

void dataflowProtection::processAnnotations(Module& M) {
	// Inspired by http://bholt.org/posts/llvm-quick-tricks.html
	auto global_annos = M.getNamedGlobal("llvm.global.annotations");
	if (global_annos) {
		auto a = cast<ConstantArray>(global_annos->getOperand(0));
		// check that it is the right type
		if (a) {
			for (int i=0; i < a->getNumOperands(); i++) {
				auto e = cast<ConstantStruct>(a->getOperand(i));

				// extract data
				auto anno = cast<ConstantDataArray>(cast<GlobalVariable>(e->getOperand(1)->getOperand(0))->getOperand(0))->getAsCString();

				// Function annotations
				if (auto fn = dyn_cast<Function>(e->getOperand(0)->getOperand(0))) {
					if (anno == no_xMR_anno) {
						if (verboseFlag) errs() << "Directive: do not clone function '" << fn->getName() << "'\n";
						fnsToSkip.insert(fn);
						if (fnsToClone.find(fn) != fnsToClone.end()) {
							fnsToClone.erase(fn);
						}
					} else if (anno == xMR_anno) {
						if (verboseFlag) errs() << "Directive: clone function '" << fn->getName() << "'\n";
						fnsToClone.insert(fn);
					} else if (anno == xMR_call_anno) {
						if (verboseFlag) errs() << "Directive: replicate calls to function '" << fn->getName() << "'\n";
						coarseGrainedUserFunctions.push_back(fn->getName());
					} else if (anno == skip_call_anno) {
						if (verboseFlag) errs() << "Directive: do not clone calls to function '"  << fn->getName() << "'\n";
						skipLibCalls.push_back(fn->getName());
						// TODO: do we need to worry about duplicates? - make it a set instead
					} else if (anno.startswith("no-verify-")) {
						StringRef global_name = anno.substr(10, anno.size() - 10);

						GlobalValue* glbl = M.getNamedValue(global_name);
						if (glbl) {
							GlobalVariable* glblVar = dyn_cast<GlobalVariable>(glbl);
							if (glblVar) {
								// make sure the set exists already
								if (globalCrossMap.find(glblVar) == globalCrossMap.end()) {
									std::set<Function*> tempSet;
									globalCrossMap[glblVar] = tempSet;
								}
								globalCrossMap[glblVar].insert(fn);
								if (verboseFlag) {
									errs() << "Directive: ignoring global '" << global_name
										   << "' being used in function '" << fn->getName() << "'\n";
								}
							}
						} else {
							errs() << warn_string << " global '" << global_name << "' doesn't exist\n";
						}

					} else if (anno.startswith("no_xMR_arg-")) {
						StringRef argNumStr = anno.substr(11, anno.size() - 11);
						int argNum = std::stoi(argNumStr.str());
						// argNumStr.getAsInteger(10, &argNum);

						if (argNum >= fn->getFunctionType()->params().size()) {
							errs() << warn_string << " index '" << argNum
								   << "' is greater than the number of operands in function '"
								   << fn->getName() << "'\n";
							// Don't exit
							// std::exit(-1);
						} else {
							// create set if first one
							if (noXmrArgList.find(fn) == noXmrArgList.end()) {
								std::set<int> tempSet;
								noXmrArgList[fn] = tempSet;
							}
							// add to set of function arguments indices to skip
							noXmrArgList[fn].insert(argNum);
							if (verboseFlag) {
								errs() << "Directive: do not clone argument "
									   << argNum << " in function '"
									   << fn->getName() << "'\n";
							}
						}

					} else if (anno.startswith(cloneAfterCallAnno)) {
						if (verboseFlag) errs() << "Directive: replicate function '" << fn->getName() << "' arguments after the call\n";
						if (anno.size() == cloneAfterCallAnno.size()) {
							// clone all the args
							cloneAfterFnCall.insert(fn);
							// also, don't touch the insides, or make more than one call
							skipFn.push_back(fn->getName());
							skipLibCalls.push_back(fn->getName());
						} else {
							// it's a list of indices to clone
							StringRef argList = anno.substr(cloneAfterCallAnno.size(), anno.size() - cloneAfterCallAnno.size());
							errs() << err_string << " this feature is not yet supported as a directive!\n";
							errs() << anno << "\n";
							exit(-1);
						}

					} else if (anno == isr_anno) {
						if (verboseFlag) errs() << "Directive: function '" << fn->getName() << "' is an ISR\n";
						isrFunctions.insert(fn);
					} else if (anno == repl_ret_anno) {
						if (verboseFlag) errs() << "Directive: clone function '" << fn->getName() << "' return value\n";
						replReturn.insert(fn);
					} else if (anno == prot_lib_anno) {
						if (verboseFlag) errs() << "Directive: treat function '" << fn->getName() << "' as a protected library\n";
						protectedLibList.insert(fn);
						// it needs to be added to clone list as well
						fnsToClone.insert(fn);
					} else {
						assert(false && "Invalid option on function");
					}

				}
				// Global annotations
				else if (auto gv = dyn_cast<GlobalVariable>(e->getOperand(0)->getOperand(0))) {
					if (anno == no_xMR_anno) {
						if (verboseFlag) errs() << "Directive: do not clone global variable '" << gv->getName() << "'\n";
						globalsToSkip.insert(gv);
					} else if (anno == xMR_anno) {
						if (verboseFlag) errs() << "Directive: clone global variable '" << gv->getName() << "'\n";
						globalsToClone.insert(gv);
					} else if (anno == default_xMR) {
						if (verboseFlag) errs() << "Directive: set xMR as default\n";
					} else if (anno == default_no_xMR) {
						if (verboseFlag) errs() << "Directive: set no xMR as default\n";
						xMR_default = false;
					} else {
						if (verboseFlag) errs() << "Directive: " << anno << "\n";
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

	/*
	 * get the data from the list of "used" globals, and add it to volatileGlobals
	 * For example, in the FreeRTOS kernel, the list looks like this:
	 * @llvm.used = appending global [4 x i8*] [i8* bitcast (i32* @ulICCEOIR to i8*),
	 * 		i8* bitcast (i32* @ulICCIAR to i8*), i8* bitcast (i32* @ulICCPMR to i8*),
	 * 		i8* bitcast (i32* @ulMaxAPIPriorityMask to i8*)], section "llvm.metadata"
	 * If the global type is already a i8*, then we can detect that. Won't be that for functions.
	 */
	auto used_annos = M.getNamedGlobal("llvm.used");
	if (used_annos) {
		auto ua = cast<ConstantArray>(used_annos->getOperand(0));
		if (ua) {
			for (int i=0; i < ua->getNumOperands(); i++) {
				auto element = ua->getOperand(i);
				if (BitCastOperator* bc = dyn_cast<BitCastOperator>(element)) {
					if (GlobalVariable* gv = dyn_cast<GlobalVariable>(bc->getOperand(0))) {
						// found a global marked as "used"
						volatileGlobals.insert(gv);
						if (verboseFlag) errs() << "Directive: don't remove '" << gv->getName() << "'\n";
					} else if (Function* fn = dyn_cast<Function>(bc->getOperand(0))) {
						// found a function marked as "used"
						usedFunctions.insert(fn);
					}
				} else if (GlobalVariable* gv = dyn_cast<GlobalVariable>(element)) {
					if (verboseFlag) errs() << "Directive: don't remove '" << gv->getName() << "'\n";
					volatileGlobals.insert(gv);
				}
			}
		}
	}
}


void dataflowProtection::processLocalAnnotations(Module& M) {
	// Debug printing
	std::set<CallInst*> skippedIndirectCalls;
	// Local variables
	for (auto &F : M) {
		for (auto &bb : F) {
			for (auto &I : bb) {
				if ( auto CI = dyn_cast<CallInst>(&I) ) {
					// have to skip any bitcasts in function calls because they aren't actually a function
					if (isIndirectFunctionCall(CI, "processAnnotations", false)) {
						if (!CI->isInlineAsm()) {
							skippedIndirectCalls.insert(CI);
						}
						continue;
					}
					if (CI->getCalledFunction()->getName() == "llvm.var.annotation") {
						// Get variable
						auto adr = dyn_cast<BitCastInst>(CI->getOperand(0));
						AllocaInst* var;
						if (!adr) {
							// there could be no bitcast if the alloca is already of type i8
							var = dyn_cast<AllocaInst>(CI->getOperand(0));
						} else {
							var = dyn_cast<AllocaInst>(adr->getOperand(0));
						}
						assert(var && "valid alloca");

						auto ce = dyn_cast<ConstantExpr>(CI->getOperand(1));
						auto gv  = dyn_cast<GlobalVariable>(ce->getOperand(0));
						auto init = dyn_cast<ConstantDataArray>(gv->getInitializer());

						if (init) {
							auto anno = init->getAsCString();
							if (anno == no_xMR_anno) {
								if (verboseFlag) errs() << "Directive: do not clone local variable '" << *var << "'\n";
								instsToSkip.insert(var);
								walkInstructionUses(var, false);
							} else if (anno == xMR_anno) {
								if (verboseFlag) errs() << "Directive: clone local variable '" << *var << "'\n";
								instsToCloneAnno.insert(var);
								// if this is all we do, it will only clone the `alloca` instruction, but
								//  we want it to clone all instructions that use the same variable
								walkInstructionUses(var, true);
								// how do we get the syncpoints to happen?
								// have to add them manually
							} else {
								errs() << anno << "\n";
								assert(false && "Unrecognized variable annotation");
							}
						} else {
							errs() << "Local variable not alloca:\n";
							PRINT_VALUE(CI);
							assert(false && "Local variable not alloca");
						}
					}
				}
			}
		}
	}
	// print warnings
	if (verboseFlag && skippedIndirectCalls.size() > 0) {
		errs() << warn_string << " skipping indirect function calls in processLocalAnnotations:\n";
		for (auto CI : skippedIndirectCalls) {
			PRINT_VALUE(CI);
		}
	}
}


//----------------------------------------------------------------------------//
// Cleanup
//----------------------------------------------------------------------------//

// shared variables
static std::set<GlobalVariable*> anno_strings;
static std::set<BitCastOperator*> anno_casts;

void dataflowProtection::removeAnnotations(Module& M) {
	auto global_annos = M.getNamedGlobal("llvm.global.annotations");
	if (!global_annos)
		return;
	auto a = cast<ConstantArray>(global_annos->getOperand(0));
	if (!a)
		return;


	// Populate a list of global strings that are only used in annotations
	// There are also function "uses" which only show up here; remove them later
	for (int i=0; i < a->getNumOperands(); i++) {
		auto e = cast<ConstantStruct>(a->getOperand(i)); 	// This is part of global_anno

		for (int j=0; j < e->getNumOperands(); j++) {
			auto op = e->getOperand(j);
			if (op->getNumOperands() >= 1) {
				// constant string
				if (auto cs = dyn_cast<GlobalVariable>(e->getOperand(j)->getOperand(0))) {
					if (cs->getSection() == "llvm.metadata") {
						anno_strings.insert(cs);
					}
				}
				// remove constant expressions so they don't count as users later
				else if (auto ce = dyn_cast<ConstantExpr>(op)) {
					// Can't delete them here, so keep a list for later
					annotationExpressions.insert(ce);
				}
			}
			// They will always be casts because it is to i8*, which is not a valid function type by itself
			if (auto bCast = dyn_cast<BitCastOperator>(op)) {
				anno_casts.insert(bCast);
			}
		}
	}

	// Remove global annotations
	for (auto a_s : anno_strings) {
		if (a_s->getNumUses() < 1) {
			a_s->eraseFromParent();
		} else {
			globalsToSkip.insert(a_s);
		}
	}
	// Remove all the bitcasts that were inside that global annotations because they can cause problems
	int removedCount = 0;
	for (auto annoBitCast : anno_casts) {
		if (annoBitCast->getNumUses() == 0) {
			annoBitCast->dropAllReferences();
			removedCount++;
		}
	}
	// if (verboseFlag)
	// 	errs() << "Removed " << removedCount << " unused bitcasts from global annotations\n";

	// Remove the global that defines the default behavior of COAST (if exists)
	if (auto default_behavior = M.getNamedGlobal(default_global)) {
		if (default_behavior->getNumUses() < 1) {
			default_behavior->eraseFromParent();
		}
	}
}


void dataflowProtection::removeLocalAnnotations(Module& M) {
	// Remove llvm.var.annotation calls
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
		// Do this 2nd so that the one possible user is removed first
		if (op0 && op0->getNumUses() < 1) {
			if (op0->getParent()) {
				op0->eraseFromParent();
			}
			// We probably added this (which is probably a bitcast) to the list of instructions to clone
			if (instsToCloneAnno.find(op0) != instsToCloneAnno.end()) {
				instsToCloneAnno.erase(op0);
			}
		}
	}

	if (lva) {
		lva->removeFromParent();
	}

	// Remove global annotations
	auto global_annos = M.getNamedGlobal("llvm.global.annotations");
	if (global_annos) {
		M.getGlobalList().erase(global_annos);
	}
	// Remove strings it used
	for (auto a_s : anno_strings) {
		a_s->eraseFromParent();
	}
	// Remove all the bitcasts that were inside that global annotations because they can cause problems
	int removedCount = 0;
	for (auto annoBitCast : anno_casts) {
		if (annoBitCast->getNumUses() == 0) {
			annoBitCast->dropAllReferences();
			removedCount++;
		}
	}
	// if (verboseFlag)
	// 	errs() << "Removed " << removedCount << " unused bitcasts from global annotations\n";

	// Try again: Remove the global that defines the default behavior of COAST (if exists)
	if (auto default_behavior = M.getNamedGlobal(default_global)) {
		assert( (default_behavior->getNumUses() < 1) && "no more uses for global default");
		default_behavior->eraseFromParent();
	}

	return;
}
