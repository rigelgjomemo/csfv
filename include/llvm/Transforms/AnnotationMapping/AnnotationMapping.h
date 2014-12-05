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

#ifndef SAFECODE_VARMAP_H
#define SAFECODE_VARMAP_H

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
    
    struct AnnotationMapping : public FunctionPass {
        static char ID;
		//std::set<std::string> visitedFunctions;
		MDNode* topContext;
		DIScope* topScope;
        AnnotationMapping();
		~AnnotationMapping();
        
        // class containing all the information about an IR variable
        // useful to choose the corrisponding right variable name mapping
        // between the source code variable and the register or allocation address
        class VarMappingInfo {
        public:
            Value * context;
			//DIScope* scope;
            //BasicBlock * basicBlock;
            std::string nameIR;
            int codeLine;
            VarMappingInfo(Value* context, /*BasicBlock * basicBlock,*/ std::string nameIR, int codeLine): context(context), /*basicBlock(basicBlock),*/ nameIR(nameIR), codeLine(codeLine){}
        };
        
        
       // virtual bool runOnModule(Module &M);
		bool runOnFunction(Function &F);
		
		int getFunctionArgumentPosition(Value* op1, Function& F);
		
		int intMallocSize(Value* val, uint64_t sizeBytes, CallInst *call);
		
		int getSizeForMalloc(Value *opsb1, Type* ty);
		
		int getMinimumMallocSizeFromFunction(Function &F);
		
		void handleBitCastInst(GetElementPtrInst *GEP, BitCastInst *bitcast);
		
		void handleAllocationByFunctionCall(GetElementPtrInst *GEP, CallInst *CI);
		
		void handleFunctionArgument(GetElementPtrInst *GEP, Value *startMemoryLocation);
		
		void handleGlobalArray(GetElementPtrInst *GEP, GlobalVariable* gv);
		
		void attachMallocMetadata(GetElementPtrInst *GEP);
		
		std::pair< std::string , ConstantRange*> getRangePair(ACSLExpression *expr);
		
        std::string findNameIR(StringMap< std::vector<VarMappingInfo> > * mappingMap, MDNode *  annotationContext,BasicBlock * basicBlock, std::string  nameSC, int codeLine);
		
		std::string findNameIRNew(StringMap< std::vector<VarMappingInfo> > * mappingMap, MDNode*  DbgNode,BasicBlock * basicBlock, std::string  nameSC, int codeLine);
        
        void printMappingMap(StringMap< std::vector<VarMappingInfo> > * mappingMap);
        
        void printReverseNameSCMap(StringMap< std::string > * reverseNameSCMap);
        
        void printConstantsMap(StringMap< std::vector<std::string> > * constantsMap);
		
		void printIrNameRangeMap(std::map<std::string, ranges::Range> irNameRangesMap);
		
		void assignFreshNamesToUnamedInstructions(Function & F);
		
		void collectVariableMappingInformations(Function &F, StringMap< std::vector<VarMappingInfo> > &mappingMap, StringMap< std::string > &reverseNameSCMap, StringMap< Value * > &reverseContextMap,StringMap< int > &reverseCodeLineMap,StringMap< std::vector<std::string> > &constantsMap);
		
		void collectVariableMappingInformationsRigel(Function &F, StringMap< std::vector<VarMappingInfo> > &mappingMap, StringMap< std::string > &reverseNameSCMap, StringMap< Value * > &reverseContextMap,StringMap< int > &reverseCodeLineMap,StringMap< std::vector<std::string> > &constantsMap);
       
	   void collectVariableMappingInformationsRigelNew(Function &F, StringMap< std::vector<VarMappingInfo> > &mappingMap, StringMap< std::string > &reverseNameSCMap, 
																StringMap< DIScope * > &reverseContextMap,StringMap< int > &reverseCodeLineMap,
																StringMap< std::vector<std::string> > &constantsMap);
	   
	    void collectFirstLineBasciBlock(Function &F, std::map< BasicBlock*, int > &firstLineBasciBlockMap);
		
		StringRef getAnnotationFromStore(StoreInst* storeAnnot);
		
		void printSafecodeMap(std::map<Instruction *, std::map<std::string, ranges::Range> > safecodeMap);
		
		void insertPhiNodeInformations(Function &F, StringMap< std::vector<VarMappingInfo> > &mappingMap, StringMap< std::string > &reverseNameSCMap, StringMap< Value * > &reverseContextMap,StringMap< int > &reverseCodeLineMap,StringMap< std::vector<std::string> > &constantsMap, std::map< BasicBlock*, int > &firstLineBasciBlockMap);
       
    };
    
}//end of anonymous namespace
#endif
