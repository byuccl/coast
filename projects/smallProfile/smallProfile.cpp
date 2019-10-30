/*
 * smallProfile.cpp
 *
 * Instruments code to count and report number of calls to each function
 *
 * Copyright BYU CCL
 * August 2019
 */

#define DEBUG_TYPE "debugStatements"

#include <string>
#include <set>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include "llvm/Support/raw_ostream.h"
#include <llvm/IR/Constants.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/CommandLine.h>

using namespace llvm;

//--------------------------------------------------------------------------//
// Command line options for the pass
//--------------------------------------------------------------------------//
cl::opt<std::string> printFnNameCl("printFnName", cl::desc("Name of printing function"));
cl::opt<bool> noPrintFlag ("noPrint", cl::desc("Does not insert call to profile printing function"));


//--------------------------------------------------------------------------//
// Class spec
//--------------------------------------------------------------------------//
class SmallProfile : public ModulePass {
public:
	static char ID;             // Pass identification
	SmallProfile() : ModulePass(ID) { }

	bool runOnModule(Module &M);
	void profileLocalFunctions(Module &M);
	void profileExternalFunctions(Module &M);

	void insertProfilePrintFunction(Module &M);
	Function* createProfilePrintFunction(Module &M);
	GetElementPtrInst* getGEPforPrint(Module &M, StringRef* varName, BasicBlock*& bb);
	Function* getPrintFunction(Module &M);

	GlobalVariable* createGlobalCounter(Module &M, Function* fn);
	void incrementCounter(GlobalVariable* cntr, Instruction* insertHere, bool extCall);

private:
	// constant strings
	StringRef newLineChar = StringRef("\n");
	StringRef formatInt = StringRef(": %d");

	// important pointers
	Function* mainFunc = nullptr;
	ReturnInst* mainReturn = nullptr;
	Type* type_i32 = nullptr;

	// containers
	std::vector<std::pair<Function*, GlobalVariable*> > profPairs;
	std::set<Function*> funcsToLookFor;
};

char SmallProfile::ID = 0;
static RegisterPass<SmallProfile> X("SmallProfile",
		"Insert profiling instructions into the IR", false, true);


//--------------------------------------------------------------------------//
// Functions
//--------------------------------------------------------------------------//

/*
 * Get a reference to the print function
 */
Function* SmallProfile::getPrintFunction(Module &M) {

	// types we'll need
	Type *charPointerType = PointerType::get(IntegerType::get(M.getContext(), 8), 0);
	Type* type_i32 = Type::getInt32Ty(M.getContext());
	FunctionType *printfTy = FunctionType::get(type_i32, std::vector<Type*> (1, charPointerType), true);

	// name of the print function
	std::string printFnName;
	if (printFnNameCl != "") {
		printFnName = printFnNameCl;
	} else {
		printFnName = "printf";
	}

	// make the function
	Constant* printfc = M.getOrInsertFunction(printFnName, printfTy);
	Function* print = dyn_cast<Function>(printfc);
	assert(print && "Print function not defined");

	return print;
}

/*
 * insert instructions to increment global into entry block of functions we have the body of
 */
void SmallProfile::profileLocalFunctions(Module &M) {

	std::string glblNm;

	for (auto &F : M) {
		StringRef fnName = F.getName();
		// skip the debug information function calls
		if (fnName.startswith_lower("llvm.dbg") || fnName.startswith_lower("llvm.lifetime."))
			continue;

		// external function calls
		if(F.getBasicBlockList().size() == 0) {
			// if we don't have the function body, we'll have to look at the calls of it instead
			funcsToLookFor.insert(&F);
		} else {

			// create a global to increment for each function
			GlobalVariable* nextCnt = createGlobalCounter(M, &F);

			// Add calls to increment counter
			Instruction* insertHere = F.getEntryBlock().getFirstNonPHIOrDbg();
			if (isa<LandingPadInst>(insertHere)) {
				insertHere = insertHere->getNextNode();
			}
			incrementCounter(nextCnt, insertHere, false);
		}

		// get the spot for printing at the end
		if (F.getName() == "main") {
			mainFunc = &F;

			//find the last instruction
			for(auto &bb : F){
				if(ReturnInst* RI = dyn_cast<ReturnInst>(bb.getTerminator())){
					mainReturn = RI;
				}
			}
		}
	}

	return;
}

/*
 * for functions that we don't have the body of, instructions to increment globals must come right
 * before the function call
 */
void SmallProfile::profileExternalFunctions(Module &M) {

	std::string glblNm;

	// add profiling statements to all calls of external functions
	for (auto &F : M) {
		for (auto &bb : F) {
			for (auto &I : bb) {
				if (CallInst* CI = dyn_cast<CallInst>(&I)) {
					Function* calledF = CI->getCalledFunction();
					// skip function pointers, sorry
					if (!calledF)
						continue;

					if (funcsToLookFor.find(calledF) != funcsToLookFor.end()) {
						// create a global, unless it exists already
						GlobalVariable* nextCnt = createGlobalCounter(M, calledF);

						// Add calls to increment counter
						incrementCounter(nextCnt, CI, true);
					}
				}
			}
		}
	}

	return;
}

/*
 * Right before the program completes, print the value of each of the global counters.
 * If your program never "finishes," but runs forever, you can insert a call to
 * PRINT_PROFILE_STATS wherever you want this to be printed.
 */
void SmallProfile::insertProfilePrintFunction(Module &M) {

	Function* printFn = getPrintFunction(M);

	// create the stats function
	Function* printStatsFn = createProfilePrintFunction(M);

	// don't print if there is no main function, or if flag said not to
	if (mainReturn == nullptr || noPrintFlag) {
		errs() << "\033[0;33mSmallProfile\033[0m: skipping inserting call to " << printStatsFn->getName() << "\n";
	} else {
		// insert a function call to stats function
		Twine callName = Twine("callStats");
		ArrayRef<Value*> statsCallArgs;
		CallInst* statsCall = CallInst::Create(printStatsFn, "", mainReturn);
//		Idea: http://www.cplusplus.com/reference/cstdlib/atexit/
	}

	// holds args for the call instructions
	std::vector<Value*> sepArgs;
	std::vector<Value*> newlineArgs;
	BasicBlock* entryBlock = &(printStatsFn->getEntryBlock());

	// Arguments for printing a new line
	GetElementPtrInst* newlineGEP = getGEPforPrint(M, &newLineChar, entryBlock);
	newlineArgs.push_back(newlineGEP);
	ArrayRef<Value*>* callArgsNewline = new ArrayRef<Value*>(newlineArgs);
	// For printing the count
	GetElementPtrInst* decGEP = getGEPforPrint(M, &formatInt, entryBlock);

	// where to put instructions
	Instruction* insertionPoint = entryBlock->getFirstNonPHI();
	Instruction* returnPoint = entryBlock->getTerminator();
	if (isa<LandingPadInst>(insertionPoint)) {
		insertionPoint = insertionPoint->getNextNode();
	}

	//Move all the GEPs to the front of the entry block to dominate all uses
	newlineGEP->moveBefore(insertionPoint);
	decGEP->moveBefore(newlineGEP);

	for (auto p : profPairs) {
		//Variable def'ns
		Function* F = p.first;
		StringRef fnName = F->getName();
		std::vector<Value*> fnArgs;
		std::vector<Value*> cntArgs;

		// Arguments for printing the function name
		GetElementPtrInst* fnGEP = getGEPforPrint(M, &fnName, entryBlock);
		fnArgs.push_back(fnGEP);
		ArrayRef<Value*>* fnCallArgs = new ArrayRef<Value*>(fnArgs);

		// Arguments for printing the profile count
		cntArgs.push_back(decGEP);
		LoadInst* LI = new LoadInst(p.second, "glblLoad");
		cntArgs.push_back(LI);
		ArrayRef<Value*>* cntCallArgs = new ArrayRef<Value*>(cntArgs);

		//Create all the function calls: function arrow bb newline
		CallInst* newlinePrint = CallInst::Create(printFn, *callArgsNewline, "", returnPoint);
		CallInst* cntPrint = CallInst::Create(printFn, *cntCallArgs, "", newlinePrint);
		CallInst* fnNamePrint = CallInst::Create(printFn, *fnCallArgs, "", cntPrint);

		LI->insertBefore(fnNamePrint);
		fnGEP->moveBefore(insertionPoint);
	}

	return;
}

/*
 * Creates a function that will hold all of the printing calls.
 */
Function* SmallProfile::createProfilePrintFunction(Module &M) {

	FunctionType* statsCallType = FunctionType::get(Type::getVoidTy(M.getContext()), false);
	Constant* c = M.getOrInsertFunction("PRINT_PROFILE_STATS", statsCallType);
	Function* printStatsFn = dyn_cast<Function>(c);
	assert(printStatsFn && "Profiling function is non-void");

	// Create a basic block that holds all the print functions, as long as it doesn't exist already
	if (printStatsFn->getBasicBlockList().size() == 0) {
		BasicBlock* bbe = BasicBlock::Create(M.getContext(), Twine("entry"), printStatsFn);
		ReturnInst* statsRet = ReturnInst::Create(M.getContext(), bbe);
	}

	return printStatsFn;
}

/*
 * Creates a global variable that tracks function calls based on the name of the function.
 * This checks to see if one exists before creating it.
 */
GlobalVariable* SmallProfile::createGlobalCounter(Module &M, Function* fn) {

	// the name of the global variable
	std::string glblNm;
	StringRef fnName = fn->getName();
	glblNm = "__" + fnName.str() + "_profCnt";

	// create the global
	GlobalVariable* nextCnt = M.getGlobalVariable(StringRef(glblNm));

	if (nextCnt == nullptr) {
		nextCnt = cast<GlobalVariable>(M.getOrInsertGlobal(StringRef(glblNm), type_i32));

		// set the correct attributes, making it local instead of extern
		nextCnt->setConstant(false);
		nextCnt->setInitializer(ConstantInt::getNullValue(type_i32));
		nextCnt->setUnnamedAddr( GlobalValue::UnnamedAddr() );
		nextCnt->setAlignment(4);

		// add to list for later
		std::pair<Function*, GlobalVariable*> tmpPair = std::make_pair(fn, nextCnt);
		profPairs.push_back(tmpPair);
	}

	assert(nextCnt && "Global variable counter exists");
	return nextCnt;
}

/*
 * Create instructions that increment a global variable and inserts them at the specified point.
 */
void SmallProfile::incrementCounter(GlobalVariable* cntr, Instruction* insertHere, bool extCall) {

	LoadInst* LI = new LoadInst(cntr, "cntLoad");
	Constant* one = ConstantInt::get(LI->getType(), 1, false);
	BinaryOperator* BI = BinaryOperator::CreateAdd(LI, one, "incCnt");
	StoreInst* SI = new StoreInst(BI, cntr);
	LI->insertBefore(insertHere);
	BI->insertAfter(LI);
	SI->insertAfter(BI);

	if (extCall) {
		//if there's debug information for the call, copy it to new instructions
		if (auto dbgLoc = insertHere->getDebugLoc()) {
			LI->setDebugLoc(dbgLoc);
			BI->setDebugLoc(dbgLoc);
			SI->setDebugLoc(dbgLoc);
		}
	}

	return;
}

/*
 * creates the correct GEP instruction to load a global string for printing
 */
GetElementPtrInst* SmallProfile::getGEPforPrint(Module &M, StringRef* varName, BasicBlock*& bb){

	Type* type_i8 = Type::getInt8Ty(M.getContext());

	//Create a char array (i8s)
	ArrayType * type_i8_array = ArrayType::get(type_i8,(unsigned long long int)(varName->size()+1));
	Constant * dataInit = ConstantDataArray::getString(M.getContext(), *varName);

	//Create a global variable and init to the array of i8s
	GlobalVariable * globalVal = dyn_cast<GlobalVariable>(
			M.getOrInsertGlobal(*varName, type_i8_array));
	globalVal->setConstant(true);
	globalVal->setInitializer(dataInit);
	globalVal->setLinkage(GlobalVariable::PrivateLinkage);
	globalVal->setUnnamedAddr( GlobalValue::UnnamedAddr() );
	globalVal->setAlignment(1);


	//Create constants for GEP arguments
	ConstantInt* zeroCI = ConstantInt::get(IntegerType::getInt32Ty(M.getContext()),0,false);
	Value* zeroVal = dyn_cast<Value>(zeroCI);

	//Assemble the GEP instruction
	std::vector<Value*> gepArgs;
	gepArgs.push_back(zeroVal);
	gepArgs.push_back(zeroVal);
	ArrayRef<Value*>* gepArgsArray;
	gepArgsArray = new ArrayRef<Value*>(gepArgs);

	//Insert the instruction into basic block
	GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(type_i8_array,
			globalVal,*gepArgsArray,varName->str(),bb->getTerminator());

	return gep;
}

/*
 * Top level function that controls the pass
 */
bool SmallProfile::runOnModule(Module &M) {

	this->type_i32 = Type::getInt32Ty(M.getContext());

	profileLocalFunctions(M);

	profileExternalFunctions(M);

	insertProfilePrintFunction(M);

	return true;
}
