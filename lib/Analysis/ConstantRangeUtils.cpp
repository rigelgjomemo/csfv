#define DEBUG_TYPE "constantrangeutils"

#include "llvm/Analysis/ConstantRangeUtils.h"
#include "llvm/IR/ConstantRange.h"
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

#include "llvm/Analysis/node.h"
#include "llvm/Analysis/driver.h" 
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/CFG.h"
//#include "safecode/ranges.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"

#include <stack>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <map>
#include <algorithm>


namespace llvm
{

	ConstantRangeUtils::ConstantRangeUtils() {}

	ConstantRangeUtils::~ConstantRangeUtils() {}

	ConstantRange* ConstantRangeUtils::getConstantRange(MDNode* md) {
				DEBUG(errs()<<*md<<"\n";);
				MDString*  metadataS = dyn_cast<MDString>(md->getOperand(0));
				std::string annotation = metadataS->getString();
				ACSLStatements root;
				example::Driver driver(root);
				DEBUG(errs()<<"Parsing: "<<annotation<<"\n";);
				bool result = driver.parse_string(annotation, "input");
		//driver.root is an ACSLStatements class.
		//ACSLStatements contains an std::vector<ACSLStatement*> called statements
		//we are iterating over this vector. so istmt is an ACSLStatement*
		for (std::vector<ACSLStatement*>::iterator istmt = driver.root.statements.begin(); 
					istmt != driver.root.statements.end(); ++istmt) {
			ACSLStatements root2;
			example::Driver driver2(root2);
			if(ACSLAssertStatement * stmt = dyn_cast<ACSLAssertStatement>((*istmt))){
				//auto sp = getRangePair(&(stmt->exp));
				if (ACSLBinaryExpression * binexp = llvm::dyn_cast<ACSLBinaryExpression>(&(stmt->exp))) {
				//case 1:"==";
				//case 3:"<";
				//case 4:">";
				//case 5:"<=";
				//case 6:">=";
				//case 7:"&&";

				//if it is a constant (ex. val==const):
				if(binexp->op==1){
					if (ACSLIdentifier * id = llvm::dyn_cast<ACSLIdentifier>(&(binexp->lhs))) {
						if (ACSLInteger * value = llvm::dyn_cast<ACSLInteger>(&(binexp->rhs))) {
							//APInt(numBits, number, isSigned = false). 
							//going with bit width of 64 for now and signed
							APInt* lower = new APInt(64, value->value, true); 
							APInt* upper = new APInt(64, value->value, true);
							DEBUG(errs()<< "lower.bitwidth: " << lower->getBitWidth()<< "\n";);
							DEBUG(errs()<< "upper.bitwidth: " << upper->getBitWidth()<< "\n";);
							ConstantRange* CR = new ConstantRange(*lower, *upper);
							//ranges::Range p(value->value, value->value);
							//std::pair<std::string, ranges::Range > sp(id->name, p);
							return CR;
						}
					}
				}
				//if it is a range exp (ex. val>=const1 && val<=const2)
				if(binexp->op==7){
					if(ACSLBinaryExpression * binexp1 = llvm::dyn_cast<ACSLBinaryExpression>(&(binexp->lhs))){
						if(ACSLBinaryExpression * binexp2 = llvm::dyn_cast<ACSLBinaryExpression>(&(binexp->rhs))){
						//TODO: handle also expressions with the first operand constant (ex. 1<=val)
							if (ACSLIdentifier * id1 = llvm::dyn_cast<ACSLIdentifier>(&(binexp1->lhs))) {
								if (ACSLInteger * constant1 = llvm::dyn_cast<ACSLInteger>(&(binexp1->rhs))) {
									if (ACSLIdentifier * id2 = llvm::dyn_cast<ACSLIdentifier>(&(binexp2->lhs))) {
										if (ACSLInteger * constant2 = llvm::dyn_cast<ACSLInteger>(&(binexp2->rhs))) {
											int op1 = binexp1->op;
											int op2 = binexp2->op;
											//std::string name1 = id1->name; std::string name2 = id2->name;
											//int integer2 = constant2->value; it should be about the same var
											//if(name1==name2){
											//TODO: handle different combinations of <, >, ...
											if(op1==6 && op2==5){
												//ranges::Range p(integer1, integer2);
												APInt lower(64, constant1->value, true);
												APInt upper(64, constant2->value+1, true);
												ConstantRange* CR = new ConstantRange(lower, upper);
												return CR;
											}
										}
									}
								}
							}
						}
					}
				}
				}
			}
		}
	}

	
	/**
	 * [a,b] <u [c,d]
	 * [a,b] <s [c,d]
	 */
	 
/*
	ConstantRangeUtils::Result ConstantRangeUtils::tryConstantFoldCMP(unsigned Predicate, Value* LHS, Value* RHS) {

		
		ConstantRange* CR_LHS = nullptr;
		ConstantRange* CR_RHS = nullptr;
		Instruction* I = nullptr;
		
		if(I = dyn_cast<Instruction>(LHS)) {
			if(I->getMetadata("acsl_range")) {
				MDNode *md = dyn_cast<MDNode>(I->getMetadata("acsl_range"));
				DEBUG(errs()<<*md<<"\n";);
				CR_LHS = getConstantRange(md);
				DEBUG(errs()<<*CR_LHS << "\n";);
			}
		}
  
		if(I = dyn_cast<Instruction>(RHS)) {
			if(I->getMetadata("acsl_range")) {
				MDNode *md = dyn_cast<MDNode>(I->getMetadata("acsl_range"));
				DEBUG(errs()<<*md<<"\n";);
				CR_RHS = getConstantRange(md);
				DEBUG(errs()<<*CR_RHS << "\n";);
			}
		}
		if (CR_LHS && CR_RHS) {
			switch(Predicate) {
				case ICmpInst::ICMP_SLT:
				{
					ConstantRange intersect = CR_LHS->intersectWith(*CR_RHS);
					if(intersect.isEmptySet()) { //they do not intersect, figure out which one is higher
						APInt lhs_upper =  CR_LHS->getUpper();
						APInt rhs_lower =  CR_RHS->getLower();
						if(lhs_upper.slt(rhs_lower))
							return ConstantRangeUtils::TRUE;
						else
							return ConstantRangeUtils::FALSE;
					}
					else
						return ConstantRangeUtils::UNKNOWN;
				}
				case ICmpInst::ICMP_ULT: //[a,b] <u [c,d]
				ConstantRange intersect = CR_LHS->intersectWith(*CR_RHS);
				if(intersect.isEmptySet()) { //they do not intersect, figure out which one is higher
						APInt lhs_upper =  CR_LHS->getUpper();
						APInt rhs_lower =  CR_RHS->getLower();
						if(lhs_upper.ult(rhs_lower))
							return ConstantRangeUtils::TRUE;
						else
							return ConstantRangeUtils::FALSE;
					}
					else
						return ConstantRangeUtils::UNKNOWN;
				 
			}
		}
		return ConstantRangeUtils::UNKNOWN;
	}
 
 */
 
 ConstantRangeUtils::Result ConstantRangeUtils::foldConstantRanges(unsigned Predicate, ConstantRange* CR_LHS, ConstantRange* CR_RHS) {
		APInt lhs_upper,lhs_lower,rhs_lower,rhs_upper;
		ConstantRange intersect = CR_LHS->intersectWith(*CR_RHS);
		if(intersect.isEmptySet()) { //they do not intersect, figure out which one is higher
			lhs_upper =  CR_LHS->getUpper();
			lhs_lower =  CR_LHS->getLower();
			rhs_lower = CR_RHS->getLower();
			rhs_upper =  CR_RHS->getUpper();
			
		}
		else
			return ConstantRangeUtils::UNKNOWN;
		errs()<<Predicate<<"\n";
		switch(Predicate) {
			case ICmpInst::ICMP_SLT: //[a,b] <s [c,d]
				if(lhs_upper.slt(rhs_lower))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
		
		case ICmpInst::ICMP_SLE: //[a,b] <=s [c,d]
			if(lhs_upper.sle(rhs_lower))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
		
		case ICmpInst::ICMP_ULT: //[a,b] <u [c,d]
			if(lhs_upper.ult(rhs_lower))
					return ConstantRangeUtils::TRUE;
			else
					return ConstantRangeUtils::FALSE;
		
		case ICmpInst::ICMP_ULE: //[a,b] <=u [c,d]
			if(lhs_upper.ule(rhs_lower))
					return ConstantRangeUtils::TRUE;
			else
					return ConstantRangeUtils::FALSE;
		
		case ICmpInst::ICMP_SGT: //[a,b] >s [c,d]
			if(lhs_lower.sgt(rhs_upper))
					return ConstantRangeUtils::TRUE;
			else
					return ConstantRangeUtils::FALSE;
	
		case ICmpInst::ICMP_UGT: //[a,b] >u [c,d]
			if(lhs_lower.ugt(rhs_upper))
					return ConstantRangeUtils::TRUE;
			else
					return ConstantRangeUtils::FALSE;
		
		case ICmpInst::ICMP_SGE: //[a,b] >=s [c,d]
			if(lhs_lower.sge(rhs_upper))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
		case ICmpInst::ICMP_UGE: //[a,b] >=u [c,d]
			if(lhs_lower.uge(rhs_upper))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
		case ICmpInst::ICMP_NE: //[a,b] <> [c,d]
			 //the fact that the ranges do not intersect in the first place trivially makes this case true
			 return ConstantRangeUtils::TRUE;
		
		case ICmpInst::ICMP_EQ:
			return ConstantRangeUtils::FALSE;
		}
		
		return ConstantRangeUtils::UNKNOWN;
	}

ConstantRangeUtils::Result ConstantRangeUtils::foldCRHS(unsigned Predicate, ConstantInt* CI_LHS, ConstantRange* CR_RHS) {
	APInt ai = CI_LHS->getValue();
	bool intersect = CR_RHS->contains(ai);
	
	if(intersect)
		return ConstantRangeUtils::UNKNOWN;
	APInt rhs_upper = CR_RHS->getUpper();
	APInt rhs_lower = CR_RHS->getLower();
	switch(Predicate) {
			case ICmpInst::ICMP_SLT: //c <s [a,b]
				if(ai.slt(rhs_lower))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			 break;
			 
		case ICmpInst::ICMP_SLE: //c <=s [a,b]
			if(ai.sle(rhs_lower))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
			 
		case ICmpInst::ICMP_ULT: //c <u [a,b]
			if(ai.ult(rhs_lower))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
		
		case ICmpInst::ICMP_ULE: //c <=u [a,b]
			if(ai.ule(rhs_lower))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
		
		case ICmpInst::ICMP_SGT: //c >s [a,b]
			if(ai.sgt(rhs_upper))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
			
		case ICmpInst::ICMP_UGT: //c >u [a,b]
			if(ai.ugt(rhs_upper))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
		
		case ICmpInst::ICMP_SGE: //c >=s [a,b]
			if(ai.sge(rhs_upper))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
		
		case ICmpInst::ICMP_UGE: //[a,b] >=u [c,d]
			if(ai.uge(rhs_upper))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			 break;
		
		case ICmpInst::ICMP_NE: //[a,b] <> [c,d]
			 //the fact that the ranges do not intersect in the first place trivially makes this case true
			 return ConstantRangeUtils::TRUE;
			 break;
		}
		return ConstantRangeUtils::UNKNOWN;
}

ConstantRangeUtils::Result ConstantRangeUtils::foldLHSC(unsigned Predicate, ConstantRange* CR_LHS, ConstantInt* CI_RHS) {
	APInt ai = CI_RHS->getValue();
	bool intersect = CR_LHS->contains(ai);
	
	if(intersect)
		return ConstantRangeUtils::UNKNOWN;
	APInt lhs_upper = CR_LHS->getUpper();
	APInt lhs_lower = CR_LHS->getLower();
	switch(Predicate) {
			case ICmpInst::ICMP_SLT: //[a,b] <s c
				if(lhs_upper.slt(ai))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			 break;
			 
		case ICmpInst::ICMP_SLE: //[a,b] <=s c
			if(lhs_upper.sle(ai))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
			 
		case ICmpInst::ICMP_ULT: //[a,b] <u c
			if(lhs_upper.ult(ai))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
		
		case ICmpInst::ICMP_ULE: //[a,b] <=u [c
			if(lhs_upper.ule(ai))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
		
		case ICmpInst::ICMP_SGT: //[a,b] >s c
			if(lhs_lower.sgt(ai))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
			
		case ICmpInst::ICMP_UGT: //[a,b] >u c
			if(lhs_lower.ugt(ai))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
		
		case ICmpInst::ICMP_SGE: //[a,b] >=s [c,d]
			if(lhs_lower.sge(ai))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			break;
		
		case ICmpInst::ICMP_UGE: //[a,b] >=u [c,d]
			if(lhs_lower.uge(ai))
					return ConstantRangeUtils::TRUE;
				else
					return ConstantRangeUtils::FALSE;
			 break;
		
		case ICmpInst::ICMP_NE: //[a,b] <> [c,d]
			 //the fact that the ranges do not intersect in the first place trivially makes this case true
			 return ConstantRangeUtils::TRUE;
			 break;
		}
		return ConstantRangeUtils::UNKNOWN;
	}

 ConstantRangeUtils::Result ConstantRangeUtils::tryConstantFoldCMP(unsigned Predicate, Value* LHS, Value* RHS) {
		ConstantRange* CR_LHS = nullptr;
		ConstantRange* CR_RHS = nullptr;
		ConstantInt* CI_LHS = nullptr;
		ConstantInt* CI_RHS = nullptr;
		Instruction* I = nullptr;
		
		if(I = dyn_cast<Instruction>(LHS)) {
			if(I->getMetadata("acsl_range")) {
				MDNode *md = dyn_cast<MDNode>(I->getMetadata("acsl_range"));
				DEBUG(errs()<<*md<<"\n";);
				CR_LHS = getConstantRange(md);
				DEBUG(errs()<<*CR_LHS << "\n";);
			}
		}
		else if(ConstantInt* CI = dyn_cast<ConstantInt>(LHS)) {
			//CI_LHS=CI;
			int64_t val = CI->getSExtValue();
			APInt* lower = new APInt(64, val, true);
			APInt* upper = new APInt(64, val+1, true);
			CR_LHS=new ConstantRange(*lower,*upper);
			
		}

		if(I = dyn_cast<Instruction>(RHS)) {
			if(I->getMetadata("acsl_range")) {
				MDNode *md = dyn_cast<MDNode>(I->getMetadata("acsl_range"));
				DEBUG(errs()<<*md<<"\n";);
				CR_RHS = getConstantRange(md);
				DEBUG(errs()<<*CR_RHS << "\n";);
			}
		}
		else if(ConstantInt* CI = dyn_cast<ConstantInt>(RHS)) {
			//CI_RHS=CI;
			int64_t val = CI->getSExtValue();
			APInt* lower = new APInt(64, val, true);
			APInt* upper = new APInt(64, val+1, true);
			CR_RHS=new ConstantRange(*lower,*upper);
		}
		
		if (CR_LHS && CR_RHS)
			return foldConstantRanges(Predicate, CR_LHS, CR_RHS);
		return ConstantRangeUtils::UNKNOWN;
}
 
 
 }

