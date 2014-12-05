
#define DEBUG_TYPE "annotationpropagation"

#include "AnnotationPropagation.h"
#include "llvm/Analysis/ConstantRangeUtils.h"

//#include "llvm/Analysis/SafecodeVarMap.h"
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
//#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IntrinsicInst.h"
#include <stack>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <map>
#include <algorithm>


using namespace llvm;


namespace {
	
	AnnotationPropagationPass::AnnotationPropagationPass() : FunctionPass(ID){}
	
	AnnotationPropagationPass::~AnnotationPropagationPass() {}
	
	//sequence usually is smth like:
	// CASE 1:
	//%var1 = load %t !acsl_range
	//%var2 = load %u !acsl_range
	//%result = unsupported_operation %var1, %var2 
	//store %result %i
	//%var3 = load %i
	//%add = add %var3, %k
	//pseudo-algo
	//  I = add instruction
	//  get operands op1, op2 //must figure out what operations yielded their values
	//  if op1 is a LoadInst  //case: result = t*u; add = result+k
	//		get the most recent store inst to the operand of the LoadInst
	//		get the first operand of the store (i.e., result)
	//		print out its opcode
	//  else					//CASE 2:
	//							//case of temporaries: add = t*u+k, there are no stores and loads
	//     					//%var1 = load %t
	//							//%var3 = load %u
	//							//%result = unsupported_operation %var1, %var2
	//							//%add = add %result, %k (actually there is also the load for k, not shown here, since it is the same case)
	void AnnotationPropagationPass::profileUnsupportedOps(Instruction *Inst) {
		if(BinaryOperator* I = dyn_cast<BinaryOperator>(Inst) ) {
			Value* op1 = I->getOperand(0);
			Value* op2 = I->getOperand(1);
			//Case 2 for op1
			if(Instruction* o = dyn_cast<Instruction>(op1)) {
				LoadInst* o1 = dyn_cast<LoadInst>(op1);
				if(!o1) { //CASE 2: it is not  a load, so it must be an intermediate result
					errs() << I->getOpcodeName() <<":unsupported:";
					errs() << "    "<<o->getOpcodeName() <<"\n";
					errs() <<"                ("<<*I<<")\n";
					errs() <<"                ("<<*op1<<")\n";
				}
				else { //CASE 1: op1 is a load
					// find the most recent store
					Value* theAlloc = o1->getOperand(0);
					AllocaInst* theAlloca = dyn_cast<AllocaInst>(theAlloc);
					//std::map<AllocaInst*, StoreInst*>::iterator it = mapAllocaStores.find(theAlloca);
					//StoreInst* recentStore = it->second;
					for(std::vector<StoreInst*>::reverse_iterator it = stores.rbegin(); it != stores.rend(); ++it) {
						Value* op = (*it)->getOperand(1);
						if(AllocaInst* all = dyn_cast<AllocaInst>(op))
							if(all==theAlloca) {//found most recent store
								Value* unsuppOp = (*it)->getOperand(0);
								errs() << I->getOpcodeName() <<":unsupported:\n";
								if(Instruction* ins = dyn_cast<Instruction>(unsuppOp)) {
									errs() << "    "<<ins->getOpcodeName() <<"\n";
									errs() <<"                ("<<*I<<")\n";
									errs() <<"                ("<<*ins<<")\n";
								}
								else {
									errs() << "    "<<*unsuppOp <<"\n";
									errs() <<"                ("<<*I<<")\n";
									errs() <<"                ("<<*unsuppOp<<")\n";
								}
								break;
							}
						}
				}
			
			}
			//Case 2 for op2
			if(Instruction* o = dyn_cast<Instruction>(op2)) {
				LoadInst* o2 = dyn_cast<LoadInst>(op2);
				if(!o2) { //CASE 2: it is not  a load, so it must be an intermediate result
					errs() << I->getOpcodeName() <<":unsupported:\n";
					errs() <<"    "<< o->getOpcodeName() <<"\n";
					errs() <<"                ("<<*I<<")\n";
					errs() <<"                ("<<*op2<<")\n";
				}
				else { //CASE 1: op2 is a load
					// find the most recent store
					Value* theAlloc = o2->getOperand(0);
					AllocaInst* theAlloca = dyn_cast<AllocaInst>(theAlloc);
					//std::map<AllocaInst*, StoreInst*>::iterator it = mapAllocaStores.find(theAlloca);
					//StoreInst* recentStore = it->second;
					for(std::vector<StoreInst*>::reverse_iterator it = stores.rbegin(); it != stores.rend(); ++it) {
						Value* op = (*it)->getOperand(1);
						if(AllocaInst* all = dyn_cast<AllocaInst>(op))
							if(all==theAlloca) {//found most recent store
								Value* unsuppOp = (*it)->getOperand(0);
								errs() << I->getOpcodeName() <<":unsupported:\n";
								if(Instruction* ins = dyn_cast<Instruction>(unsuppOp)) {
									errs() << "    " << ins->getOpcodeName() <<"\n";
									errs() <<"                ("<<*I<<")\n";
									errs() <<"                ("<<*ins<<")\n";
								}
								else {
									errs() <<"    "<<*unsuppOp <<"\n";
									errs() <<"                ("<<*I<<")\n";
									errs() <<"                ("<<*unsuppOp<<")\n";
								}
								break;
							}
						}
				}
			
			}
		
		}
		else { //the unsupported instruction is not a binary operator.
			Value* op1 = Inst->getOperand(0);
			//Case 2 for op1
			if(Instruction* o = dyn_cast<Instruction>(op1)) {
				DEBUG(errs()<<*o <<"\n");
				LoadInst* o1 = dyn_cast<LoadInst>(op1);
				if(!o1) { //CASE 2: it is not  a load, so it must be an intermediate result
					errs() << Inst->getOpcodeName() <<":unsupported:";
					errs() << "    "<<o->getOpcodeName() <<"\n";
					errs() <<"                ("<<*Inst<<")\n";
					errs() <<"                ("<<*op1<<")\n";
				}
				else { //CASE 1: op1 is a load
					// find the most recent store
					Value* theAlloc = o1->getOperand(0);
					AllocaInst* theAlloca = dyn_cast<AllocaInst>(theAlloc);
					//std::map<AllocaInst*, StoreInst*>::iterator it = mapAllocaStores.find(theAlloca);
					//StoreInst* recentStore = it->second;
					for(std::vector<StoreInst*>::reverse_iterator it = stores.rbegin(); it != stores.rend(); ++it) {
						Value* op = (*it)->getOperand(1);
						if(AllocaInst* all = dyn_cast<AllocaInst>(op))
							if(all==theAlloca) {//found most recent store
								Value* unsuppOp = (*it)->getOperand(0);
								errs() << Inst->getOpcodeName() <<":unsupported:\n";
								if(Instruction* ins = dyn_cast<Instruction>(unsuppOp)) {
									errs() << "    "<<ins->getOpcodeName() <<"\n";
									errs() <<"                ("<<*Inst<<")\n";
									errs() <<"                ("<<*ins<<")\n";
								}
								else {
									errs() << "    "<<*unsuppOp <<"\n";
									errs() <<"                ("<<*Inst<<")\n";
									errs() <<"                ("<<*unsuppOp<<")\n";
								}
								break;
							}
						}
				}
			
			}
		}
	}
	
	void AnnotationPropagationPass::propagateStore(Instruction* I) {
		StoreInst *SI = nullptr;
       ConstantRangeUtils *crUtils;
		DEBUG(errs() << "propagateStore I: "<<*I<<"\n");
		if ((SI = dyn_cast<StoreInst>(I))) {
		    LLVMContext& context = I->getContext();
		    Value *op = SI->getOperand(0);
		    ConstantRange* CR  = nullptr;
		  
		    if(Instruction* o1 = dyn_cast<Instruction>(op)) {
				if(o1->getMetadata("acsl_range") ) {
					MDNode *md = dyn_cast<MDNode>(o1->getMetadata("acsl_range"));
					CR = crUtils->getConstantRange(md);
			
				}
				else 
					//profileUnsupportedOps(o1);
				if(CR) {
					DEBUG(errs()<<CR << "\n";);
					APInt lo = CR->getLower();
					DEBUG(errs()<< lo << "\n";);
					APInt hi = CR->getUpper();
					ConstantInt* hiC = ConstantInt::get(context, hi);
					int64_t hig=hiC->getSExtValue();
					ConstantInt* loC = ConstantInt::get(context, lo);
					int64_t low=loC->getSExtValue();
					
					//now add the result metadata to the instruction
					std::ostringstream o;
					o << "assert val >= " << low << " && "<<"val <= "<<(hig-1);
						
					std::string metadata = o.str();
					DEBUG(errs()<< metadata << "\n";);
						
					MDNode* V = MDNode::get(context, MDString::get(context, metadata));
					SI->setMetadata("acsl_range", V);
					DEBUG(errs()<< *SI << "\n";);
				}
			}
			else if(ConstantInt* CI = dyn_cast<ConstantInt>(op)) {
				int64_t val = CI->getSExtValue();
				std::ostringstream o;
				o<< "assert val == "<<val;
				std::string metadata = o.str();
				DEBUG(errs()<< metadata << "\n";);
				MDNode* V = MDNode::get(context, MDString::get(context, metadata));
				SI->setMetadata("acsl_range", V);
				DEBUG(errs()<< *SI << "\n";);
			}
		}
	}
	
	void AnnotationPropagationPass::propagateBinOp(Instruction* I) {
		
		BinaryOperator* binop=nullptr;
		binop = dyn_cast<BinaryOperator>(I);
		LLVMContext& context = binop->getContext();
		
		//DEBUG(errs()<<"ADD: "<< *I<<"\n";);
		DEBUG(errs()<<I->getOpcodeName()<<": "<< *I<<"\n";);
		
		Value* op1 = binop->getOperand(0);
		Value* op2 = binop->getOperand(1);
		DEBUG(errs()<<"  Op1: "<< *op1<<"\n";);
		DEBUG(errs()<<"  Op2: "<< *op2<<"\n";);
		ConstantRange* CR1=nullptr;
		ConstantRange*  CR2=nullptr;
		ConstantRangeUtils* crUtils = new ConstantRangeUtils();
		if(Instruction* o1 = dyn_cast<Instruction>(op1)) {
			if(o1->getMetadata("acsl_range") ) {
			/*	if(o1->getName() == "add1303")
					errs()<<"hohoho";*/
				MDNode *md = dyn_cast<MDNode>(o1->getMetadata("acsl_range"));
				CR1 = crUtils->getConstantRange(md);
				APInt l = CR1->getLower();
				DEBUG(errs() << *CR1 << "\n";);
				DEBUG(errs() << l << "\n";);
			}
			else { 
				//profileUnsupportedOps(binop);
			}
		}
		else { //it is not an instruction, it may be a constant //How about Variable . Is AllocInst also a type of Instruction ???
			if(ConstantInt* CI = dyn_cast<ConstantInt>(op1)) {
				
				int64_t val = CI->getSExtValue();
				APInt* lower = new APInt(64, val, true);
				APInt* upper = new APInt(64, val+1, true);
				CR1=new ConstantRange(*lower,*upper);
			}
		}
		if(Instruction* o2 = dyn_cast<Instruction>(op2)) {
			if(o2->getMetadata("acsl_range") ) {
				MDNode *md = dyn_cast<MDNode>(o2->getMetadata("acsl_range"));
				DEBUG(errs()<<*md<<"\n";);
				CR2 = crUtils->getConstantRange(md);
				DEBUG(errs() << *CR2 << "\n";);
			}
			else { //remove
				//profileUnsupportedOps(binop);
			}
		}
		else { //it is not an instruction, it may be a constant
			if(ConstantInt* CI = dyn_cast<ConstantInt>(op2)) {
				int64_t val = CI->getSExtValue();
				APInt* lower = new APInt(64, val, true);
				APInt* upper = new APInt(64, val+1, true);
				CR2=new ConstantRange(*lower,*upper);
			}
		}
		if(CR1 && CR2) {
			APInt l1 = CR1->getLower();
			APInt l2 = CR2->getLower();
			APInt u1 = CR1->getUpper();
			APInt u2 = CR2->getUpper();
			DEBUG(errs() << "l1: " << l1.getBitWidth() <<"\n";);
			DEBUG(errs() << "u1: " << u1.getBitWidth() <<"\n";);
			DEBUG(errs() << "l2: " << l2.getBitWidth() <<"\n";);
			DEBUG(errs() << "u2: " << u2.getBitWidth() <<"\n";);
			
		    ConstantRange result = *CR1; //since there is no default constructor ConstantRange result; does not work
										//initializing to *CR1, as there is no other way
			
			switch (binop->getOpcode())
			{ 
			  default:
				 break;
			  case (Instruction::Add): 
				 result = CR1->add(*CR2);
				 break;
              
			  case (Instruction::Sub): 
				 result = CR1->sub(*CR2);
				 break;
			
			  case (Instruction::Mul): 
				 result = CR1->multiply(*CR2);
				 break;
				 
               case (Instruction::UDiv): case (Instruction::SDiv):
				 result = CR1->udiv(*CR2); //TODO :may have to change here
				 break;
				 
			   case (Instruction::URem): case(Instruction::SRem): 
			     ConstantRange *CR3 = new ConstantRange(APInt::getNullValue(CR2->getBitWidth()),CR2->getUpper());
				 result = *CR3;
				 break;
			  
			}
			
			DEBUG(errs()<<result << "\n";);
			APInt lo = result.getLower();
			DEBUG(errs()<< lo << "\n";);
			APInt hi = result.getUpper();
			ConstantInt* hiC = ConstantInt::get(context, hi);
			int64_t hig=hiC->getSExtValue();
			ConstantInt* loC = ConstantInt::get(context, lo);
			int64_t low=loC->getSExtValue();
			
			//now add the result metadata to the instruction
			std::ostringstream o;
			o << "assert val >= " << low << " && "<<"val <= "<<(hig-1);
			
			std::string metadata = o.str();
			DEBUG(errs()<< metadata << "\n";);
			
			//char* metadataC = strdup(metadata.c_str());
			//binop->annotation=metadataC;
			
			MDNode* V = MDNode::get(context, MDString::get(context, metadata));
			binop->setMetadata("acsl_range", V);
			DEBUG(errs()<< *binop<< "\n";);
			
		}
	}
	
	/*
	void AnnotationPropagationPass::propagateAdd(BinaryOperator* I) {
		
		LLVMContext& context = I->getContext();
		errs()<<"ADD: "<< *I<<"\n";
		Value* op1 = I->getOperand(0);
		Value* op2 = I->getOperand(1);
		errs()<<"  Op1: "<< *op1<<"\n";
		errs()<<"  Op2: "<< *op2<<"\n";
		ConstantRange* CR1=nullptr;
		ConstantRange*  CR2=nullptr;
		ConstantRangeUtils* crUtils = new ConstantRangeUtils();
		if(Instruction* o1 = dyn_cast<Instruction>(op1)) {
			if(o1->getMetadata("acsl_range") ) {
				MDNode *md = dyn_cast<MDNode>(o1->getMetadata("acsl_range"));
				CR1 = crUtils->getConstantRange(md);
				APInt l = CR1->getLower();
				DEBUG(errs() << *CR1 << "\n";);
				DEBUG(errs() << l << "\n";);
			}
			else { 
				profileUnsupportedOps(I);
				//const char* name = o1->getOpcodeName();
				//errs() << "opcodename1: "<<name;
			}
		}
		else { //it is not an instruction, it may be a constant
			if(ConstantInt* CI = dyn_cast<ConstantInt>(op1)) {
				
			}
		}
		if(Instruction* o2 = dyn_cast<Instruction>(op2)) {
			if(o2->getMetadata("acsl_range") ) {
				MDNode *md = dyn_cast<MDNode>(o2->getMetadata("acsl_range"));
				DEBUG(errs()<<*md<<"\n";);
				CR2 = crUtils->getConstantRange(md);
				DEBUG(errs() << *CR2 << "\n";);
			}
			else { //remove
				const char* name = o2->getOpcodeName();
				errs() << "opcodename2: "<<name;
			}
		}
		else { //it is not an instruction, it may be a constant
			
		}
		if(CR1 && CR2) {
			APInt l1 = CR1->getLower();
			APInt l2 = CR2->getLower();
			APInt u1 = CR1->getUpper();
			APInt u2 = CR2->getUpper();
			DEBUG(errs() << "l1: " << l1.getBitWidth() <<"\n";);
			DEBUG(errs() << "u1: " << u1.getBitWidth() <<"\n";);
			DEBUG(errs() << "l2: " << l2.getBitWidth() <<"\n";);
			DEBUG(errs() << "u2: " << u2.getBitWidth() <<"\n";);
			ConstantRange result = CR1->add(*CR2);
			DEBUG(errs()<<result << "\n";);
			APInt lo = result.getLower();
			DEBUG(errs()<< lo << "\n";);
			APInt hi = result.getUpper();
			ConstantInt* hiC = ConstantInt::get(context, hi);
			int64_t hig=hiC->getSExtValue();
			ConstantInt* loC = ConstantInt::get(context, lo);
			int64_t low=loC->getSExtValue();
			
			//now add the result metadata to the instruction
			std::ostringstream o;
			o << "assert val >= " << low << " && "<<"val <= "<<(hig-1);
			
			std::string metadata = o.str();
			DEBUG(errs()<< metadata << "\n";);
			
			MDNode* V = MDNode::get(context, MDString::get(context, metadata));
			I->setMetadata("acsl_range", V);
			DEBUG(errs()<< *I << "\n";);
			
		}
	}
	*/
	
	
	
	//the sext instruction is created by LLVM when we cast a signed value in C
	//the zext instruction is created by LLVM when we cast an unsigned value in C
	//Hence, we need not worry that the range metadata will be changed(in terms of signed int).
	void AnnotationPropagationPass::propagateCastInst(Instruction *I)
	{
		CastInst *CI = nullptr;
       ConstantRangeUtils *crUtils;
		DEBUG(errs() << "propagateCastInst, I: "<<*I<<"\n");
		if ((CI = dyn_cast<CastInst>(I))) {
		    LLVMContext& context = I->getContext();
		    Value *op = CI->getOperand(0);
		    ConstantRange* CR  = nullptr;
		  
		    if(Instruction* o1 = dyn_cast<Instruction>(op)) {
				if(o1->getMetadata("acsl_range") ) {
					MDNode *md = dyn_cast<MDNode>(o1->getMetadata("acsl_range"));
					CR = crUtils->getConstantRange(md);
			
				}
				else 
					//profileUnsupportedOps(o1);
				if(CR) {
					DEBUG(errs()<<CR << "\n";);
					APInt lo = CR->getLower();
					DEBUG(errs()<< lo << "\n";);
					APInt hi = CR->getUpper();
					ConstantInt* hiC = ConstantInt::get(context, hi);
					int64_t hig=hiC->getSExtValue();
					ConstantInt* loC = ConstantInt::get(context, lo);
					int64_t low=loC->getSExtValue();
					
					//now add the result metadata to the instruction
					std::ostringstream o;
					o << "assert val >= " << low << " && "<<"val <= "<<(hig-1);
						
					std::string metadata = o.str();
					DEBUG(errs()<< metadata << "\n";);
						
					MDNode* V = MDNode::get(context, MDString::get(context, metadata));
					CI->setMetadata("acsl_range", V);
					DEBUG(errs()<< *CI << "\n";);
				
				}
			}
		}
		
	else
	{
	//TODO: If the instruction passed is not a Cast Instruction, how to return
	return;		  
	}
		
    }
	
	
		bool AnnotationPropagationPass::runOnFunction(Function &F) {
			//mapAllocaStores = new DenseMap<AllocaInst*, StoreInst*>();
			//for every instruction
			//		check if it has acsl_range metadata attached (if it does we do not touch it, since these are produced by Frama-C)		
			//		if not switch case the type of instruction
			//			for each operand, 
			//				if it is a load get attached acsl_range metadata
			//			if all operands have attached metadata, 
			//				produce the ConstantRange classes
			//				merge them
			//				get the merge result and parse it to acsl_range metadata
			//				attach it to the instruction
			//			
			for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
				//errs() <<"I: "<<*I << "\n";
				if(StoreInst* store = dyn_cast<StoreInst>(&*I)) { //keep a map for the profiling of the unsupported operations
					stores.push_back(store);
					/*Value* op2 = store->getOperand(1);
					if(AllocaInst* alloca = dyn_cast<AllocaInst>(op2))
						errs()<<*alloca <<"\n";
						errs()<<*store <<"\n";
						//kot[6] = 7;
							if(mapAllocaStores.count(alloca) == 0)
							mapAllocaStores[alloca] = store;
						else {
							mapAllocaStores.erase(alloca);
							mapAllocaStores[alloca] = store;
						}*/
						
				}
				if(!I->getMetadata("acsl_range")) {
				/*	Instruction* inst = dyn_cast<Instruction>(&*I);
					BinaryOperator* binop=nullptr;
					switch(I->getOpcode()) {
						case Instruction::Add:
							binop = dyn_cast<BinaryOperator>(inst);
							propagateAdd(binop);
							break;
						case Instruction::Mul:
							propagateMul(inst);
							break;
						
					}*/
					Instruction* inst = dyn_cast<Instruction>(&*I);
					switch(I->getOpcode()) {
						case (Instruction::Add): case (Instruction::Mul): case (Instruction::Sub): case (Instruction::UDiv): 
					    case (Instruction::SDiv): case (Instruction::URem): case(Instruction::SRem):
							propagateBinOp(inst);
							break;
							
						case (Instruction::SExt): case (Instruction::ZExt):
			              propagateCastInst(inst);
							break;
						case(Instruction::Store):
							propagateStore(inst);
							break;
						
					}
				}
			}
			return true;
		}
	
	
	
}

char AnnotationPropagationPass::ID = 0;
static RegisterPass<AnnotationPropagationPass> X("annotation-propagation", "ACSL Annotation Propagation Pass");
