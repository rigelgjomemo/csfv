//===- InstCombineConstRange.cpp ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the visitInstCombCR() function.
//
//===----------------------------------------------------------------------===//


#define DEBUG_TYPE "instcombine"
//#include "llvm/Transforms/InstCombine.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/ConstantRange.h"

#include <iostream>
using namespace llvm;
using namespace PatternMatch;
using namespace std;

// Macro to economize space in generating legal products between class APInt multipliers
#define APIproduct(dest, m0, m1) \
	tmp0 = m0;\
	tmp1 = m1;\
	testW = tmp0.getBitWidth()+tmp1.getBitWidth();\
	tmp0 = tmp0.sext(testW);\
	tmp1 = tmp1.sext(testW);\
	dest = dest.sext(testW);\
	dest.operator=(1);\
	dest.operator*=(tmp0);\
	dest.operator*=(tmp1)

//??????????????
// The logic in this function assumes that for the range [a,b) that b>=c. 
//
// TODO: check that this implementation is logically designed to include a in the range
//  and exclude b from the range
//
// Combine components that have constant ranges specified
//
//Instruction *InstCombiner::visitInstCombCR(Instruction &CRI) {
int	main(void) {

	APInt MyLowr;	// Memory for the result; TODO: allocate this memory
	APInt MyUpr;
	APInt *newLowr=&MyLowr;
	APInt *newUpr=&MyUpr;
	Instruction *CRI;
/*
 * 	// Extract opcode and operands from the instruction passed in
	int OpCode = CRI.getOpcode();
	Value *Op0 = CRI.getOperand(0);
	Value *Op1 = CRI.getOperand(1);
	Value *Op2 = CRI.getOperand(2);
*/
	std::string outstr;

	// TESTING: Here to read in lower and uppr for 3 operands
	int l0, u0, l1, u1, l2, u2;
	cin >> l0 >> u0;
	cin >> l1 >> u1;
	cin >> l2 >> u2;
	int OpCode = 12;
	APInt lower(16, l0, 1);	// numBits, value, isSigned
	APInt upper(16, u0, 1);
	//lower.operator=(7);
	//upper.operator=(21);
	
	// Rigel will provide the interface:
	//ConstantRange *CR1 =InstCombCR dyn_cast<ConstantRange>(getMetadata(Op0);
	// This is a placeholder; default is max range for type
	ConstantRange CR0(lower, upper); // = ConstantRange::ConstantRange;
	// NOTE: a is included in range and b is excluded: the iterval is [a,b)
	//
	// ALSO note the class supports wraparound as in these examples:
	
	// BOOLEANS:
	//  [F, F) = {}     = Empty set
	//  [T, F) = {T}
	//  [F, T) = {F}
	//  [T, T) = {F, T} = Full set
	//
	// 8-bit types:
	// [0, 0)     = {}       = Empty set
	// [255, 255) = {0..255} = Full Set


	APInt a = CR0.getLower();	// lower and upper have same BitWidth
	APInt b = CR0.getUpper();
	//printf()

	// Rigel will provide the interface:
	//ConstantRange *CR1 = dyn_cast<ConstantRange>(getMetadata(Op0);
	// This is a placeholder; default is max range for type
	APInt lower1(8, l1, 1);	// numBits, value, isSigned
	APInt upper1(8, u1, 1);
	//lower1.operator=(55);
	//upper1.operator=(127);
	ConstantRange CR1(lower1, upper1); // = ConstantRange::ConstantRange;
	APInt c = CR1.getLower();
	APInt d = CR1.getUpper();
	
	// Rigel will provide the interface:
	//ConstantRange *CR1 = dyn_cast<ConstantRange>(getMetadata(Op0);
	// This is a placeholder; default is max range for type
	APInt lower2(8, l2, 1);	// numBits, value, isSigned
	APInt upper2(8, u2, 1);
	//lower2.operator=(-63);
	//upper2.operator=(-11);
	ConstantRange CR2(lower2, upper2); // = ConstantRange::ConstantRange;
	APInt e = CR2.getLower();
	APInt f = CR2.getUpper();
	
	// Be sure Bitwidths work nice together. ConstantRange constructor ensures a, b same 
	// width, c, d same width, e, f same width.
	uint32_t width0 = a.getBitWidth();
	uint32_t width1 = c.getBitWidth();
	uint32_t width2 = e.getBitWidth();
	if (width0 >= width1) {
		if (width0 >= width2) {	// width0 wins for everyone
			c = c.sext(width0);
			d = d.sext(width0);
			e = e.sext(width0);
			f = f.sext(width0);
		} else {	// width2 wins for everyone
			a = a.sext(width2);
			b = b.sext(width2);			
			c = c.sext(width2);
			d = d.sext(width2);			
		}
	} else if (width1 >= width2) {
		a = a.sext(width1);
		b = b.sext(width1);
		e = e.sext(width1);
		f = f.sext(width1);
	} else {
			a = a.sext(width2);
			b = b.sext(width2);			
			c = c.sext(width2);
			d = d.sext(width2);					
	}
	uint32_t newWidth = (width0>width1)?width0:width1;
	//printf("width0=%d, width1=%d, newWidth=%d\n", width0, width1, newWidth);
	//printf("newLowr->BitWidth=%d\n", newLowr->getBitWidth());
	MyLowr = newLowr->sext(newWidth);	// Assign APInt with modified width to local variable
	MyUpr = newUpr->sext(newWidth);		// Assign APInt with modified width to local variable
	//printf("newLowr->BitWidth=%d\n", newLowr->getBitWidth());

	// temporary variables
	APInt test0, test1;
	APInt tmp0, tmp1;	// use in macro to avoid side effects
	uint32_t testW;

checkmehere:
	switch(OpCode){
	default:
		OpCode++;
		printf("Opcode=%d\n", OpCode);
		goto checkmehere;
		break;
	case Instruction::Add:	// 8
		printf("ADD(%d)\n", OpCode);
		// What to do in overflow/underflow/wraparound?	
		//printf("isSingleWOrd+\m",)
		//printf("newLowr->BitWidth=%d, a.BitWidth=%d\n",newLowr->getBitWidth(), a.getBitWidth());
		newLowr->operator=(a);
		newLowr->operator+=(c);
		newUpr->operator=(b);
		newUpr->operator+=(d);
		//cout << 
		break;

	case Instruction::Sub:	// 10
		printf("SUB(%d)\n", OpCode);
		// What to do in overflow/underflow/wraparound?
		newLowr->operator=(a);
		newLowr->operator-=(c);
		newUpr->operator=(b);
		newUpr->operator-=(d);
		break;

	case Instruction::Mul:	// 12
		printf("MUL(%d)\n", OpCode);
		if (a.isNonNegative()) {
			if (c.isNonNegative()) {
				APIproduct(MyLowr, a, c);
				APIproduct(MyUpr, b, d);
			} else {
				APIproduct(MyLowr, b, c);
				if (d.isNegative()) {
					APIproduct(MyUpr, a, d);
				} else APIproduct(MyUpr, b, d);
			}
		} else {
			if (c.isNonNegative()) {
				APIproduct(MyLowr, a, d);
				if (b.isNegative()) {
					// Something is wrong, we should NOT need a break at the end of this leg!
					// NOT a problem with the macro, it still happens with the code written out
					APIproduct(MyUpr, b, c);
					/*
					printf("upr bitw = %d\n",MyUpr.getBitWidth());
					tmp0 = b;
					tmp1 = c;
					testW = tmp0.getBitWidth()+tmp1.getBitWidth();
					tmp0 = tmp0.sext(testW);
					tmp1 = tmp1.sext(testW);
					MyUpr = MyUpr.sext(testW);
					MyUpr.operator=(1);
					cout << "MyUpr value: " << MyUpr.toString(10, true) << "\n";
					MyUpr.operator*=(tmp0);
					cout << "MyUpr value: " << MyUpr.toString(10, true) << "\n";
					MyUpr.operator*=(tmp1);
					cout << "MyUpr value: " << MyUpr.toString(10, true) << "\n";
					printf("upr bitw = %d\n",MyUpr.getBitWidth());
					outstr = newUpr->toString(10, true);
					cout << "new Upper = " << outstr << "\n";
					cout << "MyUpr value: " << MyUpr.toString(10, true) << "\n";
					printf("COMPARE!!!\n");
					 * */
					break;
				} else
					APIproduct(MyUpr, b, d);
			} else if (b.isNegative()) {
				APIproduct(MyUpr, a, c);
				if (d.isNonNegative()) {
					APIproduct(MyLowr, a, d);
				} else {
					APIproduct(MyLowr, b, d);
				}
			} else if (d.isNegative()) {
				APIproduct(MyUpr, a, c);
				APIproduct(MyLowr, b, c);
			} else {
				// Need to compare these two products
				test0 = test0.sext(newWidth);
				test1 = test1.sext(newWidth);
				printf("test0 width=%d\n", test0.getBitWidth());
				APIproduct((test0), a, c);
				APIproduct((test1), b, d);
				if (test0.sgt(test1)) {
					newUpr->operator=(test0);
				} else {
					newUpr->operator=(test1);
				}
				test0 = a; test1 = a; // re-initialize widths
				APIproduct((test0), a, d);
				APIproduct((test1), b, c);
				if (test0.slt(test1)) {
					newLowr->operator=(test0);
				} else {
					newLowr->operator=(test1);
				}
			}
		} 
		break;
		
	case Instruction::SDiv:	// 15
		printf("SDIV(%d)\n", OpCode);
		if (a.isNonNegative()) {
			if (c.isStrictlyPositive()) {
				*newLowr = (a.sdiv(d));
				*newUpr = (b.sdiv(c));
			} else {
				if (d.isNegative()) {
					*newLowr = b.sdiv(d);
					*newUpr = a.sdiv(c);
				} else {
					return 0;//&CRI;	// cannot simplify, range resolves to +-infinity
				}
			}
		} else {
			if (c.isStrictlyPositive()) {
				*newLowr = a.sdiv(c);
				if (b.isNegative()) {
					*newUpr = b.sdiv(d);
				} else {
					*newUpr = b.sdiv(c);
				}
			} else if (b.isNegative()) {
				if (d.isNegative()) {
					*newLowr = b.sdiv(c);
					*newUpr = a.sdiv(d);
				} else {
					return 0;//&CRI;	// cannot simplify, range resolves to +-infinity
				}
			} else if (d.isNegative()) {
				*newLowr = b.sdiv(d);
				*newUpr = a.sdiv(d);
			} else {
				// compare a/c, b/d etc
				return 0; //&CRI;	// cannot simplify, range resolves to +-infinity
			}
		}
			
		break;
	case Instruction::Select:	// 50
		printf("SELECT(%d)\n", OpCode);
			if ((a.slt(0) && b.sge(0))||(a.sle(0) && b.sgt(0)))  { // Here for [a,b) including 0 and others
				*newLowr = (c.slt(e)?c:e);
				*newUpr = (d.sgt(f))?d:f;
			} else if (a.sgt(0) || b.slt(0)) { // Here for [a,b) NOT including 0
				*newLowr = c;
				*newUpr = d;
			} else {
				newLowr->operator=(e);
				newUpr->operator=(f);				
			}
	
		break;
	}
	
	printf("[%d, %d), [%d, %d), [%d, %d)\n", l0, u0, l1, u1, l2, u2);
	cout << "MyLowr value: " << MyLowr.toString(10, true) << "\n";	
	cout << "MyUpr value: " << MyUpr.toString(10, true) << "\n";

	return 0;//&CRI;
}

/* I tried to make this a helper function it but did not build, so I put it in a macro instead
int APIproduct(APInt *dest, APInt *m1, APInt *m2) {
	// Make space for multiplication
	dest->sext(m1->getBitWidth()+m2->getBitWidth());
	dest->operator=(1);		// make it one
	dest->operator*=(m1);	// multiply by m1
	dest->operator*=(m2);	// multipy by m2
	return 1;
}*/