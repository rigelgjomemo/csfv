//===-------- Safecode Variable Mapping Pass Header -----------------------===//
//
// The LLVM Compiler Infrastructure - CSFV Annotation Framework
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a transformation pass that changes the acsl annotations
// mapping the variable names to the registers associated with them by mem2reg
//
//===----------------------------------------------------------------------===//

#ifndef REMOVE_IOC_H
#define REMOVE_IOC_H

#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Constants.h"

#include "llvm/Analysis/node.h"
#include "llvm/Analysis/driver.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/CFG.h"
#include "llvm/Analysis/ranges.h"

#include <stack>
#include <iostream>
#include <sstream>
#include <cstdlib>


using namespace llvm;


namespace llvm {
    
    struct RemoveIOCPass : public FunctionPass {
        static char ID;
		
        RemoveIOCPass();
		
		~RemoveIOCPass();
        
       
        
        
       // virtual bool runOnModule(Module &M);
		bool runOnFunction(Function &F);
		
		
       
    };
    
}//end of anonymous namespace
#endif
