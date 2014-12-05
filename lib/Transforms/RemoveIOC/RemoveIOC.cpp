
#define DEBUG_TYPE "remove-ioc"


#include "RemoveIOC.h"
#include "llvm/Analysis/ConstantRangeUtils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/PassRegistry.h"
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
#include <string>
#include <vector>

//included for removeOverFlowCheck function
#include "llvm/IR/IRBuilder.h"


using namespace llvm;


/*
void llvm::initializeRemoveIOC(PassRegistry &Registry) {
	  initializeRemoveIOCPass(Registry);
}

INITIALIZE_PASS_BEGIN(RemoveIOC, "remove-ioc",
                "Remove Integer Overflow Checks", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfo)
INITIALIZE_PASS_END(RemoveIOC, "remove-ioc",
                "Remove Integer Overflow Checks", false, false)
				
*/
namespace {
		
	//RemoveIOC::RemoveIOC() : FunctionPass(ID){}
	
	RemoveIOC::~RemoveIOC() {}
	
    void RemoveIOC::removeOverFlowCheck(CallInst *I,int i)
	{
		
		LLVMContext &context = I->getContext();
		std::string fname = I->getCalledFunction()->getName().str();
		//errs()<<"Enter removeOverFlowCheck :"<<fname<<"\n";
		//Since all overflow functions have only two operands, there is no need to iterate thorugh the arguments.
		
		Value *arg1 = I->getArgOperand(0);
		Value *arg2 = I->getArgOperand(1);
		//errs()<<"   arg1: "<<*arg1<<"\n";
		//errs()<<"   arg2: "<<*arg2<<"\n";
		ConstantRange *CR1 = nullptr;
		ConstantRange *CR2 = nullptr;
		ConstantRangeUtils crUtils;
		
		//dummy range creation
		APInt l,h;
		l = static_cast<uint64_t >(10);
		h = static_cast<uint64_t >(100);
		//ConstantRange *dummyRange = new ConstantRange(l,h); //just for testing purpose. Need to clear it out afterwards
		Instruction *o1,*o2;
		if ((o1 = dyn_cast<Instruction>(arg1)) && (o2 = dyn_cast<Instruction>(arg2)))
		{
			if (o1->getMetadata("acsl_range"))
			{
				//errs()<<"got Metadata for arg1 \n";
				MDNode *md = dyn_cast<MDNode>(o1->getMetadata("acsl_range"));
				//DEBUG(errs()<<*md<<"\n";);
				CR1 = crUtils.getConstantRange(md);
				//errs()<<"range for arg1"<<*CR1<<"\n";				
			}
			else 
			{
			  // CR1 = new ConstantRange(*dummyRange);
				//if there is no metadata available, then it is best to skip that check function call
			   //errs()<<"Exit removeOverFlowCheck-Could Not get Metadata for arg1 instruction \n";
		       return;
			}
		
			if(o2->getMetadata("acsl_range") ) 
			{
				//errs()<<"got Metadata for arg2 \n";
				MDNode *md = dyn_cast<MDNode>(o2->getMetadata("acsl_range"));
				//DEBUG(errs()<<*md<<"\n";);
				CR2 = crUtils.getConstantRange(md);
				//errs() <<"range for arg2"<< *CR2 << "\n";
		     }
			 else 
			 {
				 //CR2 = new ConstantRange(*dummyRange);
				 //if there is no metadata available, then it is best to skip that check function call
				 //errs()<<"Exit removeOverFlowCheck-Could Not get Metadata for arg2 instruction \n";
				 return;
			 }
		 }
		 else
	     {
			//TODO :enter some debugging information here
			//errs()<<"Exit removeOverFlowCheck -Couldnot dyn_cast args to Instruction \n";
			return;
		 }
		
		//assumption is that for any LLVM Instruction, the two operands should be same width.
		//verify this however with Prof. Rigel
		 ConstantRange result = *CR1;
		 uint32_t bitwidth = CR1->getBitWidth();
		 ConstantRange ulimit(APInt::getMinValue(bitwidth),APInt::getMaxValue(bitwidth));
		 ConstantRange slimit(APInt::getSignedMinValue(bitwidth),APInt::getSignedMaxValue(bitwidth));
		
		//new Instruction to be inserted if the current overflow check is redundant
		Instruction *NI = nullptr;
		  
		switch(i)
		{				
		  //smul
          case 0:
		     result = CR1->multiply(*CR2);
			  
			 //if the resulting range is within the signed limit, we need to remove the checker function			 
			 if (slimit.contains(result)) 
			 {
                //errs()<<"smul \n";
				NI = BinaryOperator::CreateNSW(Instruction::Mul, arg1, arg2, "smul");				
			 }	
			 break;
		  //sadd
		  case 1:
		     result = CR1->add(*CR2);
			  
			 //if the resulting range is within the signed limit, we need to remove the checker function			 
			 if (slimit.contains(result)) 
			 {
				//errs()<<"sadd \n";
			    NI = BinaryOperator::CreateNSW(Instruction::Add, arg1, arg2, "sadd");
			 }	
			break;
		  
		  //umul
		  case 2:
			result = CR1->multiply(*CR2);
			  
			 //if the resulting range is within the unsigned limit, we need to remove the checker function			 
			 if (ulimit.contains(result)) 
			 {  
				//errs()<<"umul \n"; 
			    NI = BinaryOperator::Create(Instruction::Mul, arg1, arg2, "umul");
			 }	
		    break;
		  
		  //uadd
		  case 3:
		    result = CR1->add(*CR2);
			  
			 //if the resulting range is within the unsigned limit, we need to remove the checker function			 
			 if (ulimit.contains(result)) 
			 {
				//errs()<<"uadd \n";
				NI = BinaryOperator::Create(Instruction::Add, arg1, arg2, "uadd");
			 }	
		    break;
			
		  //ssub
		  case 4:
		    result = CR1->sub(*CR2);
			  
			 //if the resulting range is within the signed limit, we need to remove the checker function			 
			 if (slimit.contains(result)) 
			 {
				//errs()<<"ssub \n";
				NI = BinaryOperator::CreateNSW(Instruction::Sub, arg1, arg2, "ssub");
			 }	
		    break;
			
			//usub
		  case 5:
		    result = CR1->sub(*CR2);
			  
			 //if the resulting range is within the unsigned limit, we need to remove the checker function			 
			 if (ulimit.contains(result)) 
			 {
				//errs()<<"usub \n";
				NI = BinaryOperator::Create(Instruction::Sub, arg1, arg2, "usub");
			 }	
		    break;
		}
		
		//if the overflow check is redundant
		if (NI)
		{

		  BasicBlock *BB = I->getParent();	  
		  
		  //Now to get the extract instruction where the computation for the overFlowCheckFunction is stored (just after the call to it)
		  //This is done because other instructions that follow might use this result, and we should ReplaceAllUses of this 
		  //instruction
		  
		  BasicBlock::iterator itb = I;
		  itb++;
		  Instruction *Vextract = itb;
		  //errs()<<Vextract->getOpcodeName()<<"\n";		  
		  //Now check for the branch condition and in that block
		  BranchInst *br_inst = nullptr;
		  for (itb = I; itb != BB->end(); itb++)
		     if ((br_inst = dyn_cast<BranchInst>(itb))) break;
		 
         //Add metadata to the new Instruction NI
     	
    	 APInt lo = result.getLower();
	     //DEBUG(errs()<< lo << "\n";);
	     APInt hi = result.getUpper();
		 ConstantInt* hiC = ConstantInt::get(context, hi);
		 int64_t hig=hiC->getSExtValue();
		 ConstantInt* loC = ConstantInt::get(context, lo);
		 int64_t low=loC->getSExtValue();
					
		 std::ostringstream o;
     	 o << "assert val >= " << low << " && "<<"val <= "<<(hig-1);
	 				
	     std::string metadata = o.str();
		 //DEBUG(errs()<< metadata << "\n";);
			
		 MDNode* V = MDNode::get(context, MDString::get(context, metadata));
		 NI->setMetadata("acsl_range", V);
 
		 NI->insertBefore(I);
		 
		 Vextract->replaceAllUsesWith(NI);
		 // ReplaceInstWithInst(Vextract,NI);
		  
		  //this is the Basic block where the control reaches if there is no overflow
		BasicBlock *BBCont = br_inst->getSuccessor(0);
		  
		//store all instructions to be deleted in a deque
		for (BasicBlock::iterator it = I; it != BB->end(); it++)
		     dequeToDel.push_back(it);
		
		
	//delete all instructions from that overflow check function call to the next branch instruction
	//	for (std::vector<Instruction*>::reverse_iterator vit = vec.rbegin();vit != vec.rend();vit++)
	//		{
	//		 // errs()<<"Instruction being deleted :"<<*vit<<"\n";	 
	//		(*vit)->eraseFromParent();
	//		}
		
		Instruction *BI = BranchInst::Create(BBCont);
		//BB->getInstList().push_back(NI);
		//BB->getInstList().push_back(BR);
		dequeToInsert.push_back(BI);
			
		}
		
		
		//errs()<<"Exit removeOverFlowCheck \n";
	}
	
	
	
	void RemoveIOC::removeInstructionsInDequeToDel()
	{
	
       while(!dequeToDel.empty())
	   {
		   Instruction *DI = dequeToDel.back();
		   if (dyn_cast<BranchInst>(DI))
			   if (!dequeToInsert.empty())
				    { Instruction *I = dequeToInsert.back();
					  dequeToInsert.pop_back();
	                  I->insertBefore(DI);}
		   
		   //errs()<<"Removing Instruction \n"<<*DI;
		   DI->eraseFromParent();
		   dequeToDel.pop_back();
	   
		}				
		
	}
	
	
	bool RemoveIOC::runOnFunction(Function &F) {
		
		//inst_iterator I = inst_begin(F);
		for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
	   // while (1){
			if (I->getOpcode() == Instruction::Call)
			{
			   //errs()<<"Enter RemoveIOC \n"; 
			   Instruction* inst = dyn_cast<Instruction>(&*I);	
				CallInst *CI = dyn_cast<CallInst>(inst);
							Function *calledF = CI->getCalledFunction();
							std::string fname = calledF->getName().str();
							std::string overflow_check_functions[]={
								"llvm.smul.with.overflow",
								"llvm.sadd.with.overflow",
								"llvm.umul.with.overflow",
								"llvm.uadd.with.overflow",
								"llvm.ssub.with.overflow",
								"llvm.usub.with.overflow",
								};
							
				for (int i=0; i < 6 ; ++i){
				 std::size_t found;	
				   if ((found = fname.find(overflow_check_functions[i])) != std::string::npos) {
					   //errs() << "Found:" <<fname<<"\n";
				        removeOverFlowCheck(CI,i);
						break;						
				   }
				}
												
			} 
		 //if (I != inst_end(F)) I++;
		// else break;
		}
		removeInstructionsInDequeToDel();
		//errs()<<"Done with RemoveIOC::runOnFunction\n";
		return true;
	}
}
	
char RemoveIOC::ID = 0;
static RegisterPass<RemoveIOC> X("remove-ioc", "Integer Overflow Checks Removal Pass (csfv)");