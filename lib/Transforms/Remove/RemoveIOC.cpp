
#define DEBUG_TYPE "removeioc"


#include "llvm/Analysis/ConstantRangeUtils.h"

#include "RemoveIOC.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR//InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/ADT/DenseMap.h"

#include "llvm/Analysis/node.h"
#include "llvm/Analysis/driver.h" 
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/CFG.h"
//#include "safecode/ranges.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IntrinsicInst.h"
#include <stack>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <map>
#include <algorithm>


using namespace llvm;


namespace llvm {
	
	RemoveIOCPass::RemoveIOCPass() : FunctionPass(ID){}
	
	RemoveIOCPass::~RemoveIOCPass() {}
	
	bool RemoveIOCPass::runOnFunction(Function &F) {
		for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
			errs()<<"Remove\n";
		}
		return true;
	}
	
	
	
}

char RemoveIOCPass::ID = 0;
static RegisterPass<RemoveIOCPass> X("remove-ioc", "Integer Overflow Checks Removal Pass (csfv)");
