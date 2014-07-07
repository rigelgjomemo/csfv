#ifndef CONSTANTRANGEUTILS_H
#define CONSTANTRANGEUTILS_H
#include "llvm/IR/ConstantRange.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h" 
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/ValueHandle.h"

namespace llvm
{

class ConstantRangeUtils
{
public:
	ConstantRangeUtils();
	~ConstantRangeUtils();
	
	typedef enum {FALSE, TRUE, UNKNOWN} Result;
	
	int computeSignBit(ConstantRange CR);
	
	bool isKnownZero(ConstantRange CR);
	
	bool isNegative(ConstantRange CR);
	
	bool isPositive(ConstantRange CR);
	
	ConstantRange* getConstantRange(MDNode* md);
	
	ConstantRangeUtils::Result tryConstantFoldCMP(unsigned Predicate, Value* LHS, Value* RHS);
	
	ConstantRangeUtils::Result foldConstantRanges(unsigned Predicate, ConstantRange* CR_LHS, ConstantRange* CR_RHS);
	
	ConstantRangeUtils::Result foldLHSC(unsigned Predicate, ConstantRange* CR_LHS, ConstantInt* CI_RHS);
	
	ConstantRangeUtils::Result foldCRHS(unsigned Predicate, ConstantInt* CI_LHS, ConstantRange* CR_RHS);
};

}

#endif // CONSTANTRANGEUTILS_H
