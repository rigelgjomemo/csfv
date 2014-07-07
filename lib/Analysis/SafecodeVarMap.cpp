//===-------- Acsl Variable Mapping Pass ------------------------*- C++ -*-===//
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

#define DEBUG_TYPE "safecodevarmap"


#include "llvm/Analysis/SafecodeVarMap.h"
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

SafecodeVarMap::SafecodeVarMap() : FunctionPass(ID){}
SafecodeVarMap::~SafecodeVarMap(){}

//returns the position of op1 in the list of arguments of F
//returns -1 if op1 is not an argument

void SafecodeVarMap::assignFreshNamesToUnamedInstructions(Function & F){
	int nameCount=0;
	for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
		if((!I->hasName())&&(!(I->getType()->isVoidTy()))) {
			std::stringstream ss;
			ss << "var"<<nameCount;
			std::string s = ss.str();
			I->setName(s);
			nameCount++;
		}
	}
}

void SafecodeVarMap::collectVariableMappingInformationsRigel(Function &F, StringMap< std::vector<VarMappingInfo> > &mappingMap, StringMap< std::string > &reverseNameSCMap, 
																StringMap< Value * > &reverseContextMap,StringMap< int > &reverseCodeLineMap,
																StringMap< std::vector<std::string> > &constantsMap){
	//collect information about global variables.
	//!llvm.dbg.cu = !{!0}
	//!0 = metadata !{i32 786449, metadata !1, i32 12, metadata !"clang version 3.5.0 (trunk 210885)", i1 false, metadata !"", i32 0, metadata !2, metadata !2, metadata !3, metadata !8, metadata !2, metadata !"", i32 1} ; [ DW_TAG_compile_unit ] [/home/rigel/Research/Lenore/prog.c] [DW_LANG_C99]
	//!8 = metadata !{metadata !9, metadata !11, metadata !12, metadata !13, metadata !14, metadata !15}
	//!9 = metadata !{i32 786484, i32 0, null, metadata !"globalI", metadata !"globalI", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalI, null} ; [ DW_TAG_variable ] [globalI] [line 1] [def]
	//!11 = metadata !{i32 786484, i32 0, null, metadata !"globalJ", metadata !"globalJ", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalJ, null} ; [ DW_TAG_variable ] [globalJ] [line 1] [def]
	//!12 = metadata !{i32 786484, i32 0, null, metadata !"globalK", metadata !"globalK", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalK, null} ; [ DW_TAG_variable ] [globalK] [line 1] [def]
	//!13 = metadata !{i32 786484, i32 0, null, metadata !"globalH", metadata !"globalH", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalH, null} ; [ DW_TAG_variable ] [globalH] [line 1] [def]
	//!14 = metadata !{i32 786484, i32 0, null, metadata !"globalM", metadata !"globalM", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalM, null} ; [ DW_TAG_variable ] [globalM] [line 1] [def]
	//!15 = metadata !{i32 786484, i32 0, null, metadata !"globalB", metadata !"globalB", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalB, null} ; [ DW_TAG_variable ] [globalB] [line 1] [def]
	NamedMDNode* dbgCU = F.getParent()->getNamedMetadata("llvm.dbg.cu");
	//errs() << *dbgCU << "\n";
	if(dbgCU) {
		MDNode* node0 = dyn_cast<MDNode>(dbgCU->getOperand(0));
		errs() << * node0 << "\n";
		MDNode* globalVarsList = dyn_cast<MDNode>(node0->getOperand(10)); //the list of global variable metadata is referenced by field 11.
		errs() << *globalVarsList << "\n";
		for(int i = 0; i < globalVarsList->getNumOperands() ; ++i ) {
			MDNode* globalVar = dyn_cast<MDNode>(globalVarsList->getOperand(i));
			DIGlobalVariable* DIG =  new DIGlobalVariable(globalVar);
			StringRef nameIR;
			StringRef nameSC;
			if(DIG) {
				nameIR=DIG->getName();
				errs()<< nameIR<<"\n";
				StringRef nameSC = DIG->getDisplayName();
				DIScope annContext = DIG->getContext();
				unsigned Line = DIG->getLineNumber();
				errs() << Line <<"\n";
				VarMappingInfo newInfo = VarMappingInfo(annContext, nameIR, Line);
				
				mappingMap[nameSC].push_back(newInfo);
			}
			errs() << *globalVar<<"\n";
		}
	}
	for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
		//DEBUG(errs() << *I <<"\n");
		//call void @llvm.dbg.declare(metadata !{i32* %j}, metadata !11), !dbg !13
		//!4 = metadata !{i32 786478, metadata !1, metadata !5, metadata !"main", metadata !"main", metadata !"", i32 1, metadata !6, i1 false, i1 true, i32 0, i32 0, null, i32 0, i1 false, void ()* @main, null, null, metadata !2, i32 1} ; [ DW_TAG_subprogram ] [line 1] [def] [main]
		//!11 = metadata !{i32 786688, metadata !4, metadata !"j", metadata !5, i32 2, metadata !12, i32 0, i32 0} ; [ DW_TAG_auto_variable ] [j] [line 2]
		//!13 = metadata !{i32 2, i32 0, metadata !4, null}
		
		//
		if(DbgDeclareInst* DbgDecl = dyn_cast<DbgDeclareInst>(&*I)) {
			//DEBUG(errs() << *DbgDecl << "\n";);
			MDNode* var = DbgDecl->getVariable();
			//errs() << *var << "\n";
			//StringRef name = var->getName();
			//errs() << name << "\n";
			DIVariable* localVar = new DIVariable(var);
			StringRef nameSC= localVar->getName();// -> nameSC is the source code name
			DEBUG(errs() << "nameSC: "<<nameSC <<"\n";);
			unsigned codeLine = localVar->getLineNumber();
			/*if (MDNode *N = DbgDecl->getMetadata("dbg")) {
				DILocation Loc(N);
				unsigned Line = Loc.getLineNumber();
				codeLine = Line;
				//if(codeLine >= 4122)
				//	errs()<<"";
			}*/
			DEBUG(errs() << "codeLine: "<< codeLine <<"\n";);
			
			MDNode * MD1 = cast<MDNode>(DbgDecl->getOperand(0)); // metadata !{i32* %j}
			DEBUG(errs() << *MD1 << "\n";);
			Value* v = MD1->getOperand(0);		//
			if(!v) //this may be null sometimes
				continue;
			StringRef nameIR = MD1->getOperand(0)->getName(); //j
			
			//if it is an annotation
			if(ConcreteOperator<Operator,Instruction::GetElementPtr> *CO=
			   dyn_cast<ConcreteOperator<Operator,Instruction::GetElementPtr> >(MD1->getOperand(0))) {
				if(CO->getNumOperands()>0){
					if(GlobalVariable *GV = dyn_cast<GlobalVariable>(CO->getOperand(0))){
						if(GV->hasInitializer()){
							if (ConstantDataArray *CDA =
								dyn_cast<ConstantDataArray>(GV->getInitializer())){
								if(CDA->isString()){
									//do nothing, it is just an annotation and it is too early to parse it
								}
							}
						}
						
					}
				}
				
			}
			//if it is a value information
			else {
				//MD2 -> !11 = metadata !{i32 786688, metadata !4, metadata !"j", metadata !5, i32 2, metadata !12, i32 0, i32 0} ; [ DW_TAG_auto_variable ] [j] [line 2]
				MDNode * MD2 = cast<MDNode>(DbgDecl->getOperand(1)); //
				//context -> !4 = metadata !{i32 786478, metadata !1, metadata !5, metadata !"main", metadata !"main", metadata !"", i32 1, metadata !6, i1 false, i1 true, i32 0, i32 0, null, i32 0, i1 false, void ()* @main, null, null, metadata !2, i32 1} ; [ DW_TAG_subprogram ] [line 1] [def] [main]
				Value * context = MD2->getOperand(1);
							
				VarMappingInfo newInfo = VarMappingInfo(context, nameIR, codeLine);
				
				mappingMap[nameSC].push_back(newInfo);
				
				reverseNameSCMap[nameIR] = nameSC;
				reverseContextMap[nameIR] = context;
				reverseCodeLineMap[nameIR] = codeLine;
				//errs()<< "nameSC: "<<nameSC<<"\n";
				//errs() << "   -> context: "<<*context <<"\n";
				//errs() << "      nameIR:  "<< nameIR << "\n";
				//errs() << "      Codeline:  "<< codeLine << "\n";
				
				
				
			//}
	//	}
			
		}
	}
	}
}

/*
void SafecodeVarMap::collectVariableMappingInformationsRigelNew(Function &F, StringMap< std::vector<VarMappingInfo> > &mappingMap, StringMap< std::string > &reverseNameSCMap, 
																StringMap< DIScope * > &reverseContextMap,StringMap< int > &reverseCodeLineMap,
																StringMap< std::vector<std::string> > &constantsMap){
	//collect information about global variables.
	//!llvm.dbg.cu = !{!0}
	//!0 = metadata !{i32 786449, metadata !1, i32 12, metadata !"clang version 3.5.0 (trunk 210885)", i1 false, metadata !"", i32 0, metadata !2, metadata !2, metadata !3, metadata !8, metadata !2, metadata !"", i32 1} ; [ DW_TAG_compile_unit ] [/home/rigel/Research/Lenore/prog.c] [DW_LANG_C99]
	//!8 = metadata !{metadata !9, metadata !11, metadata !12, metadata !13, metadata !14, metadata !15}
	//!9 = metadata !{i32 786484, i32 0, null, metadata !"globalI", metadata !"globalI", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalI, null} ; [ DW_TAG_variable ] [globalI] [line 1] [def]
	//!11 = metadata !{i32 786484, i32 0, null, metadata !"globalJ", metadata !"globalJ", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalJ, null} ; [ DW_TAG_variable ] [globalJ] [line 1] [def]
	//!12 = metadata !{i32 786484, i32 0, null, metadata !"globalK", metadata !"globalK", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalK, null} ; [ DW_TAG_variable ] [globalK] [line 1] [def]
	//!13 = metadata !{i32 786484, i32 0, null, metadata !"globalH", metadata !"globalH", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalH, null} ; [ DW_TAG_variable ] [globalH] [line 1] [def]
	//!14 = metadata !{i32 786484, i32 0, null, metadata !"globalM", metadata !"globalM", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalM, null} ; [ DW_TAG_variable ] [globalM] [line 1] [def]
	//!15 = metadata !{i32 786484, i32 0, null, metadata !"globalB", metadata !"globalB", metadata !"", metadata !5, i32 1, metadata !10, i32 0, i32 1, i32* @globalB, null} ; [ DW_TAG_variable ] [globalB] [line 1] [def]
	NamedMDNode* dbgCU = F.getParent()->getNamedMetadata("llvm.dbg.cu");
	//errs() << *dbgCU << "\n";
	if(dbgCU) {
		MDNode* node0 = dyn_cast<MDNode>(dbgCU->getOperand(0));
		errs() << * node0 << "\n";
		MDNode* globalVarsList = dyn_cast<MDNode>(node0->getOperand(10)); //the list of global variable metadata is referenced by field 11.
		errs() << *globalVarsList << "\n";
		for(int i = 0; i < globalVarsList->getNumOperands() ; ++i ) {
			MDNode* globalVar = dyn_cast<MDNode>(globalVarsList->getOperand(i));
			DIGlobalVariable* DIG =  new DIGlobalVariable(globalVar);
			StringRef nameIR;
			StringRef nameSC;
			if(DIG) {
				nameIR=DIG->getName();
				errs()<< nameIR<<"\n";
				StringRef nameSC = DIG->getDisplayName();
				Value* annContext = globalVar->getOperand(2);
				unsigned Line = DIG->getLineNumber();
				errs() << Line <<"\n";
				VarMappingInfo newInfo = VarMappingInfo(annContext, nameIR, Line);
				
				mappingMap[nameSC].push_back(newInfo);
			}
			errs() << *globalVar<<"\n";
		}
	}
	for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
		//DEBUG(errs() << *I <<"\n");
		//call void @llvm.dbg.declare(metadata !{i32* %j}, metadata !11), !dbg !13
		//!4 = metadata !{i32 786478, metadata !1, metadata !5, metadata !"main", metadata !"main", metadata !"", i32 1, metadata !6, i1 false, i1 true, i32 0, i32 0, null, i32 0, i1 false, void ()* @main, null, null, metadata !2, i32 1} ; [ DW_TAG_subprogram ] [line 1] [def] [main]
		//!11 = metadata !{i32 786688, metadata !4, metadata !"j", metadata !5, i32 2, metadata !12, i32 0, i32 0} ; [ DW_TAG_auto_variable ] [j] [line 2]
		//!13 = metadata !{i32 2, i32 0, metadata !4, null}
		
		//
		if(DbgDeclareInst* DbgDecl = dyn_cast<DbgDeclareInst>(&*I)) {
			//DEBUG(errs() << *DbgDecl << "\n";);
			MDNode* var = DbgDecl->getVariable();
			//errs() << *var << "\n";
			//StringRef name = var->getName();
			//errs() << name << "\n";
			DIVariable* localVar = new DIVariable(var);
			StringRef nameSC= localVar->getName();// -> nameSC is the source code name
			DEBUG(errs() << "nameSC: "<<nameSC <<"\n";);
			unsigned codeLine = localVar->getLineNumber();
			//if (MDNode *N = DbgDecl->getMetadata("dbg")) {
				//DILocation Loc(N);
				//unsigned Line = Loc.getLineNumber();
				//codeLine = Line;
				//if(codeLine >= 4122)
				//	errs()<<"";
			//}
			DEBUG(errs() << "codeLine: "<< codeLine <<"\n";);
			
			MDNode * MD1 = cast<MDNode>(DbgDecl->getOperand(0)); // metadata !{i32* %j}
			DEBUG(errs() << *MD1 << "\n";);
			Value* v = MD1->getOperand(0);		//
			if(!v) //this may be null sometimes
				continue;
			StringRef nameIR = MD1->getOperand(0)->getName(); //j
			
			//if it is an annotation
			if(ConcreteOperator<Operator,Instruction::GetElementPtr> *CO=
			   dyn_cast<ConcreteOperator<Operator,Instruction::GetElementPtr> >(MD1->getOperand(0))) {
				if(CO->getNumOperands()>0){
					if(GlobalVariable *GV = dyn_cast<GlobalVariable>(CO->getOperand(0))){
						if(GV->hasInitializer()){
							if (ConstantDataArray *CDA =
								dyn_cast<ConstantDataArray>(GV->getInitializer())){
								if(CDA->isString()){
									//do nothing, it is just an annotation and it is too early to parse it
								}
							}
						}
						
					}
				}
				
			}
			//if it is a value information
			else {
				//MD2 -> !11 = metadata !{i32 786688, metadata !4, metadata !"j", metadata !5, i32 2, metadata !12, i32 0, i32 0} ; [ DW_TAG_auto_variable ] [j] [line 2]
				MDNode * MD2 = cast<MDNode>(DbgDecl->getOperand(1)); //
				//context -> !4 = metadata !{i32 786478, metadata !1, metadata !5, metadata !"main", metadata !"main", metadata !"", i32 1, metadata !6, i1 false, i1 true, i32 0, i32 0, null, i32 0, i1 false, void ()* @main, null, null, metadata !2, i32 1} ; [ DW_TAG_subprogram ] [line 1] [def] [main]
				DIScope * context = localVar->getContext();
							
				VarMappingInfo newInfo = VarMappingInfo(context, nameIR, codeLine);
				
				mappingMap[nameSC].push_back(newInfo);
				
				reverseNameSCMap[nameIR] = nameSC;
				reverseContextMap[nameIR] = context;
				reverseCodeLineMap[nameIR] = codeLine;
				//errs()<< "nameSC: "<<nameSC<<"\n";
				//errs() << "   -> context: "<<*context <<"\n";
				//errs() << "      nameIR:  "<< nameIR << "\n";
				//errs() << "      Codeline:  "<< codeLine << "\n";
				
				
				
			//}
	//	}
			
		}
	}
	}
}

*/

StringRef SafecodeVarMap::getAnnotationFromStore(StoreInst* storeAnnot) {
	DEBUG(errs()<< "   "<<*storeAnnot<<"\n");
	Value* gepAnnot = storeAnnot->getOperand(0);
	if(ConstantExpr* cexpr = dyn_cast<ConstantExpr>(gepAnnot)) {
		DEBUG(errs() << *cexpr<<"\n");
		Instruction* inst = cexpr->getAsInstruction(); 
		//DEBUG(errs()<< *bb <<"\n");
		DEBUG(errs() << *inst<<"\n");
		if(GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(inst)) {
			Value* gepFirst = gepInst->getPointerOperand();
			DEBUG(errs() << *gepFirst<<"\n");
			if(GlobalVariable *GI = dyn_cast<GlobalVariable>(gepFirst)){
				DEBUG(errs()<<*GI<<"\n");
				if(GI->hasInitializer()){
					if (ConstantDataArray *GV = dyn_cast<ConstantDataArray>(GI->getInitializer())){
						if(GV->isString()){
							//drop front to remove the @ and the last (strange symbol)
							StringRef annotation = GV->getAsString().drop_back(1).drop_front(1);
							//one more check
							if(annotation.startswith("assert")) {
								DEBUG(errs()<<annotation<<"\n");
								delete inst;
								return annotation;
							}
						}
					}
				}
			}
		}
		delete inst;
	}
	
	return "";
}

bool SafecodeVarMap::runOnFunction(Function &F){
	std::string fName = F.getName().str();
	errs() << "SafecodeVarMap::runOnFunction: " << fName << "\n";
	
	//keep track of the analyzed functions
	//visitedFunctions.insert(fName);
	// loop (1) FILL A DATA STRUCTURE WITH THE INFORMATIONS GATHERED BY THE DUBUG INFO
	//VarMappingInfo is a data structure that holds several pieces of info about
	//a variable. That is: 1) context (a.k.a scope in the source file of the original 
	// source variable to which this IR variable refers,
	//each scope is an entity in llvm), 2) basic block in the IR where the IR variable 
	//is, 3) nameIR, the name of this variable (i.e. register) in the IR
	//4) line in the source code, this IR var refers to.
	StringMap< std::vector<VarMappingInfo> > mappingMap;
	//reverseNameSCMap: <irName, sourceCodeName>. This is one of the results of the
	//computations.
	StringMap< std::string > reverseNameSCMap;
	//reverseContextMap: <irName, context>. Given an IR name tells the context in the source code
	StringMap< Value * > reverseContextMap;
	//same as above for line number (codeline)
	StringMap< int > reverseCodeLineMap;
	//this is for understanding which variable a phi-node refers to when the
	//values of the alternatives inside the phinode are all constants (we do not have
	//debug info for phi nodes). So we go over the constant values and check
	//which variable they may belong to and store the results in this map:
	//<constant, nameIR>.
	StringMap< std::vector<std::string> > constantsMap;
	//SafecodeInfo is a: <varname, annotation>. For each variable in an
	//instruction, gives the acsl annotation.
	//Each Instruction may have more than one variables. However, it only contains instructions llvm.dbg.value
	std::map<Instruction *, std::map<std::string, ranges::Range> > safecodeMap;
	
	//map <IRVariableName, range>
	std::map<std::string, ranges::Range> irNameRangeMap;
	
	//the loop below goes over all instructions and renames
	//those that do not have a name, which appear in the IR
	//with %n, where n is a number. This is present in Rigel's
	//setup (safecode, LLVM 3.2) but not on Niko's setup (LLVM 3.3, no safecode)
	//This apparently messes up Niko's code on rigel's setup, and it misses
	//some assertions. The loop below to try fix this.
	assignFreshNamesToUnamedInstructions(F);
	
	//This loop populates the VarMappingInfo structure
	collectVariableMappingInformationsRigel(F, mappingMap, reverseNameSCMap, reverseContextMap,reverseCodeLineMap,constantsMap);
	
	inst_iterator it = inst_begin(F);
	while(!(&*it)->getMetadata("dbg")) {
		++it;
	}
	MDNode* meta = (&*it)->getMetadata("dbg");
	MDNode* annotCont = dyn_cast<MDNode>(meta->getOperand(2));
	if(annotCont) {
		this->topContext = annotCont;
		//errs()<<*annotCont;
	}
// loop (4) over the annotations and change them accordingly to the right mapping
	
	for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
		DEBUG(errs()<< *I << "\n";);
		if(DbgDeclareInst * DbgDecl = dyn_cast<DbgDeclareInst>(&*I)) {
			StringRef dbgName = DbgDecl->getName();
		//	errs()<<dbgName<<"\n";
			BasicBlock::iterator NextInst((&*I));
			NextInst++;
			DEBUG(errs() << *NextInst<<"\n");
			//if nextinst is a store with a GEP as first operand and previous MDNode as second operand, this is an annotation
			//get annotation, and parse it.
			if(StoreInst* storeAnnot = dyn_cast<StoreInst>(NextInst)) {
				StringRef annotation=getAnnotationFromStore(storeAnnot);
				if(annotation.equals(""))
					continue;
				//get the line of code and the context (scope of the annotation)
				int codeLine = 0;
				//MDNode * annotationContext;
				MDNode *N = DbgDecl->getMetadata("dbg");
				if (N) {
					DILocation Loc(N);
					unsigned Line = Loc.getLineNumber();
					codeLine = Line;
					/*if(codeLine >= 2375) {
						errs()<< " ";
						printMappingMap(&mappingMap);
					}*/
					//annotationContext = cast<MDNode>(N->getOperand(2));
				}
				
				//errs()<<annotation<<" -> "<< *annotationContext <<" @ "<<codeLine<<"\n";
				
				//Parse the string
				ACSLStatements root;
				example::Driver driver(root);
				//errs()<<"Parsing: "<<annotation<<"\n";
				bool result = driver.parse_string(annotation, "input");
				if (result){
					std::string lastNameIR = "";
					//For every variable search in the data structure the correct mapping
					std::set<std::string> varList = driver.root.getTreeVariables();
					for (std::set<std::string>::iterator j = varList.begin(); j != varList.end(); ++j){
						BasicBlock * basicBlock = DbgDecl->getParent();
						std::string nameSC = *j;
						//errs()<<annotation<<"\n";
						std::string correctNameIR = findNameIRRigel(&mappingMap,N,basicBlock, nameSC, codeLine);
						//if(correctNameIR=="") //probably a global variable
							//this works if we assume that the global variable in IR has the same name as in source code.
							//llvm guidelines say not to rely on the name, however there is no debug information that can link
							//global variables in the IR with global variables in the source code.
							//GlobalVariable global = F->getParent()->getGlobalVariable(nameSC); 
																								
						//errs()<<correctNameIR<<"\n";
						//Substitute the variable name with the correct one
						driver.root.changeTreeVariableName(correctNameIR, nameSC);

						//save the var about the annotation
						lastNameIR=correctNameIR;
					}
					DEBUG(
						  errs()<<driver.root.getTreePrintString()<<"\n";
					);
					//Store the annotations in safecode map
					for (std::vector<ACSLStatement*>::iterator istmt = driver.root.statements.begin(); istmt != driver.root.statements.end(); ++istmt)
					{
						ACSLStatements root2;
						example::Driver driver2(root2);
						if(ACSLAssertStatement * stmt = dyn_cast<ACSLAssertStatement>((*istmt))){
																				   
							auto sp = getRangePair(&(stmt->exp));

							DEBUG(
								//dbgs()<<"Inserted info @"<<(&*I)->getName()<<": "<< sp.first <<" -> "<<sp.second<<" in safecodeMap\n";
							);
							//errs()<<*I<<"\n";
							safecodeMap[&*I][sp.first]=sp.second;
						}
						
					}
				}
			}
		}
	}
//rigel
//the name mapping between source and IR variables is in reverseName
//safecoMap has mapping between llvm.dbg.value and ranges
//
//for every instruction
//   get operands. And for every operand
//		check if it has an associated acsl_range metadata (if Phu puts annotations everywhere, this should happen often)
//		if yes, do nothing
//		if not, we must find the range, we need a map<Instruction*, Metadata>
//		when we are inserting into safecodeMap
//end rigel

	
	//loop 5 rigel:
	//Iterate over the DbgDeclInst in safecodeMap
	//for each of them containing an annotation: (this annotation will be valid as long as:
	//											1) we do not see another DbgDeclInst with annotation about the same variable)
	//											2) we do not see a store to an alloca related to this DbgDeclInst)
	//											3) related to means that the alloca's name is mapped to the source variable in the annotation
	//	 get the name of the source variable from the annotation: nameSC
	//   start a loop over the instructions that goes from the DbgDeclInst to the end of the function  
	//	 	if instruction is a load
	//			get its name (this is the alloca's name)
	//			get corresponding source code name from reverseMapNames: nameSC1
	//			if nameSC1 == nameSC, this means that the annotation is about the same variable.
	//				attach metadata to the load.
	//				to be able to break when store is encountered, add name of loaded alloca to map DbgDeclInst -> <irName1, irName2, irName3,...>
	//				this map represents the allocas for which this annotation is alive.
	//      if instruction is a store
	//			get the alloca that is the target of the store
	//			get the name of that alloca
	//			get the corresponding source code name from reverseNameMap: nameSC1
	//			if nameSC1 = nameSC, we found a store that is potentially to an alloca for which this annotation is alive
	//				look up the map DbgDeclInst -> <irName1, irName2,...>, if 
	
	//printSafecodeMap(safecodeMap);
	for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
		DEBUG(errs() << "I: "<<*I<<"\n";);
		//if it has an annotation get the varname
		if(safecodeMap.count(&*I)){
			std::string variable = safecodeMap[&*I].begin()->first;
			DEBUG(errs()<<"variable: "<<variable<<"\n";);
			//safecodeMap will have the pair with the ranges
			//iterate over all the following instructions in the same BB
			if (MDNode *N = I->getMetadata("dbg")) {
					DILocation Loc(N);
					unsigned codeLine = 0;
					unsigned Line = Loc.getLineNumber();
					codeLine = Line;
					if(codeLine >= 75)
						errs()<< " ";
			}
			BasicBlock::iterator NextInst((&*I));
			NextInst++;
			DEBUG(errs() << *NextInst;);
			BasicBlock::iterator end = I->getParent()->end();
			//while (Instruction * NextInst = it->getNextNode()) {
			while (NextInst != end) {
				DEBUG(errs()<<"     NextInst: "<<*NextInst<<"\n";);
				//if there is an annotation about the same variable stop???
				if (safecodeMap.count(NextInst)) { 
					//get the variable name of this annotation
					std::string newVar = safecodeMap[&*NextInst].begin()->first;
					DEBUG(errs()<<"     "<<newVar<<"\n";);
					if(newVar==variable) //if they have the same name break
						break;
				}                        
				//if there is a store to the alloca we should assume that the value will change
				//so the annotation is not valid anymore, therefore we should stop propagating the annotation.
				if(StoreInst* store = dyn_cast<StoreInst>(NextInst)) { 
					StringRef irName = store->getOperand(1)->getName();			
					
					std::string newVar = reverseNameSCMap[irName];
					if(store->getOperand(0)->getName()=="add32") {
						errs() << "StoreInst, store into: " << irName <<"\n";
						errs() << "variable is: " << variable<<"\n";
						errs() <<"newVar is: "<< newVar<<"\n";
					}
				
					if(newVar==variable) 
						break; 
				}
				//at this point I have only mapping between allocas and annotations.
				//Add the metadata only to the load instructions? 
				//if it is an algebric operation keep track
				/* if(BinaryOperator * BI = dyn_cast<BinaryOperator>(NextInst)){
					//rigel
					//check if by any chance the annotation is there already for NextInst.
					
				
					//}
					//end rigel
					
					bool rangeFound = true;
					Instruction::BinaryOps opcode = BI->getOpcode();
					ranges::Range range1;
					if(ConstantInt * CI = dyn_cast<ConstantInt>(BI->getOperand(0))){
						int num = CI->getSExtValue();
						range1 = ranges::Range(num,num);
					} else if (safecodeMap[(&*I)].count(BI->getOperand(0)->getName())){
						range1 = safecodeMap[(&*I)][BI->getOperand(0)->getName()];
					} else {
						rangeFound = false;
					}
					ranges::Range range2;
					if(ConstantInt * CI = dyn_cast<ConstantInt>(BI->getOperand(1))){
						int num = CI->getSExtValue();
						range2 = ranges::Range(num,num);
					} else if (safecodeMap[(&*I)].count(BI->getOperand(1)->getName())){
						range2 = safecodeMap[(&*I)][BI->getOperand(1)->getName()];
					} else {
						rangeFound = false;
					}
					if(rangeFound){
						ranges::Range result;
						bool supportedInst = true;
						switch (opcode) {
							//if it is a add compute range and update the map with %add_name -> range*
							case (Instruction::Add):
								result = range1 + range2;
								break;                     
								//if it is a sub compute range and update the map with %sub_name -> range*
							case (Instruction::Sub):
								result = range1 - range2;
								break;
								//if it is a mul compute range and update the map with %mul_name -> range*
							case (Instruction::Mul):
								result = range1 * range2;
								break;
							default:
								supportedInst = false;
						}
						//errs()<<"adding "<<BI->getName()<<"->"<<result<<"\n";
						if (supportedInst){
							safecodeMap[(&*I)][BI->getName()]=result; 
						}
					}
				}
				//if the variable is reassigned i64->i32 keep track
				else if(SExtInst * SI = dyn_cast<SExtInst>(NextInst)){
					std::string sextName = SI->getOperand(0)->getName();
					if(safecodeMap[(&*I)].count(sextName)){
						safecodeMap[(&*I)][SI->getName()]=safecodeMap[(&*I)][sextName];
					}
				}
				//if the variable is reassigned i64->i32 keep track
				else if(ZExtInst * ZI = dyn_cast<ZExtInst>(NextInst)){
					std::string zextName = ZI->getOperand(0)->getName();
					if(safecodeMap[(&*I)].count(zextName)){
						safecodeMap[(&*I)][ZI->getName()]=safecodeMap[(&*I)][zextName];
					}
				}
				//if it is a malloc if we are abele to compute size, attach acsl_malloc metadata
				else if(CallInst * call = dyn_cast<CallInst>(NextInst)){
					if(Function* callee = call->getCalledFunction()){
						StringRef cName = callee->getName();
						if(cName.compare("malloc")==0) {
						//val: i64 %var2
						Value* val = call->getArgOperand(0);
							std::string mallocOpName = val->getName();
							if(safecodeMap[(&*I)].count(mallocOpName)){
								ranges::Range r = safecodeMap[(&*I)][mallocOpName];
								std::stringstream strstream;
								strstream << r.getBottom();
								std::string mallocSizeAnnotation = strstream.str();
								LLVMContext& C = NextInst->getContext();
								MDNode* V = MDNode::get(C, MDString::get(C, mallocSizeAnnotation));
								call->setMetadata("acsl_malloc_var_size", V);
							}
						}
					}
				}
				//if the GEP is using the var attach the metadata "acsl_safecode"
				else if(GetElementPtrInst * GEP = dyn_cast<GetElementPtrInst>(NextInst)){
					//attach acsl_safecode metadata and keep track
					if(GEP->getNumOperands()>2){
						//errs()<<*GEP<<"\n";
						std::string gepName = GEP->getOperand(2)->getName();
						DEBUG(
							errs() << "gepName: "<<gepName <<"\n";
						);
						if(safecodeMap[(&*I)].count(gepName)){
							ranges::Range r = safecodeMap[(&*I)][gepName];
							LLVMContext& C = NextInst->getContext();
							MDNode* V = MDNode::get(C, MDString::get(C, r.getAnnotation(gepName)));
							GEP->setMetadata("acsl_range", V);
							safecodeMap[(&*I)][GEP->getName()] = r;
						} 
					}
					if(GEP->getNumOperands()==2){
						std::string gepStartName = GEP->getOperand(0)->getName();
						std::string gepOffsetName = GEP->getOperand(1)->getName();
						DEBUG(errs()<<"gepStartName: "<<gepStartName <<"\n";);
						DEBUG(errs()<<"gepOffsetName: "<<gepOffsetName <<"\n";);
						if(safecodeMap[(&*I)].count(gepOffsetName)){
							ranges::Range result = safecodeMap[(&*I)][gepOffsetName];
							LLVMContext& C = NextInst->getContext();
							//if there are info about the start name compute the range from there
							if(safecodeMap[(&*I)].count(gepStartName)){
								result = result + safecodeMap[(&*I)][gepStartName];
							}
							MDNode* V = MDNode::get(C, MDString::get(C, result.getAnnotation(gepOffsetName)));
							GEP->setMetadata("acsl_range", V);
							safecodeMap[(&*I)][GEP->getName()] = result;
						} 
					}
						
					attachMallocMetadata(GEP);
					
				}
				//if the STORE is using the var attach the metadata "acsl_safecode"
				else if(StoreInst * STI = dyn_cast<StoreInst>(NextInst)){
					std::string gepName = STI->getOperand(1)->getName();
					DEBUG(errs()<<"gepName: "<<gepName <<"\n";);
					if(safecodeMap[(&*I)].count(gepName)){
						ranges::Range r = safecodeMap[(&*I)][gepName];
						LLVMContext& C = NextInst->getContext();
						MDNode* V = MDNode::get(C, MDString::get(C, r.getAnnotation(gepName)));
						STI->setMetadata("acsl_range", V);
					}
					
				}*/
				//if the LOAD is using the var attach the metadata "acsl_safecode"
				if(LoadInst * LDI = dyn_cast<LoadInst>(NextInst)){
					if(!LDI->getMetadata("acsl_range")) {
						std::string gepName = LDI->getOperand(0)->getName();
						DEBUG(errs()<<"gepName: "<<gepName <<"\n";);
						if(safecodeMap[(&*I)].count(gepName)){
							ranges::Range r = safecodeMap[(&*I)][gepName];
							LLVMContext& C = NextInst->getContext();
							MDNode* V = MDNode::get(C, MDString::get(C, r.getAnnotation(gepName)));
							LDI->setMetadata("acsl_range", V);
							//replicate the range about the variable in the load
							//TODO: do it only if it is a LOAD about global variable
							safecodeMap[(&*I)][LDI->getName().str()]=r;
						}
					}
				}
				//very quick hack for IOC.
				else if(BranchInst* br = dyn_cast<BranchInst>(NextInst)) { //we have arrived at the branch instruction, repeat the same thing for the basic block of its true target
					BasicBlock* trueBr = br->getSuccessor(0);
					for(BasicBlock::iterator iter = trueBr->begin(); iter != trueBr->end(); ++iter) {
						DEBUG(errs()<<"     iter: "<<*iter<<"\n";);
						//if there is an annotation about the same variable stop???
						if (safecodeMap.count(iter)) { 
							//get the variable name of this annotation
							std::string newVar = safecodeMap[&*iter].begin()->first;
							DEBUG(errs()<<"     "<<newVar<<"\n";);
							if(newVar==variable) //if they have the same name break
								break;
						}                        
						//if there is a store to the alloca we should assume that the value will change
						//so the annotation is not valid anymore, therefore we should stop propagating the annotation.
						if(StoreInst* store = dyn_cast<StoreInst>(iter)) { 
							StringRef irName = store->getOperand(1)->getName();			
							DEBUG(errs() << irName <<"\n";);
							std::string newVar = reverseNameSCMap[irName];
							if(newVar==variable) 
								break; 
						}
						//if the LOAD is using the var attach the metadata "acsl_safecode"
						if(LoadInst * LDI = dyn_cast<LoadInst>(iter)){
							if(!LDI->getMetadata("acsl_range")) { //t e are traversing a BB which is the target of a branch, 
								std::string gepName = LDI->getOperand(0)->getName(); //it may be possible that this is a backedge jump
								DEBUG(errs()<<"Load from: "<<gepName <<"\n";);		  //and we have an
								if(safecodeMap[(&*I)].count(gepName)){
									ranges::Range r = safecodeMap[(&*I)][gepName];
									LLVMContext& C = NextInst->getContext();
									MDNode* V = MDNode::get(C, MDString::get(C, r.getAnnotation(gepName)));
									LDI->setMetadata("acsl_range", V);
									//replicate the range about the variable in the load
									//TODO: do it only if it is a LOAD about global variable
									safecodeMap[(&*I)][LDI->getName().str()]=r;
								}
							
							}
						}
					}
				}
				//++it;
				++NextInst;
			}
		}
	}
	DEBUG(errs()<<"Done with SafecodeVarMap\n");
   return true;
}


//input: 
//findNameIR, gets in input mappingMap, the context where the annotation is in the source code
//the basic block in the IR where the annotation is and the line number, and the name of the variable
//in the source code, which we got by parsing the annotation.
/*
std::string SafecodeVarMap::findNameIR(StringMap< std::vector<VarMappingInfo> > * mappingMap, MDNode *  annotationContext,BasicBlock * basicBlock, std::string  nameSC, int codeLine){

	std::vector<VarMappingInfo> vec = (*mappingMap)[nameSC];
	if(vec.size()==1) { //usually this is the case, i.e. there is only one variable with a specific name in a function.
		VarMappingInfo vmi = vec[0];
		return vmi.nameIR;
	}
	errs()<<"Annotation Context: " <<*annotationContext<<"\n";
	errs()<<"In Basic Block: " <<basicBlock->getName()<<"\n";
	errs()<<"Source Code variable: " <<nameSC<<"\n";
    errs()<<"Codeline: " <<codeLine<<"\n";
	printMappingMap(mappingMap);
	
	//if we are here, there are several allocas mapped to the same source code variable. 
	//This happens if we declare a variable with the same name in a different scope:
	//int i = 0;
	//while(j>k) {
	//    int i = k+2;
	//}
	//we must find the correct alloca for the variable in the annotation.
	MDNode * context = annotationContext;

	//if(codeLine > 4350)
	//		errs()<<"hoho\n";
	std::string nameIR = nameSC;
	//if(nameSC=="max_no")
//		errs() << " \n";
	//lookup the data structure for the correct mapping
	bool hasSuperContext= true;
	// if there is more than one parent there should be a phi function...
	bool hasParent = true;
	bool foundName = false;
	
	std::map<BasicBlock*, bool> visitedBasicBlocks;
	std::stack<BasicBlock *> blockStack;
	blockStack.push(basicBlock); //this is the current basic block, where the annotation is
	//loop over the chain of context
	 //So, we start at the current scope of the annotation
	 //and look for all the possible mapping candidates starting from the current block
	 //A candidate is a variable whose value is being set (that is a definition) and whose
	 //corresponding source code name is the same as the name of the variable
	 //in the annotation. If we find one in the current block we are done.
	 //Otherwise we look at all the predecessor blocks having the same scope. If we do not find
	 // a definition there, we start with the parent scope (also called context below) and repeat the same thing.
	 //Until we reach the global scope.
	while (hasSuperContext && hasParent && !foundName) { //hasSuperContext refers to the parent scope
		if (blockStack.empty()) {							//hasParent refers to the block.
			//	errs()<<"block stack is empty\n";
			if(context && (context->getNumOperands()>1)){ //context is the same as scope.
				
				if (MDNode * MDP = dyn_cast<MDNode>(context->getOperand(1)) ){
					
				 //   errs()<<"Searching in top context ->"<<MDP<<"\n";
					
					context = MDP;
					blockStack.push(basicBlock);
					visitedBasicBlocks.clear();
				 }
					else
					 hasSuperContext=false;
			}
			else{
				hasSuperContext=false;
				
			}
		}
		else {
			//take the top element on the stack of blocks to visit
			BasicBlock * currentBlock = blockStack.top();
			
			   //   errs()<<"\tSearching in predecessor block ->"<< currentBlock <<"\n";
			
			blockStack.pop();
			visitedBasicBlocks[currentBlock] = true;
			
			//find all the mappings in the same context and basicBlock
			std::vector<VarMappingInfo> sameContextAndBasicBlockVector;
			for (std::vector<VarMappingInfo>::iterator z = (*mappingMap)[nameSC].begin(); z!=(*mappingMap)[nameSC].end(); ++z) {
				//check if there is a variable map in the same context
				VarMappingInfo info = (*z);
				errs()<<"checking IR variable: "<<info.nameIR<<"\n";
				errs()<<"   with context: " << *(info.context)<<"\n";
				errs()<<"   declared in BB: " << info.basicBlock->getName()<<"\n";
				errs()<<"   at line: "<<info.codeLine<<"\n";
				
				errs()<<*context<<"\n";
				errs()<<currentBlock->getName()<<"\n";
				errs()<<codeLine<<"\n";
				//errs() <<info.codeLine;
				//if the context of the annotation (context) is equal to the context of the variable declaration (info.context)
				//and the basic block of the annotation is equal to the basic block of the variable declaration
				//and the line of the variable declaration is less than the line of the annotation
				//i.e., the variable declaration comes before the annotation, push back into vector
				//do this for all the allocas that correspond to the same source variable. If none have the same basic block,
				//context of the annotation and smaller line, add the predecessors of the annotation's BB to the block stack 
				//and restart looking.
				//However, context is never updated (?).
				//
				//
				if(context == info.context && currentBlock == info.basicBlock && info.codeLine<=codeLine){
					sameContextAndBasicBlockVector.push_back(info);
				}
			}
			if (sameContextAndBasicBlockVector.size()!=0) {
				//search for the nearest mapping in the vector
				VarMappingInfo closest = sameContextAndBasicBlockVector.front();
				for (std::vector<VarMappingInfo>::iterator i = sameContextAndBasicBlockVector.begin(); i!=sameContextAndBasicBlockVector.end(); ++i) {
					VarMappingInfo info = *i;
					if(info.codeLine<=codeLine){
						if(info.codeLine>closest.codeLine){
							closest=info;
						}
					}
				}
				nameIR = closest.nameIR;
				foundName=true;
				break;
				
				
			} else {
				//popolate the stack with all the predecessor that are not already visited
				for (pred_iterator PI = pred_begin(currentBlock), E = pred_end(currentBlock); PI != E; ++PI) {
					if(visitedBasicBlocks.count((*PI))==0){
						errs() << "searching for nameIR in predecessor: " << (*PI)->getName()<<"\n";
						blockStack.push((*PI));
					}
				}
				
			}
		}
		
	}
	return nameIR;
	
}

*/
std::string SafecodeVarMap::findNameIRRigel(StringMap< std::vector<VarMappingInfo> > * mappingMap, MDNode *  DbgNode,BasicBlock * basicBlock, std::string  nameSC, int codeLine){

	std::vector<VarMappingInfo> vec = (*mappingMap)[nameSC];
	if(vec.size()==1) { //usually this is the case, i.e. there is only one variable with a specific name in a function.
		VarMappingInfo vmi = vec[0];
		return vmi.nameIR;
	}
	
	//errs() << "Inside findNameIR: DbgNode->getNumOperands = " << DbgNode->getNumOperands() << "\n";
	//errs() << "Inside findNameIR: DbgNode = " << *DbgNode<<"\n";
	MDNode* annotationContext = dyn_cast<MDNode>(DbgNode->getOperand(2)); //!32
	
	//errs()<<"Annotation Context: " <<*annotationContext<<"\n";
	//errs()<<"In Basic Block: " <<basicBlock->getName()<<"\n";
	//errs()<<"Source Code variable: " <<nameSC<<"\n";
    //errs()<<"Codeline: " <<codeLine<<"\n";
	//printMappingMap(mappingMap);
	std::string nameIR="";
	//if we are here, there are several allocas mapped to the same source code variable. 
	//This happens if we declare a variable with the same name in a different scope:
	//int i = 0;
	//while(j>k) {
	//    int i = k+2;
	//}
	//we must find the correct alloca for the variable in the annotation.
	MDNode * currentContext = annotationContext;
	bool found = false;
	bool atTop = false;
	while(!found && !atTop) {
		errs() << "Inside findNameIR: currentContext = " << *currentContext<<"\n";
		if(currentContext==this->topContext) //search once more in top context and then stop
			atTop=true;
		//find all the mappings in the same context and basicBlock
		std::vector<VarMappingInfo> sameContextAndBasicBlockVector;
		for (std::vector<VarMappingInfo>::iterator z = (*mappingMap)[nameSC].begin(); z!=(*mappingMap)[nameSC].end(); ++z) {
			//check if there is a variable map in the same context
			VarMappingInfo info = (*z);
			//errs()<<"checking IR variable: "<<info.nameIR<<"\n";
		//	errs()<<"   with context: " << *(info.context)<<"\n";
			//errs()<<"   declared in BB: " << info.basicBlock->getName()<<"\n";
			//errs()<<"   at line: "<<info.codeLine<<"\n";
			
			//errs()<<*currentContext<<"\n";
			//errs()<<codeLine<<"\n";
			//errs() <<info.codeLine;
			//if the context of the annotation (context) is equal to the context of the variable declaration (info.context)
			//and the line of the variable declaration is less than the line of the annotation
			//i.e., the variable declaration comes before the annotation, push back into vector
			//do this for all the allocas that correspond to the same source variable. If none have the same basic block,
			//context of the annotation and smaller line, add the predecessors of the annotation's BB to the block stack 
			//and restart looking.
			//
			//
			//
			if(currentContext == info.context && info.codeLine<=codeLine){
				sameContextAndBasicBlockVector.push_back(info);
			}
		}
		if (sameContextAndBasicBlockVector.size()!=0) {
			//search for the nearest mapping in the vector
			VarMappingInfo closest = sameContextAndBasicBlockVector.front();
			for (std::vector<VarMappingInfo>::iterator i = sameContextAndBasicBlockVector.begin(); i!=sameContextAndBasicBlockVector.end(); ++i) {
				VarMappingInfo info = *i;
				if(info.codeLine<=codeLine){
					if(info.codeLine>closest.codeLine){
						closest=info;
					}
				}
			}
			nameIR = closest.nameIR;
			found=true; //stop the search
		}
		else { //go to parent context
			if(currentContext==annotationContext) { //the first time we need to jump one step in the chain of metadata
				if(currentContext->getNumOperands()>=3) {
					currentContext = dyn_cast<MDNode>(annotationContext->getOperand(2)); //!30
					//errs()<<*currentContext;
				}
				else break;
			}
			else {
				if(currentContext->getNumOperands()>=3) {
					currentContext = dyn_cast<MDNode>(currentContext->getOperand(2));//!25, !4
					//errs()<<*currentContext;
					if(currentContext->getNumOperands()>=3) {
						currentContext = dyn_cast<MDNode>(currentContext->getOperand(2));//probably a global variable, TODO later
						//errs()<<*currentContext;
					}
					else break;
				}
				else break;
			}
		}
	}
	return nameIR;
}
	

/*
std::string SafecodeVarMap::findNameIRRigelNew(StringMap< std::vector<VarMappingInfo> > * mappingMap, MDNode *  DbgNode,BasicBlock * basicBlock, std::string  nameSC, int codeLine){

	std::vector<VarMappingInfo> vec = (*mappingMap)[nameSC];
	if(vec.size()==1) { //usually this is the case, i.e. there is only one variable with a specific name in a function.
		VarMappingInfo vmi = vec[0];
		return vmi.nameIR;
	}
	
	//errs() << "Inside findNameIR: DbgNode->getNumOperands = " << DbgNode->getNumOperands() << "\n";
	//errs() << "Inside findNameIR: DbgNode = " << *DbgNode<<"\n";
	MDNode* annotationContext = dyn_cast<MDNode>(DbgNode->getOperand(2)); //!32
	
	//errs()<<"Annotation Context: " <<*annotationContext<<"\n";
	//errs()<<"In Basic Block: " <<basicBlock->getName()<<"\n";
	//errs()<<"Source Code variable: " <<nameSC<<"\n";
    //errs()<<"Codeline: " <<codeLine<<"\n";
	//printMappingMap(mappingMap);
	std::string nameIR="";
	//if we are here, there are several allocas mapped to the same source code variable. 
	//This happens if we declare a variable with the same name in a different scope:
	//int i = 0;
	//while(j>k) {
	//    int i = k+2;
	//}
	//we must find the correct alloca for the variable in the annotation.
	MDNode * currentContext = annotationContext;
	bool found = false;
	bool atTop = false;
	while(!found && !atTop) {
		errs() << "Inside findNameIR: currentContext = " << *currentContext<<"\n";
		if(currentContext==this->topContext) //search once more in top context and then stop
			atTop=true;
		//find all the mappings in the same context and basicBlock
		std::vector<VarMappingInfo> sameContextAndBasicBlockVector;
		for (std::vector<VarMappingInfo>::iterator z = (*mappingMap)[nameSC].begin(); z!=(*mappingMap)[nameSC].end(); ++z) {
			//check if there is a variable map in the same context
			VarMappingInfo info = (*z);
			//errs()<<"checking IR variable: "<<info.nameIR<<"\n";
		//	errs()<<"   with context: " << *(info.context)<<"\n";
			//errs()<<"   declared in BB: " << info.basicBlock->getName()<<"\n";
			//errs()<<"   at line: "<<info.codeLine<<"\n";
			
			//errs()<<*currentContext<<"\n";
			//errs()<<codeLine<<"\n";
			//errs() <<info.codeLine;
			//if the context of the annotation (context) is equal to the context of the variable declaration (info.context)
			//and the line of the variable declaration is less than the line of the annotation
			//i.e., the variable declaration comes before the annotation, push back into vector
			//do this for all the allocas that correspond to the same source variable. If none have the same basic block,
			//context of the annotation and smaller line, add the predecessors of the annotation's BB to the block stack 
			//and restart looking.
			//
			//
			//
			if(currentContext == info.context && info.codeLine<=codeLine){
				sameContextAndBasicBlockVector.push_back(info);
			}
		}
		if (sameContextAndBasicBlockVector.size()!=0) {
			//search for the nearest mapping in the vector
			VarMappingInfo closest = sameContextAndBasicBlockVector.front();
			for (std::vector<VarMappingInfo>::iterator i = sameContextAndBasicBlockVector.begin(); i!=sameContextAndBasicBlockVector.end(); ++i) {
				VarMappingInfo info = *i;
				if(info.codeLine<=codeLine){
					if(info.codeLine>closest.codeLine){
						closest=info;
					}
				}
			}
			nameIR = closest.nameIR;
			found=true; //stop the search
		}
		else { //go to parent context
			if(currentContext==annotationContext) { //the first time we need to jump one step in the chain of metadata
				if(currentContext->getNumOperands()>=3) {
					currentContext = dyn_cast<MDNode>(annotationContext->getOperand(2)); //!30
					//errs()<<*currentContext;
				}
				else break;
			}
			else {
				if(currentContext->getNumOperands()>=3) {
					currentContext = dyn_cast<MDNode>(currentContext->getOperand(2));//!25, !4
					//errs()<<*currentContext;
					if(currentContext->getNumOperands()>=3) {
						currentContext = dyn_cast<MDNode>(currentContext->getOperand(2));//probably a global variable, TODO later
						//errs()<<*currentContext;
					}
					else break;
				}
				else break;
			}
		}
	}
	return nameIR;
}
*/


void SafecodeVarMap::printIrNameRangeMap(std::map<std::string, ranges::Range> irNameRangeMap) {
	DEBUG(errs() << "Printing irNameRangeMap: \n");
	for(std::map<std::string, ranges::Range>::iterator i = irNameRangeMap.begin(); i!=irNameRangeMap.end(); ++i ) {
		std::string key = i->first;
		ranges::Range value = i->second;
		DEBUG(errs() << key << " | "<<value << "\n");
	}
}

void SafecodeVarMap::printMappingMap(StringMap< std::vector<VarMappingInfo> > * mappingMap){
	errs()<<"===-------------------------------------------------------------------------===\n";
	errs() << "               ... Mapping Informations ...\n";
	errs()<<"===-------------------------------------------------------------------------===\n\n";
	for(StringMapIterator<std::vector<VarMappingInfo> > i = mappingMap->begin();i!=mappingMap->end();++i){
		StringRef varName = i->getKey();
		errs()<<"Info about variable: "<<varName<<"\n";
		for (std::vector<VarMappingInfo>::iterator j = (*mappingMap)[varName].begin(); j!= (*mappingMap)[varName].end(); ++j) {
			VarMappingInfo info = *j;
			errs()<<"\t"<< info.nameIR << " -> " << info.context << " -> "<< info.codeLine << "\n";
		}
	}
	errs()<<"===-------------------------------------------------------------------------===\n\n";
	
}

void SafecodeVarMap::printSafecodeMap(std::map<Instruction *, std::map<std::string, ranges::Range> > safecodeMap) {
	errs()<<"---------Printing safecodeVarMap-----------\n";
	for(std::map<Instruction *, std::map<std::string, ranges::Range> >::iterator it = safecodeMap.begin(); it != safecodeMap.end();++it) {
		Instruction* instruction = it->first;
		errs() << "Key: " <<*instruction<<" ->\n";
		std::map<std::string, ranges::Range> mapped = it->second;
		for(std::map<std::string, ranges::Range>::iterator i = mapped.begin(); i != mapped.end();++i) {
			std::string var = i->first;
			ranges::Range range = i->second;
			errs() << "    " << var << " -> " << range << "\n";
		}
		
	}
}

void SafecodeVarMap::printReverseNameSCMap(StringMap< std::string > * reverseNameSCMap){
	//errs()<<"===-------------------------------------------------------------------------===\n";
	//errs() << "               ... Reverse Mapping Informations ...\n";
	//errs()<<"===-------------------------------------------------------------------------===\n\n";
	for(StringMapIterator<std::string> i = reverseNameSCMap->begin();i!=reverseNameSCMap->end();++i){
		StringRef varName = i->getKey();
		 //errs()<<"NameIR: "<<varName<<" -> "<< (&*i)->getValue() <<"\n";
	}
	 //errs()<<"===-------------------------------------------------------------------------===\n\n";
	
}

void SafecodeVarMap::printConstantsMap(StringMap< std::vector<std::string> > * constantsMap){
	 //errs()<<"===-------------------------------------------------------------------------===\n";
	 //errs() << "               ... Constans Mapping Informations ...\n";
	 //errs()<<"===-------------------------------------------------------------------------===\n\n";
	for(StringMapIterator<std::vector<std::string> > i = constantsMap->begin();i!=constantsMap->end();++i){
		StringRef varName = i->getKey();
		 //errs()<<"Constant: "<<varName<<"\n";
		for (std::vector<std::string>::iterator j = (*constantsMap)[varName].begin(); j!= (*constantsMap)[varName].end(); ++j) {
			 //errs()<<"\t"<<*j<<"\n";
		}
	}
	//errs()<<"===-------------------------------------------------------------------------===\n\n";
	
}

std::pair< std::string , ranges::Range> SafecodeVarMap::getRangePair(ACSLExpression *expr){
	if (ACSLBinaryExpression * binexp = llvm::dyn_cast<ACSLBinaryExpression>(expr)) {

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

					ranges::Range p(value->value, value->value);
					std::pair<std::string, ranges::Range > sp(id->name, p);
					return sp;

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



									std::string name1 = id1->name;

									int integer1 = constant1->value;



									std::string name2 = id2->name;

									int integer2 = constant2->value;



									//it should be about the same var

									if(name1==name2){

										//TODO: handle different combinations of <, >, ...

										if(op1==6 && op2==5){
											ranges::Range p(integer1, integer2);
											APInt int1(32,integer1, true);
											APInt int2(32,integer2, true);
											DEBUG(errs()<<"string: "<< name1<<": ["<<int1 <<", "<<int2<<"]\n";);
											///DEBUG(errs()<<int1;);
											//DEBUG(errs()<<", ";);
											//DEBUG(errs()<<", ";);
											std::pair<std::string, ranges::Range > sp(name1, p);
											return sp;
										}

									}

								}

							}

						}

					}



				}

			}

		}

		//if it is a short range (ex. val==const1 || ... || val==constN)

		if (binexp->op==8) {

			if(ACSLBinaryExpression * binexp2 = llvm::dyn_cast<ACSLBinaryExpression>(&(binexp->rhs))){

				bool bottomFound = false;

				bool topFound = false;

				int top;

				int bottom;

				std::string varname;

				//take the top of the range

				if (binexp2->op==1) {

					if (ACSLInteger * constant2 = llvm::dyn_cast<ACSLInteger>(&(binexp2->rhs))) {

						if (ACSLIdentifier * id2 = llvm::dyn_cast<ACSLIdentifier>(&(binexp2->lhs))) {

							varname = id2->name;

							top = constant2->value;

							topFound=true;

						}

					}

				}

				//take the bottom of the range

				if(ACSLBinaryExpression * binexp1 = llvm::dyn_cast<ACSLBinaryExpression>(&(binexp->lhs))){

					ACSLBinaryExpression * tempExp = binexp1;

					while (!bottomFound) {

						if (tempExp->op==1) {

							if (ACSLInteger * constant1 = llvm::dyn_cast<ACSLInteger>(&(tempExp->rhs))) {

								if (ACSLIdentifier * id1 = llvm::dyn_cast<ACSLIdentifier>(&(tempExp->lhs))) {

									if(varname==id1->name){

										bottom = constant1->value;

										bottomFound = true;

									}

								}

							}

							break;

						}

						else if(tempExp->op==8){

							if(ACSLBinaryExpression * lhsTemp = llvm::dyn_cast<ACSLBinaryExpression>(&(tempExp->lhs))){

								tempExp = lhsTemp;

							}

						} else {

							break;

						}

					}

				}

				if (topFound && bottomFound) {
					ranges::Range p(bottom, top);
					std::pair<std::string, ranges::Range > sp(varname, p);
					return sp;
				}

			}

		}

	}
	return std::make_pair("#", ranges::Range(0,0));
}

}//end of anonymous namespace



char SafecodeVarMap::ID = 0;
static RegisterPass<SafecodeVarMap> X("safecodevarmap", "Safecode Acsl Variable Mapping Pass");
