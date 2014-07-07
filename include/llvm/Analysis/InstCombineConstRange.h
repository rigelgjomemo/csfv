#include "InstCombine.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/ADT/APInt.h"
#include <iostream>
using namespace llvm;
using namespace PatternMatch;
using namespace std;

class ConstantRangeAcsl : ConstantRange {

		ConstantRangeAcsl(APInt low, APInt high) : ConstantRange(low, high){}
		
	
};