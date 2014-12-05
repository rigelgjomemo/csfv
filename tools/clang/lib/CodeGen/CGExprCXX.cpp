//===--- CGExprCXX.cpp - Emit LLVM Code for C++ expressions ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with code generation of C++ expressions
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CGCUDARuntime.h"
#include "CGCXXABI.h"
#include "CGDebugInfo.h"
#include "CGObjCRuntime.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "clang/Frontend/CodeGenOptions.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Intrinsics.h"

using namespace clang;
using namespace CodeGen;

RValue CodeGenFunction::EmitCXXMemberCall(const CXXMethodDecl *MD,
                                          SourceLocation CallLoc,
                                          llvm::Value *Callee,
                                          ReturnValueSlot ReturnValue,
                                          llvm::Value *This,
                                          llvm::Value *ImplicitParam,
                                          QualType ImplicitParamTy,
                                          CallExpr::const_arg_iterator ArgBeg,
                                          CallExpr::const_arg_iterator ArgEnd) {
  assert(MD->isInstance() &&
         "Trying to emit a member call expr on a static method!");

  // C++11 [class.mfct.non-static]p2:
  //   If a non-static member function of a class X is called for an object that
  //   is not of type X, or of a type derived from X, the behavior is undefined.
  EmitTypeCheck(isa<CXXConstructorDecl>(MD) ? TCK_ConstructorCall
                                            : TCK_MemberCall,
                CallLoc, This, getContext().getRecordType(MD->getParent()));

  CallArgList Args;

  // Push the this ptr.
  Args.add(RValue::get(This), MD->getThisType(getContext()));

  // If there is an implicit parameter (e.g. VTT), emit it.
  if (ImplicitParam) {
    Args.add(RValue::get(ImplicitParam), ImplicitParamTy);
  }

  const FunctionProtoType *FPT = MD->getType()->castAs<FunctionProtoType>();
  RequiredArgs required = RequiredArgs::forPrototypePlus(FPT, Args.size());
  
  // And the rest of the call args.
  EmitCallArgs(Args, FPT, ArgBeg, ArgEnd);

  return EmitCall(CGM.getTypes().arrangeCXXMethodCall(Args, FPT, required),
                  Callee, ReturnValue, Args, MD);
}

static CXXRecordDecl *getCXXRecord(const Expr *E) {
  QualType T = E->getType();
  if (const PointerType *PTy = T->getAs<PointerType>())
    T = PTy->getPointeeType();
  const RecordType *Ty = T->castAs<RecordType>();
  return cast<CXXRecordDecl>(Ty->getDecl());
}

// Note: This function also emit constructor calls to support a MSVC
// extensions allowing explicit constructor function call.
RValue CodeGenFunction::EmitCXXMemberCallExpr(const CXXMemberCallExpr *CE,
                                              ReturnValueSlot ReturnValue) {
  const Expr *callee = CE->getCallee()->IgnoreParens();

  if (isa<BinaryOperator>(callee))
    return EmitCXXMemberPointerCallExpr(CE, ReturnValue);

  const MemberExpr *ME = cast<MemberExpr>(callee);
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(ME->getMemberDecl());

  if (MD->isStatic()) {
    // The method is static, emit it as we would a regular call.
    llvm::Value *Callee = CGM.GetAddrOfFunction(MD);
    return EmitCall(getContext().getPointerType(MD->getType()), Callee,
                    CE->getLocStart(), ReturnValue, CE->arg_begin(),
                    CE->arg_end());
  }

  // Compute the object pointer.
  const Expr *Base = ME->getBase();
  bool CanUseVirtualCall = MD->isVirtual() && !ME->hasQualifier();

  const CXXMethodDecl *DevirtualizedMethod = nullptr;
  if (CanUseVirtualCall && CanDevirtualizeMemberFunctionCall(Base, MD)) {
    const CXXRecordDecl *BestDynamicDecl = Base->getBestDynamicClassType();
    DevirtualizedMethod = MD->getCorrespondingMethodInClass(BestDynamicDecl);
    assert(DevirtualizedMethod);
    const CXXRecordDecl *DevirtualizedClass = DevirtualizedMethod->getParent();
    const Expr *Inner = Base->ignoreParenBaseCasts();
    if (getCXXRecord(Inner) == DevirtualizedClass)
      // If the class of the Inner expression is where the dynamic method
      // is defined, build the this pointer from it.
      Base = Inner;
    else if (getCXXRecord(Base) != DevirtualizedClass) {
      // If the method is defined in a class that is not the best dynamic
      // one or the one of the full expression, we would have to build
      // a derived-to-base cast to compute the correct this pointer, but
      // we don't have support for that yet, so do a virtual call.
      DevirtualizedMethod = nullptr;
    }
    // If the return types are not the same, this might be a case where more
    // code needs to run to compensate for it. For example, the derived
    // method might return a type that inherits form from the return
    // type of MD and has a prefix.
    // For now we just avoid devirtualizing these covariant cases.
    if (DevirtualizedMethod &&
        DevirtualizedMethod->getReturnType().getCanonicalType() !=
            MD->getReturnType().getCanonicalType())
      DevirtualizedMethod = nullptr;
  }

  llvm::Value *This;
  if (ME->isArrow())
    This = EmitScalarExpr(Base);
  else
    This = EmitLValue(Base).getAddress();


  if (MD->isTrivial()) {
    if (isa<CXXDestructorDecl>(MD)) return RValue::get(nullptr);
    if (isa<CXXConstructorDecl>(MD) && 
        cast<CXXConstructorDecl>(MD)->isDefaultConstructor())
      return RValue::get(nullptr);

    if (MD->isCopyAssignmentOperator() || MD->isMoveAssignmentOperator()) {
      // We don't like to generate the trivial copy/move assignment operator
      // when it isn't necessary; just produce the proper effect here.
      llvm::Value *RHS = EmitLValue(*CE->arg_begin()).getAddress();
      EmitAggregateAssign(This, RHS, CE->getType());
      return RValue::get(This);
    }
    
    if (isa<CXXConstructorDecl>(MD) && 
        cast<CXXConstructorDecl>(MD)->isCopyOrMoveConstructor()) {
      // Trivial move and copy ctor are the same.
      llvm::Value *RHS = EmitLValue(*CE->arg_begin()).getAddress();
      EmitSynthesizedCXXCopyCtorCall(cast<CXXConstructorDecl>(MD), This, RHS,
                                     CE->arg_begin(), CE->arg_end());
      return RValue::get(This);
    }
    llvm_unreachable("unknown trivial member function");
  }

  // Compute the function type we're calling.
  const CXXMethodDecl *CalleeDecl = DevirtualizedMethod ? DevirtualizedMethod : MD;
  const CGFunctionInfo *FInfo = nullptr;
  if (const CXXDestructorDecl *Dtor = dyn_cast<CXXDestructorDecl>(CalleeDecl))
    FInfo = &CGM.getTypes().arrangeCXXDestructor(Dtor,
                                                 Dtor_Complete);
  else if (const CXXConstructorDecl *Ctor = dyn_cast<CXXConstructorDecl>(CalleeDecl))
    FInfo = &CGM.getTypes().arrangeCXXConstructorDeclaration(Ctor,
                                                             Ctor_Complete);
  else
    FInfo = &CGM.getTypes().arrangeCXXMethodDeclaration(CalleeDecl);

  llvm::FunctionType *Ty = CGM.getTypes().GetFunctionType(*FInfo);

  // C++ [class.virtual]p12:
  //   Explicit qualification with the scope operator (5.1) suppresses the
  //   virtual call mechanism.
  //
  // We also don't emit a virtual call if the base expression has a record type
  // because then we know what the type is.
  bool UseVirtualCall = CanUseVirtualCall && !DevirtualizedMethod;
  llvm::Value *Callee;

  if (const CXXDestructorDecl *Dtor = dyn_cast<CXXDestructorDecl>(MD)) {
    assert(CE->arg_begin() == CE->arg_end() &&
           "Destructor shouldn't have explicit parameters");
    assert(ReturnValue.isNull() && "Destructor shouldn't have return value");
    if (UseVirtualCall) {
      CGM.getCXXABI().EmitVirtualDestructorCall(*this, Dtor, Dtor_Complete,
                                                CE->getExprLoc(), This);
    } else {
      if (getLangOpts().AppleKext &&
          MD->isVirtual() &&
          ME->hasQualifier())
        Callee = BuildAppleKextVirtualCall(MD, ME->getQualifier(), Ty);
      else if (!DevirtualizedMethod)
        Callee = CGM.GetAddrOfCXXDestructor(Dtor, Dtor_Complete, FInfo, Ty);
      else {
        const CXXDestructorDecl *DDtor =
          cast<CXXDestructorDecl>(DevirtualizedMethod);
        Callee = CGM.GetAddrOfFunction(GlobalDecl(DDtor, Dtor_Complete), Ty);
      }
      EmitCXXMemberCall(MD, CE->getExprLoc(), Callee, ReturnValue, This,
                        /*ImplicitParam=*/nullptr, QualType(), nullptr,nullptr);
    }
    return RValue::get(nullptr);
  }
  
  if (const CXXConstructorDecl *Ctor = dyn_cast<CXXConstructorDecl>(MD)) {
    Callee = CGM.GetAddrOfFunction(GlobalDecl(Ctor, Ctor_Complete), Ty);
  } else if (UseVirtualCall) {
    Callee = CGM.getCXXABI().getVirtualFunctionPointer(*this, MD, This, Ty);
  } else {
    if (getLangOpts().AppleKext &&
        MD->isVirtual() &&
        ME->hasQualifier())
      Callee = BuildAppleKextVirtualCall(MD, ME->getQualifier(), Ty);
    else if (!DevirtualizedMethod)
      Callee = CGM.GetAddrOfFunction(MD, Ty);
    else {
      Callee = CGM.GetAddrOfFunction(DevirtualizedMethod, Ty);
    }
  }

  if (MD->isVirtual()) {
    This = CGM.getCXXABI().adjustThisArgumentForVirtualFunctionCall(
        *this, MD, This, UseVirtualCall);
  }

  return EmitCXXMemberCall(MD, CE->getExprLoc(), Callee, ReturnValue, This,
                           /*ImplicitParam=*/nullptr, QualType(),
                           CE->arg_begin(), CE->arg_end());
}

RValue
CodeGenFunction::EmitCXXMemberPointerCallExpr(const CXXMemberCallExpr *E,
                                              ReturnValueSlot ReturnValue) {
  const BinaryOperator *BO =
      cast<BinaryOperator>(E->getCallee()->IgnoreParens());
  const Expr *BaseExpr = BO->getLHS();
  const Expr *MemFnExpr = BO->getRHS();
  
  const MemberPointerType *MPT = 
    MemFnExpr->getType()->castAs<MemberPointerType>();

  const FunctionProtoType *FPT = 
    MPT->getPointeeType()->castAs<FunctionProtoType>();
  const CXXRecordDecl *RD = 
    cast<CXXRecordDecl>(MPT->getClass()->getAs<RecordType>()->getDecl());

  // Get the member function pointer.
  llvm::Value *MemFnPtr = EmitScalarExpr(MemFnExpr);

  // Emit the 'this' pointer.
  llvm::Value *This;
  
  if (BO->getOpcode() == BO_PtrMemI)
    This = EmitScalarExpr(BaseExpr);
  else 
    This = EmitLValue(BaseExpr).getAddress();

  EmitTypeCheck(TCK_MemberCall, E->getExprLoc(), This,
                QualType(MPT->getClass(), 0));

  // Ask the ABI to load the callee.  Note that This is modified.
  llvm::Value *Callee =
    CGM.getCXXABI().EmitLoadOfMemberFunctionPointer(*this, BO, This, MemFnPtr, MPT);
  
  CallArgList Args;

  QualType ThisType = 
    getContext().getPointerType(getContext().getTagDeclType(RD));

  // Push the this ptr.
  Args.add(RValue::get(This), ThisType);

  RequiredArgs required = RequiredArgs::forPrototypePlus(FPT, 1);
  
  // And the rest of the call args
  EmitCallArgs(Args, FPT, E->arg_begin(), E->arg_end());
  return EmitCall(CGM.getTypes().arrangeCXXMethodCall(Args, FPT, required),
                  Callee, ReturnValue, Args);
}

RValue
CodeGenFunction::EmitCXXOperatorMemberCallExpr(const CXXOperatorCallExpr *E,
                                               const CXXMethodDecl *MD,
                                               ReturnValueSlot ReturnValue) {
  assert(MD->isInstance() &&
         "Trying to emit a member call expr on a static method!");
  LValue LV = EmitLValue(E->getArg(0));
  llvm::Value *This = LV.getAddress();

  if ((MD->isCopyAssignmentOperator() || MD->isMoveAssignmentOperator()) &&
      MD->isTrivial()) {
    llvm::Value *Src = EmitLValue(E->getArg(1)).getAddress();
    QualType Ty = E->getType();
    EmitAggregateAssign(This, Src, Ty);
    return RValue::get(This);
  }

  llvm::Value *Callee = EmitCXXOperatorMemberCallee(E, MD, This);
  return EmitCXXMemberCall(MD, E->getExprLoc(), Callee, ReturnValue, This,
                           /*ImplicitParam=*/nullptr, QualType(),
                           E->arg_begin() + 1, E->arg_end());
}

RValue CodeGenFunction::EmitCUDAKernelCallExpr(const CUDAKernelCallExpr *E,
                                               ReturnValueSlot ReturnValue) {
  return CGM.getCUDARuntime().EmitCUDAKernelCallExpr(*this, E, ReturnValue);
}

static void EmitNullBaseClassInitialization(CodeGenFunction &CGF,
                                            llvm::Value *DestPtr,
                                            const CXXRecordDecl *Base) {
  if (Base->isEmpty())
    return;

  DestPtr = CGF.EmitCastToVoidPtr(DestPtr);

  const ASTRecordLayout &Layout = CGF.getContext().getASTRecordLayout(Base);
  CharUnits Size = Layout.getNonVirtualSize();
  CharUnits Align = Layout.getNonVirtualAlignment();

  llvm::Value *SizeVal = CGF.CGM.getSize(Size);

  // If the type contains a pointer to data member we can't memset it to zero.
  // Instead, create a null constant and copy it to the destination.
  // TODO: there are other patterns besides zero that we can usefully memset,
  // like -1, which happens to be the pattern used by member-pointers.
  // TODO: isZeroInitializable can be over-conservative in the case where a
  // virtual base contains a member pointer.
  if (!CGF.CGM.getTypes().isZeroInitializable(Base)) {
    llvm::Constant *NullConstant = CGF.CGM.EmitNullConstantForBase(Base);

    llvm::GlobalVariable *NullVariable = 
      new llvm::GlobalVariable(CGF.CGM.getModule(), NullConstant->getType(),
                               /*isConstant=*/true, 
                               llvm::GlobalVariable::PrivateLinkage,
                               NullConstant, Twine());
    NullVariable->setAlignment(Align.getQuantity());
    llvm::Value *SrcPtr = CGF.EmitCastToVoidPtr(NullVariable);

    // Get and call the appropriate llvm.memcpy overload.
    CGF.Builder.CreateMemCpy(DestPtr, SrcPtr, SizeVal, Align.getQuantity());
    return;
  } 
  
  // Otherwise, just memset the whole thing to zero.  This is legal
  // because in LLVM, all default initializers (other than the ones we just
  // handled above) are guaranteed to have a bit pattern of all zeros.
  CGF.Builder.CreateMemSet(DestPtr, CGF.Builder.getInt8(0), SizeVal,
                           Align.getQuantity());
}

void
CodeGenFunction::EmitCXXConstructExpr(const CXXConstructExpr *E,
                                      AggValueSlot Dest) {
  assert(!Dest.isIgnored() && "Must have a destination!");
  const CXXConstructorDecl *CD = E->getConstructor();
  
  // If we require zero initialization before (or instead of) calling the
  // constructor, as can be the case with a non-user-provided default
  // constructor, emit the zero initialization now, unless destination is
  // already zeroed.
  if (E->requiresZeroInitialization() && !Dest.isZeroed()) {
    switch (E->getConstructionKind()) {
    case CXXConstructExpr::CK_Delegating:
    case CXXConstructExpr::CK_Complete:
      EmitNullInitialization(Dest.getAddr(), E->getType());
      break;
    case CXXConstructExpr::CK_VirtualBase:
    case CXXConstructExpr::CK_NonVirtualBase:
      EmitNullBaseClassInitialization(*this, Dest.getAddr(), CD->getParent());
      break;
    }
  }
  
  // If this is a call to a trivial default constructor, do nothing.
  if (CD->isTrivial() && CD->isDefaultConstructor())
    return;
  
  // Elide the constructor if we're constructing from a temporary.
  // The temporary check is required because Sema sets this on NRVO
  // returns.
  if (getLangOpts().ElideConstructors && E->isElidable()) {
    assert(getContext().hasSameUnqualifiedType(E->getType(),
                                               E->getArg(0)->getType()));
    if (E->getArg(0)->isTemporaryObject(getContext(), CD->getParent())) {
      EmitAggExpr(E->getArg(0), Dest);
      return;
    }
  }
  
  if (const ConstantArrayType *arrayType 
        = getContext().getAsConstantArrayType(E->getType())) {
    EmitCXXAggrConstructorCall(CD, arrayType, Dest.getAddr(), 
                               E->arg_begin(), E->arg_end());
  } else {
    CXXCtorType Type = Ctor_Complete;
    bool ForVirtualBase = false;
    bool Delegating = false;
    
    switch (E->getConstructionKind()) {
     case CXXConstructExpr::CK_Delegating:
      // We should be emitting a constructor; GlobalDecl will assert this
      Type = CurGD.getCtorType();
      Delegating = true;
      break;

     case CXXConstructExpr::CK_Complete:
      Type = Ctor_Complete;
      break;

     case CXXConstructExpr::CK_VirtualBase:
      ForVirtualBase = true;
      // fall-through

     case CXXConstructExpr::CK_NonVirtualBase:
      Type = Ctor_Base;
    }
    
    // Call the constructor.
    EmitCXXConstructorCall(CD, Type, ForVirtualBase, Delegating, Dest.getAddr(),
                           E->arg_begin(), E->arg_end());
  }
}

void
CodeGenFunction::EmitSynthesizedCXXCopyCtor(llvm::Value *Dest, 
                                            llvm::Value *Src,
                                            const Expr *Exp) {
  if (const ExprWithCleanups *E = dyn_cast<ExprWithCleanups>(Exp))
    Exp = E->getSubExpr();
  assert(isa<CXXConstructExpr>(Exp) && 
         "EmitSynthesizedCXXCopyCtor - unknown copy ctor expr");
  const CXXConstructExpr* E = cast<CXXConstructExpr>(Exp);
  const CXXConstructorDecl *CD = E->getConstructor();
  RunCleanupsScope Scope(*this);
  
  // If we require zero initialization before (or instead of) calling the
  // constructor, as can be the case with a non-user-provided default
  // constructor, emit the zero initialization now.
  // FIXME. Do I still need this for a copy ctor synthesis?
  if (E->requiresZeroInitialization())
    EmitNullInitialization(Dest, E->getType());
  
  assert(!getContext().getAsConstantArrayType(E->getType())
         && "EmitSynthesizedCXXCopyCtor - Copied-in Array");
  EmitSynthesizedCXXCopyCtorCall(CD, Dest, Src, E->arg_begin(), E->arg_end());
}

static CharUnits CalculateCookiePadding(CodeGenFunction &CGF,
                                        const CXXNewExpr *E) {
  if (!E->isArray())
    return CharUnits::Zero();

  // No cookie is required if the operator new[] being used is the
  // reserved placement operator new[].
  if (E->getOperatorNew()->isReservedGlobalPlacementOperator())
    return CharUnits::Zero();

  return CGF.CGM.getCXXABI().GetArrayCookieSize(E);
}

static llvm::Value *EmitCXXNewAllocSize(CodeGenFunction &CGF,
                                        const CXXNewExpr *e,
                                        unsigned minElements,
                                        llvm::Value *&numElements,
                                        llvm::Value *&sizeWithoutCookie) {
  QualType type = e->getAllocatedType();

  if (!e->isArray()) {
    CharUnits typeSize = CGF.getContext().getTypeSizeInChars(type);
    sizeWithoutCookie
      = llvm::ConstantInt::get(CGF.SizeTy, typeSize.getQuantity());
    return sizeWithoutCookie;
  }

  // The width of size_t.
  unsigned sizeWidth = CGF.SizeTy->getBitWidth();

  // Figure out the cookie size.
  llvm::APInt cookieSize(sizeWidth,
                         CalculateCookiePadding(CGF, e).getQuantity());

  // Emit the array size expression.
  // We multiply the size of all dimensions for NumElements.
  // e.g for 'int[2][3]', ElemType is 'int' and NumElements is 6.
  numElements = CGF.EmitScalarExpr(e->getArraySize());
  assert(isa<llvm::IntegerType>(numElements->getType()));

  // The number of elements can be have an arbitrary integer type;
  // essentially, we need to multiply it by a constant factor, add a
  // cookie size, and verify that the result is representable as a
  // size_t.  That's just a gloss, though, and it's wrong in one
  // important way: if the count is negative, it's an error even if
  // the cookie size would bring the total size >= 0.
  bool isSigned 
    = e->getArraySize()->getType()->isSignedIntegerOrEnumerationType();
  llvm::IntegerType *numElementsType
    = cast<llvm::IntegerType>(numElements->getType());
  unsigned numElementsWidth = numElementsType->getBitWidth();

  // Compute the constant factor.
  llvm::APInt arraySizeMultiplier(sizeWidth, 1);
  while (const ConstantArrayType *CAT
             = CGF.getContext().getAsConstantArrayType(type)) {
    type = CAT->getElementType();
    arraySizeMultiplier *= CAT->getSize();
  }

  CharUnits typeSize = CGF.getContext().getTypeSizeInChars(type);
  llvm::APInt typeSizeMultiplier(sizeWidth, typeSize.getQuantity());
  typeSizeMultiplier *= arraySizeMultiplier;

  // This will be a size_t.
  llvm::Value *size;
  
  // If someone is doing 'new int[42]' there is no need to do a dynamic check.
  // Don't bloat the -O0 code.
  if (llvm::ConstantInt *numElementsC =
        dyn_cast<llvm::ConstantInt>(numElements)) {
    const llvm::APInt &count = numElementsC->getValue();

    bool hasAnyOverflow = false;

    // If 'count' was a negative number, it's an overflow.
    if (isSigned && count.isNegative())
      hasAnyOverflow = true;

    // We want to do all this arithmetic in size_t.  If numElements is
    // wider than that, check whether it's already too big, and if so,
    // overflow.
    else if (numElementsWidth > sizeWidth &&
             numElementsWidth - sizeWidth > count.countLeadingZeros())
      hasAnyOverflow = true;

    // Okay, compute a count at the right width.
    llvm::APInt adjustedCount = count.zextOrTrunc(sizeWidth);

    // If there is a brace-initializer, we cannot allocate fewer elements than
    // there are initializers. If we do, that's treated like an overflow.
    if (adjustedCount.ult(minElements))
      hasAnyOverflow = true;

    // Scale numElements by that.  This might overflow, but we don't
    // care because it only overflows if allocationSize does, too, and
    // if that overflows then we shouldn't use this.
    numElements = llvm::ConstantInt::get(CGF.SizeTy,
                                         adjustedCount * arraySizeMultiplier);

    // Compute the size before cookie, and track whether it overflowed.
    bool overflow;
    llvm::APInt allocationSize
      = adjustedCount.umul_ov(typeSizeMultiplier, overflow);
    hasAnyOverflow |= overflow;

    // Add in the cookie, and check whether it's overflowed.
    if (cookieSize != 0) {
      // Save the current size without a cookie.  This shouldn't be
      // used if there was overflow.
      sizeWithoutCookie = llvm::ConstantInt::get(CGF.SizeTy, allocationSize);

      allocationSize = allocationSize.uadd_ov(cookieSize, overflow);
      hasAnyOverflow |= overflow;
    }

    // On overflow, produce a -1 so operator new will fail.
    if (hasAnyOverflow) {
      size = llvm::Constant::getAllOnesValue(CGF.SizeTy);
    } else {
      size = llvm::ConstantInt::get(CGF.SizeTy, allocationSize);
    }

  // Otherwise, we might need to use the overflow intrinsics.
  } else {
    // There are up to five conditions we need to test for:
    // 1) if isSigned, we need to check whether numElements is negative;
    // 2) if numElementsWidth > sizeWidth, we need to check whether
    //   numElements is larger than something representable in size_t;
    // 3) if minElements > 0, we need to check whether numElements is smaller
    //    than that.
    // 4) we need to compute
    //      sizeWithoutCookie := numElements * typeSizeMultiplier
    //    and check whether it overflows; and
    // 5) if we need a cookie, we need to compute
    //      size := sizeWithoutCookie + cookieSize
    //    and check whether it overflows.

    llvm::Value *hasOverflow = nullptr;

    // If numElementsWidth > sizeWidth, then one way or another, we're
    // going to have to do a comparison for (2), and this happens to
    // take care of (1), too.
    if (numElementsWidth > sizeWidth) {
      llvm::APInt threshold(numElementsWidth, 1);
      threshold <<= sizeWidth;

      llvm::Value *thresholdV
        = llvm::ConstantInt::get(numElementsType, threshold);

      hasOverflow = CGF.Builder.CreateICmpUGE(numElements, thresholdV);
      numElements = CGF.Builder.CreateTrunc(numElements, CGF.SizeTy);

    // Otherwise, if we're signed, we want to sext up to size_t.
    } else if (isSigned) {
      if (numElementsWidth < sizeWidth)
        numElements = CGF.Builder.CreateSExt(numElements, CGF.SizeTy);
      
      // If there's a non-1 type size multiplier, then we can do the
      // signedness check at the same time as we do the multiply
      // because a negative number times anything will cause an
      // unsigned overflow.  Otherwise, we have to do it here. But at least
      // in this case, we can subsume the >= minElements check.
      if (typeSizeMultiplier == 1)
        hasOverflow = CGF.Builder.CreateICmpSLT(numElements,
                              llvm::ConstantInt::get(CGF.SizeTy, minElements));

    // Otherwise, zext up to size_t if necessary.
    } else if (numElementsWidth < sizeWidth) {
      numElements = CGF.Builder.CreateZExt(numElements, CGF.SizeTy);
    }

    assert(numElements->getType() == CGF.SizeTy);

    if (minElements) {
      // Don't allow allocation of fewer elements than we have initializers.
      if (!hasOverflow) {
        hasOverflow = CGF.Builder.CreateICmpULT(numElements,
                              llvm::ConstantInt::get(CGF.SizeTy, minElements));
      } else if (numElementsWidth > sizeWidth) {
        // The other existing overflow subsumes this check.
        // We do an unsigned comparison, since any signed value < -1 is
        // taken care of either above or below.
        hasOverflow = CGF.Builder.CreateOr(hasOverflow,
                          CGF.Builder.CreateICmpULT(numElements,
                              llvm::ConstantInt::get(CGF.SizeTy, minElements)));
      }
    }

    size = numElements;

    // Multiply by the type size if necessary.  This multiplier
    // includes all the factors for nested arrays.
    //
    // This step also causes numElements to be scaled up by the
    // nested-array factor if necessary.  Overflow on this computation
    // can be ignored because the result shouldn't be used if
    // allocation fails.
    if (typeSizeMultiplier != 1) {
      llvm::Value *umul_with_overflow
        = CGF.CGM.getIntrinsic(llvm::Intrinsic::umul_with_overflow, CGF.SizeTy);

      llvm::Value *tsmV =
        llvm::ConstantInt::get(CGF.SizeTy, typeSizeMultiplier);
      llvm::Value *result =
        CGF.Builder.CreateCall2(umul_with_overflow, size, tsmV);

      llvm::Value *overflowed = CGF.Builder.CreateExtractValue(result, 1);
      if (hasOverflow)
        hasOverflow = CGF.Builder.CreateOr(hasOverflow, overflowed);
      else
        hasOverflow = overflowed;

      size = CGF.Builder.CreateExtractValue(result, 0);

      // Also scale up numElements by the array size multiplier.
      if (arraySizeMultiplier != 1) {
        // If the base element type size is 1, then we can re-use the
        // multiply we just did.
        if (typeSize.isOne()) {
          assert(arraySizeMultiplier == typeSizeMultiplier);
          numElements = size;

        // Otherwise we need a separate multiply.
        } else {
          llvm::Value *asmV =
            llvm::ConstantInt::get(CGF.SizeTy, arraySizeMultiplier);
          numElements = CGF.Builder.CreateMul(numElements, asmV);
        }
      }
    } else {
      // numElements doesn't need to be scaled.
      assert(arraySizeMultiplier == 1);
    }
    
    // Add in the cookie size if necessary.
    if (cookieSize != 0) {
      sizeWithoutCookie = size;

      llvm::Value *uadd_with_overflow
        = CGF.CGM.getIntrinsic(llvm::Intrinsic::uadd_with_overflow, CGF.SizeTy);

      llvm::Value *cookieSizeV = llvm::ConstantInt::get(CGF.SizeTy, cookieSize);
      llvm::Value *result =
        CGF.Builder.CreateCall2(uadd_with_overflow, size, cookieSizeV);

      llvm::Value *overflowed = CGF.Builder.CreateExtractValue(result, 1);
      if (hasOverflow)
        hasOverflow = CGF.Builder.CreateOr(hasOverflow, overflowed);
      else
        hasOverflow = overflowed;

      size = CGF.Builder.CreateExtractValue(result, 0);
    }

    // If we had any possibility of dynamic overflow, make a select to
    // overwrite 'size' with an all-ones value, which should cause
    // operator new to throw.
    if (hasOverflow)
      size = CGF.Builder.CreateSelect(hasOverflow,
                                 llvm::Constant::getAllOnesValue(CGF.SizeTy),
                                      size);
  }

  if (cookieSize == 0)
    sizeWithoutCookie = size;
  else
    assert(sizeWithoutCookie && "didn't set sizeWithoutCookie?");

  return size;
}

static void StoreAnyExprIntoOneUnit(CodeGenFunction &CGF, const Expr *Init,
                                    QualType AllocType, llvm::Value *NewPtr) {
  // FIXME: Refactor with EmitExprAsInit.
  CharUnits Alignment = CGF.getContext().getTypeAlignInChars(AllocType);
  switch (CGF.getEvaluationKind(AllocType)) {
  case TEK_Scalar:
    CGF.EmitScalarInit(Init, nullptr, CGF.MakeAddrLValue(NewPtr, AllocType,
                                                         Alignment),
                       false);
    return;
  case TEK_Complex:
    CGF.EmitComplexExprIntoLValue(Init, CGF.MakeAddrLValue(NewPtr, AllocType,
                                                           Alignment),
                                  /*isInit*/ true);
    return;
  case TEK_Aggregate: {
    AggValueSlot Slot
      = AggValueSlot::forAddr(NewPtr, Alignment, AllocType.getQualifiers(),
                              AggValueSlot::IsDestructed,
                              AggValueSlot::DoesNotNeedGCBarriers,
                              AggValueSlot::IsNotAliased);
    CGF.EmitAggExpr(Init, Slot);
    return;
  }
  }
  llvm_unreachable("bad evaluation kind");
}

void
CodeGenFunction::EmitNewArrayInitializer(const CXXNewExpr *E,
                                         QualType ElementType,
                                         llvm::Value *BeginPtr,
                                         llvm::Value *NumElements,
                                         llvm::Value *AllocSizeWithoutCookie) {
  // If we have a type with trivial initialization and no initializer,
  // there's nothing to do.
  if (!E->hasInitializer())
    return;

  llvm::Value *CurPtr = BeginPtr;

  unsigned InitListElements = 0;

  const Expr *Init = E->getInitializer();
  llvm::AllocaInst *EndOfInit = nullptr;
  QualType::DestructionKind DtorKind = ElementType.isDestructedType();
  EHScopeStack::stable_iterator Cleanup;
  llvm::Instruction *CleanupDominator = nullptr;

  // If the initializer is an initializer list, first do the explicit elements.
  if (const InitListExpr *ILE = dyn_cast<InitListExpr>(Init)) {
    InitListElements = ILE->getNumInits();

    // If this is a multi-dimensional array new, we will initialize multiple
    // elements with each init list element.
    QualType AllocType = E->getAllocatedType();
    if (const ConstantArrayType *CAT = dyn_cast_or_null<ConstantArrayType>(
            AllocType->getAsArrayTypeUnsafe())) {
      unsigned AS = CurPtr->getType()->getPointerAddressSpace();
      llvm::Type *AllocPtrTy = ConvertTypeForMem(AllocType)->getPointerTo(AS);
      CurPtr = Builder.CreateBitCast(CurPtr, AllocPtrTy);
      InitListElements *= getContext().getConstantArrayElementCount(CAT);
    }

    // Enter a partial-destruction Cleanup if necessary.
    if (needsEHCleanup(DtorKind)) {
      // In principle we could tell the Cleanup where we are more
      // directly, but the control flow can get so varied here that it
      // would actually be quite complex.  Therefore we go through an
      // alloca.
      EndOfInit = CreateTempAlloca(BeginPtr->getType(), "array.init.end");
      CleanupDominator = Builder.CreateStore(BeginPtr, EndOfInit);
      pushIrregularPartialArrayCleanup(BeginPtr, EndOfInit, ElementType,
                                       getDestroyer(DtorKind));
      Cleanup = EHStack.stable_begin();
    }

    for (unsigned i = 0, e = ILE->getNumInits(); i != e; ++i) {
      // Tell the cleanup that it needs to destroy up to this
      // element.  TODO: some of these stores can be trivially
      // observed to be unnecessary.
      if (EndOfInit)
        Builder.CreateStore(Builder.CreateBitCast(CurPtr, BeginPtr->getType()),
                            EndOfInit);
      // FIXME: If the last initializer is an incomplete initializer list for
      // an array, and we have an array filler, we can fold together the two
      // initialization loops.
      StoreAnyExprIntoOneUnit(*this, ILE->getInit(i),
                              ILE->getInit(i)->getType(), CurPtr);
      CurPtr = Builder.CreateConstInBoundsGEP1_32(CurPtr, 1, "array.exp.next");
    }

    // The remaining elements are filled with the array filler expression.
    Init = ILE->getArrayFiller();

    // Extract the initializer for the individual array elements by pulling
    // out the array filler from all the nested initializer lists. This avoids
    // generating a nested loop for the initialization.
    while (Init && Init->getType()->isConstantArrayType()) {
      auto *SubILE = dyn_cast<InitListExpr>(Init);
      if (!SubILE)
        break;
      assert(SubILE->getNumInits() == 0 && "explicit inits in array filler?");
      Init = SubILE->getArrayFiller();
    }

    // Switch back to initializing one base element at a time.
    CurPtr = Builder.CreateBitCast(CurPtr, BeginPtr->getType());
  }

  // Attempt to perform zero-initialization using memset.
  auto TryMemsetInitialization = [&]() -> bool {
    // FIXME: If the type is a pointer-to-data-member under the Itanium ABI,
    // we can initialize with a memset to -1.
    if (!CGM.getTypes().isZeroInitializable(ElementType))
      return false;

    // Optimization: since zero initialization will just set the memory
    // to all zeroes, generate a single memset to do it in one shot.

    // Subtract out the size of any elements we've already initialized.
    auto *RemainingSize = AllocSizeWithoutCookie;
    if (InitListElements) {
      // We know this can't overflow; we check this when doing the allocation.
      auto *InitializedSize = llvm::ConstantInt::get(
          RemainingSize->getType(),
          getContext().getTypeSizeInChars(ElementType).getQuantity() *
              InitListElements);
      RemainingSize = Builder.CreateSub(RemainingSize, InitializedSize);
    }

    // Create the memset.
    CharUnits Alignment = getContext().getTypeAlignInChars(ElementType);
    Builder.CreateMemSet(CurPtr, Builder.getInt8(0), RemainingSize,
                         Alignment.getQuantity(), false);
    return true;
  };

  // If all elements have already been initialized, skip any further
  // initialization.
  llvm::ConstantInt *ConstNum = dyn_cast<llvm::ConstantInt>(NumElements);
  if (ConstNum && ConstNum->getZExtValue() <= InitListElements) {
    // If there was a Cleanup, deactivate it.
    if (CleanupDominator)
      DeactivateCleanupBlock(Cleanup, CleanupDominator);
    return;
  }

  assert(Init && "have trailing elements to initialize but no initializer");

  // If this is a constructor call, try to optimize it out, and failing that
  // emit a single loop to initialize all remaining elements.
  if (const CXXConstructExpr *CCE = dyn_cast<CXXConstructExpr>(Init)) {
    CXXConstructorDecl *Ctor = CCE->getConstructor();
    if (Ctor->isTrivial()) {
      // If new expression did not specify value-initialization, then there
      // is no initialization.
      if (!CCE->requiresZeroInitialization() || Ctor->getParent()->isEmpty())
        return;

      if (TryMemsetInitialization())
        return;
    }

    // Store the new Cleanup position for irregular Cleanups.
    //
    // FIXME: Share this cleanup with the constructor call emission rather than
    // having it create a cleanup of its own.
    if (EndOfInit) Builder.CreateStore(CurPtr, EndOfInit);

    // Emit a constructor call loop to initialize the remaining elements.
    if (InitListElements)
      NumElements = Builder.CreateSub(
          NumElements,
          llvm::ConstantInt::get(NumElements->getType(), InitListElements));
    EmitCXXAggrConstructorCall(Ctor, NumElements, CurPtr,
                               CCE->arg_begin(), CCE->arg_end(),
                               CCE->requiresZeroInitialization());
    return;
  }

  // If this is value-initialization, we can usually use memset.
  ImplicitValueInitExpr IVIE(ElementType);
  if (isa<ImplicitValueInitExpr>(Init)) {
    if (TryMemsetInitialization())
      return;

    // Switch to an ImplicitValueInitExpr for the element type. This handles
    // only one case: multidimensional array new of pointers to members. In
    // all other cases, we already have an initializer for the array element.
    Init = &IVIE;
  }

  // At this point we should have found an initializer for the individual
  // elements of the array.
  assert(getContext().hasSameUnqualifiedType(ElementType, Init->getType()) &&
         "got wrong type of element to initialize");

  // If we have an empty initializer list, we can usually use memset.
  if (auto *ILE = dyn_cast<InitListExpr>(Init))
    if (ILE->getNumInits() == 0 && TryMemsetInitialization())
      return;

  // Create the loop blocks.
  llvm::BasicBlock *EntryBB = Builder.GetInsertBlock();
  llvm::BasicBlock *LoopBB = createBasicBlock("new.loop");
  llvm::BasicBlock *ContBB = createBasicBlock("new.loop.end");

  // Find the end of the array, hoisted out of the loop.
  llvm::Value *EndPtr =
    Builder.CreateInBoundsGEP(BeginPtr, NumElements, "array.end");

  // If the number of elements isn't constant, we have to now check if there is
  // anything left to initialize.
  if (!ConstNum) {
    llvm::Value *IsEmpty = Builder.CreateICmpEQ(CurPtr, EndPtr,
                                                "array.isempty");
    Builder.CreateCondBr(IsEmpty, ContBB, LoopBB);
  }

  // Enter the loop.
  EmitBlock(LoopBB);

  // Set up the current-element phi.
  llvm::PHINode *CurPtrPhi =
    Builder.CreatePHI(CurPtr->getType(), 2, "array.cur");
  CurPtrPhi->addIncoming(CurPtr, EntryBB);
  CurPtr = CurPtrPhi;

  // Store the new Cleanup position for irregular Cleanups.
  if (EndOfInit) Builder.CreateStore(CurPtr, EndOfInit);

  // Enter a partial-destruction Cleanup if necessary.
  if (!CleanupDominator && needsEHCleanup(DtorKind)) {
    pushRegularPartialArrayCleanup(BeginPtr, CurPtr, ElementType,
                                   getDestroyer(DtorKind));
    Cleanup = EHStack.stable_begin();
    CleanupDominator = Builder.CreateUnreachable();
  }

  // Emit the initializer into this element.
  StoreAnyExprIntoOneUnit(*this, Init, Init->getType(), CurPtr);

  // Leave the Cleanup if we entered one.
  if (CleanupDominator) {
    DeactivateCleanupBlock(Cleanup, CleanupDominator);
    CleanupDominator->eraseFromParent();
  }

  // Advance to the next element by adjusting the pointer type as necessary.
  llvm::Value *NextPtr =
      Builder.CreateConstInBoundsGEP1_32(CurPtr, 1, "array.next");

  // Check whether we've gotten to the end of the array and, if so,
  // exit the loop.
  llvm::Value *IsEnd = Builder.CreateICmpEQ(NextPtr, EndPtr, "array.atend");
  Builder.CreateCondBr(IsEnd, ContBB, LoopBB);
  CurPtrPhi->addIncoming(NextPtr, Builder.GetInsertBlock());

  EmitBlock(ContBB);
}

static void EmitNewInitializer(CodeGenFunction &CGF, const CXXNewExpr *E,
                               QualType ElementType,
                               llvm::Value *NewPtr,
                               llvm::Value *NumElements,
                               llvm::Value *AllocSizeWithoutCookie) {
  if (E->isArray())
    CGF.EmitNewArrayInitializer(E, ElementType, NewPtr, NumElements,
                                AllocSizeWithoutCookie);
  else if (const Expr *Init = E->getInitializer())
    StoreAnyExprIntoOneUnit(CGF, Init, E->getAllocatedType(), NewPtr);
}

/// Emit a call to an operator new or operator delete function, as implicitly
/// created by new-expressions and delete-expressions.
static RValue EmitNewDeleteCall(CodeGenFunction &CGF,
                                const FunctionDecl *Callee,
                                const FunctionProtoType *CalleeType,
                                const CallArgList &Args) {
  llvm::Instruction *CallOrInvoke;
  llvm::Value *CalleeAddr = CGF.CGM.GetAddrOfFunction(Callee);
  RValue RV =
      CGF.EmitCall(CGF.CGM.getTypes().arrangeFreeFunctionCall(Args, CalleeType),
                   CalleeAddr, ReturnValueSlot(), Args,
                   Callee, &CallOrInvoke);

  /// C++1y [expr.new]p10:
  ///   [In a new-expression,] an implementation is allowed to omit a call
  ///   to a replaceable global allocation function.
  ///
  /// We model such elidable calls with the 'builtin' attribute.
  llvm::Function *Fn = dyn_cast<llvm::Function>(CalleeAddr);
  if (Callee->isReplaceableGlobalAllocationFunction() &&
      Fn && Fn->hasFnAttribute(llvm::Attribute::NoBuiltin)) {
    // FIXME: Add addAttribute to CallSite.
    if (llvm::CallInst *CI = dyn_cast<llvm::CallInst>(CallOrInvoke))
      CI->addAttribute(llvm::AttributeSet::FunctionIndex,
                       llvm::Attribute::Builtin);
    else if (llvm::InvokeInst *II = dyn_cast<llvm::InvokeInst>(CallOrInvoke))
      II->addAttribute(llvm::AttributeSet::FunctionIndex,
                       llvm::Attribute::Builtin);
    else
      llvm_unreachable("unexpected kind of call instruction");
  }

  return RV;
}

RValue CodeGenFunction::EmitBuiltinNewDeleteCall(const FunctionProtoType *Type,
                                                 const Expr *Arg,
                                                 bool IsDelete) {
  CallArgList Args;
  const Stmt *ArgS = Arg;
  EmitCallArgs(Args, *Type->param_type_begin(),
               ConstExprIterator(&ArgS), ConstExprIterator(&ArgS + 1));
  // Find the allocation or deallocation function that we're calling.
  ASTContext &Ctx = getContext();
  DeclarationName Name = Ctx.DeclarationNames
      .getCXXOperatorName(IsDelete ? OO_Delete : OO_New);
  for (auto *Decl : Ctx.getTranslationUnitDecl()->lookup(Name))
    if (auto *FD = dyn_cast<FunctionDecl>(Decl))
      if (Ctx.hasSameType(FD->getType(), QualType(Type, 0)))
        return EmitNewDeleteCall(*this, cast<FunctionDecl>(Decl), Type, Args);
  llvm_unreachable("predeclared global operator new/delete is missing");
}

namespace {
  /// A cleanup to call the given 'operator delete' function upon
  /// abnormal exit from a new expression.
  class CallDeleteDuringNew : public EHScopeStack::Cleanup {
    size_t NumPlacementArgs;
    const FunctionDecl *OperatorDelete;
    llvm::Value *Ptr;
    llvm::Value *AllocSize;

    RValue *getPlacementArgs() { return reinterpret_cast<RValue*>(this+1); }

  public:
    static size_t getExtraSize(size_t NumPlacementArgs) {
      return NumPlacementArgs * sizeof(RValue);
    }

    CallDeleteDuringNew(size_t NumPlacementArgs,
                        const FunctionDecl *OperatorDelete,
                        llvm::Value *Ptr,
                        llvm::Value *AllocSize) 
      : NumPlacementArgs(NumPlacementArgs), OperatorDelete(OperatorDelete),
        Ptr(Ptr), AllocSize(AllocSize) {}

    void setPlacementArg(unsigned I, RValue Arg) {
      assert(I < NumPlacementArgs && "index out of range");
      getPlacementArgs()[I] = Arg;
    }

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      const FunctionProtoType *FPT
        = OperatorDelete->getType()->getAs<FunctionProtoType>();
      assert(FPT->getNumParams() == NumPlacementArgs + 1 ||
             (FPT->getNumParams() == 2 && NumPlacementArgs == 0));

      CallArgList DeleteArgs;

      // The first argument is always a void*.
      FunctionProtoType::param_type_iterator AI = FPT->param_type_begin();
      DeleteArgs.add(RValue::get(Ptr), *AI++);

      // A member 'operator delete' can take an extra 'size_t' argument.
      if (FPT->getNumParams() == NumPlacementArgs + 2)
        DeleteArgs.add(RValue::get(AllocSize), *AI++);

      // Pass the rest of the arguments, which must match exactly.
      for (unsigned I = 0; I != NumPlacementArgs; ++I)
        DeleteArgs.add(getPlacementArgs()[I], *AI++);

      // Call 'operator delete'.
      EmitNewDeleteCall(CGF, OperatorDelete, FPT, DeleteArgs);
    }
  };

  /// A cleanup to call the given 'operator delete' function upon
  /// abnormal exit from a new expression when the new expression is
  /// conditional.
  class CallDeleteDuringConditionalNew : public EHScopeStack::Cleanup {
    size_t NumPlacementArgs;
    const FunctionDecl *OperatorDelete;
    DominatingValue<RValue>::saved_type Ptr;
    DominatingValue<RValue>::saved_type AllocSize;

    DominatingValue<RValue>::saved_type *getPlacementArgs() {
      return reinterpret_cast<DominatingValue<RValue>::saved_type*>(this+1);
    }

  public:
    static size_t getExtraSize(size_t NumPlacementArgs) {
      return NumPlacementArgs * sizeof(DominatingValue<RValue>::saved_type);
    }

    CallDeleteDuringConditionalNew(size_t NumPlacementArgs,
                                   const FunctionDecl *OperatorDelete,
                                   DominatingValue<RValue>::saved_type Ptr,
                              DominatingValue<RValue>::saved_type AllocSize)
      : NumPlacementArgs(NumPlacementArgs), OperatorDelete(OperatorDelete),
        Ptr(Ptr), AllocSize(AllocSize) {}

    void setPlacementArg(unsigned I, DominatingValue<RValue>::saved_type Arg) {
      assert(I < NumPlacementArgs && "index out of range");
      getPlacementArgs()[I] = Arg;
    }

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      const FunctionProtoType *FPT
        = OperatorDelete->getType()->getAs<FunctionProtoType>();
      assert(FPT->getNumParams() == NumPlacementArgs + 1 ||
             (FPT->getNumParams() == 2 && NumPlacementArgs == 0));

      CallArgList DeleteArgs;

      // The first argument is always a void*.
      FunctionProtoType::param_type_iterator AI = FPT->param_type_begin();
      DeleteArgs.add(Ptr.restore(CGF), *AI++);

      // A member 'operator delete' can take an extra 'size_t' argument.
      if (FPT->getNumParams() == NumPlacementArgs + 2) {
        RValue RV = AllocSize.restore(CGF);
        DeleteArgs.add(RV, *AI++);
      }

      // Pass the rest of the arguments, which must match exactly.
      for (unsigned I = 0; I != NumPlacementArgs; ++I) {
        RValue RV = getPlacementArgs()[I].restore(CGF);
        DeleteArgs.add(RV, *AI++);
      }

      // Call 'operator delete'.
      EmitNewDeleteCall(CGF, OperatorDelete, FPT, DeleteArgs);
    }
  };
}

/// Enter a cleanup to call 'operator delete' if the initializer in a
/// new-expression throws.
static void EnterNewDeleteCleanup(CodeGenFunction &CGF,
                                  const CXXNewExpr *E,
                                  llvm::Value *NewPtr,
                                  llvm::Value *AllocSize,
                                  const CallArgList &NewArgs) {
  // If we're not inside a conditional branch, then the cleanup will
  // dominate and we can do the easier (and more efficient) thing.
  if (!CGF.isInConditionalBranch()) {
    CallDeleteDuringNew *Cleanup = CGF.EHStack
      .pushCleanupWithExtra<CallDeleteDuringNew>(EHCleanup,
                                                 E->getNumPlacementArgs(),
                                                 E->getOperatorDelete(),
                                                 NewPtr, AllocSize);
    for (unsigned I = 0, N = E->getNumPlacementArgs(); I != N; ++I)
      Cleanup->setPlacementArg(I, NewArgs[I+1].RV);

    return;
  }

  // Otherwise, we need to save all this stuff.
  DominatingValue<RValue>::saved_type SavedNewPtr =
    DominatingValue<RValue>::save(CGF, RValue::get(NewPtr));
  DominatingValue<RValue>::saved_type SavedAllocSize =
    DominatingValue<RValue>::save(CGF, RValue::get(AllocSize));

  CallDeleteDuringConditionalNew *Cleanup = CGF.EHStack
    .pushCleanupWithExtra<CallDeleteDuringConditionalNew>(EHCleanup,
                                                 E->getNumPlacementArgs(),
                                                 E->getOperatorDelete(),
                                                 SavedNewPtr,
                                                 SavedAllocSize);
  for (unsigned I = 0, N = E->getNumPlacementArgs(); I != N; ++I)
    Cleanup->setPlacementArg(I,
                     DominatingValue<RValue>::save(CGF, NewArgs[I+1].RV));

  CGF.initFullExprCleanup();
}

llvm::Value *CodeGenFunction::EmitCXXNewExpr(const CXXNewExpr *E) {
  // The element type being allocated.
  QualType allocType = getContext().getBaseElementType(E->getAllocatedType());

  // 1. Build a call to the allocation function.
  FunctionDecl *allocator = E->getOperatorNew();
  const FunctionProtoType *allocatorType =
    allocator->getType()->castAs<FunctionProtoType>();

  CallArgList allocatorArgs;

  // The allocation size is the first argument.
  QualType sizeType = getContext().getSizeType();

  // If there is a brace-initializer, cannot allocate fewer elements than inits.
  unsigned minElements = 0;
  if (E->isArray() && E->hasInitializer()) {
    if (const InitListExpr *ILE = dyn_cast<InitListExpr>(E->getInitializer()))
      minElements = ILE->getNumInits();
  }

  llvm::Value *numElements = nullptr;
  llvm::Value *allocSizeWithoutCookie = nullptr;
  llvm::Value *allocSize =
    EmitCXXNewAllocSize(*this, E, minElements, numElements,
                        allocSizeWithoutCookie);
  
  allocatorArgs.add(RValue::get(allocSize), sizeType);

  // We start at 1 here because the first argument (the allocation size)
  // has already been emitted.
  EmitCallArgs(allocatorArgs, allocatorType->isVariadic(),
               allocatorType->param_type_begin() + 1,
               allocatorType->param_type_end(), E->placement_arg_begin(),
               E->placement_arg_end());

  // Emit the allocation call.  If the allocator is a global placement
  // operator, just "inline" it directly.
  RValue RV;
  if (allocator->isReservedGlobalPlacementOperator()) {
    assert(allocatorArgs.size() == 2);
    RV = allocatorArgs[1].RV;
    // TODO: kill any unnecessary computations done for the size
    // argument.
  } else {
    RV = EmitNewDeleteCall(*this, allocator, allocatorType, allocatorArgs);
  }

  // Emit a null check on the allocation result if the allocation
  // function is allowed to return null (because it has a non-throwing
  // exception spec; for this part, we inline
  // CXXNewExpr::shouldNullCheckAllocation()) and we have an
  // interesting initializer.
  bool nullCheck = allocatorType->isNothrow(getContext()) &&
    (!allocType.isPODType(getContext()) || E->hasInitializer());

  llvm::BasicBlock *nullCheckBB = nullptr;
  llvm::BasicBlock *contBB = nullptr;

  llvm::Value *allocation = RV.getScalarVal();
  unsigned AS = allocation->getType()->getPointerAddressSpace();

  // The null-check means that the initializer is conditionally
  // evaluated.
  ConditionalEvaluation conditional(*this);

  if (nullCheck) {
    conditional.begin(*this);

    nullCheckBB = Builder.GetInsertBlock();
    llvm::BasicBlock *notNullBB = createBasicBlock("new.notnull");
    contBB = createBasicBlock("new.cont");

    llvm::Value *isNull = Builder.CreateIsNull(allocation, "new.isnull");
    Builder.CreateCondBr(isNull, contBB, notNullBB);
    EmitBlock(notNullBB);
  }

  // If there's an operator delete, enter a cleanup to call it if an
  // exception is thrown.
  EHScopeStack::stable_iterator operatorDeleteCleanup;
  llvm::Instruction *cleanupDominator = nullptr;
  if (E->getOperatorDelete() &&
      !E->getOperatorDelete()->isReservedGlobalPlacementOperator()) {
    EnterNewDeleteCleanup(*this, E, allocation, allocSize, allocatorArgs);
    operatorDeleteCleanup = EHStack.stable_begin();
    cleanupDominator = Builder.CreateUnreachable();
  }

  assert((allocSize == allocSizeWithoutCookie) ==
         CalculateCookiePadding(*this, E).isZero());
  if (allocSize != allocSizeWithoutCookie) {
    assert(E->isArray());
    allocation = CGM.getCXXABI().InitializeArrayCookie(*this, allocation,
                                                       numElements,
                                                       E, allocType);
  }

  llvm::Type *elementPtrTy
    = ConvertTypeForMem(allocType)->getPointerTo(AS);
  llvm::Value *result = Builder.CreateBitCast(allocation, elementPtrTy);

  EmitNewInitializer(*this, E, allocType, result, numElements,
                     allocSizeWithoutCookie);
  if (E->isArray()) {
    // NewPtr is a pointer to the base element type.  If we're
    // allocating an array of arrays, we'll need to cast back to the
    // array pointer type.
    llvm::Type *resultType = ConvertTypeForMem(E->getType());
    if (result->getType() != resultType)
      result = Builder.CreateBitCast(result, resultType);
  }

  // Deactivate the 'operator delete' cleanup if we finished
  // initialization.
  if (operatorDeleteCleanup.isValid()) {
    DeactivateCleanupBlock(operatorDeleteCleanup, cleanupDominator);
    cleanupDominator->eraseFromParent();
  }

  if (nullCheck) {
    conditional.end(*this);

    llvm::BasicBlock *notNullBB = Builder.GetInsertBlock();
    EmitBlock(contBB);

    llvm::PHINode *PHI = Builder.CreatePHI(result->getType(), 2);
    PHI->addIncoming(result, notNullBB);
    PHI->addIncoming(llvm::Constant::getNullValue(result->getType()),
                     nullCheckBB);

    result = PHI;
  }
  
  return result;
}

void CodeGenFunction::EmitDeleteCall(const FunctionDecl *DeleteFD,
                                     llvm::Value *Ptr,
                                     QualType DeleteTy) {
  assert(DeleteFD->getOverloadedOperator() == OO_Delete);

  const FunctionProtoType *DeleteFTy =
    DeleteFD->getType()->getAs<FunctionProtoType>();

  CallArgList DeleteArgs;

  // Check if we need to pass the size to the delete operator.
  llvm::Value *Size = nullptr;
  QualType SizeTy;
  if (DeleteFTy->getNumParams() == 2) {
    SizeTy = DeleteFTy->getParamType(1);
    CharUnits DeleteTypeSize = getContext().getTypeSizeInChars(DeleteTy);
    Size = llvm::ConstantInt::get(ConvertType(SizeTy), 
                                  DeleteTypeSize.getQuantity());
  }

  QualType ArgTy = DeleteFTy->getParamType(0);
  llvm::Value *DeletePtr = Builder.CreateBitCast(Ptr, ConvertType(ArgTy));
  DeleteArgs.add(RValue::get(DeletePtr), ArgTy);

  if (Size)
    DeleteArgs.add(RValue::get(Size), SizeTy);

  // Emit the call to delete.
  EmitNewDeleteCall(*this, DeleteFD, DeleteFTy, DeleteArgs);
}

namespace {
  /// Calls the given 'operator delete' on a single object.
  struct CallObjectDelete : EHScopeStack::Cleanup {
    llvm::Value *Ptr;
    const FunctionDecl *OperatorDelete;
    QualType ElementType;

    CallObjectDelete(llvm::Value *Ptr,
                     const FunctionDecl *OperatorDelete,
                     QualType ElementType)
      : Ptr(Ptr), OperatorDelete(OperatorDelete), ElementType(ElementType) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      CGF.EmitDeleteCall(OperatorDelete, Ptr, ElementType);
    }
  };
}

/// Emit the code for deleting a single object.
static void EmitObjectDelete(CodeGenFunction &CGF,
                             const FunctionDecl *OperatorDelete,
                             llvm::Value *Ptr,
                             QualType ElementType,
                             bool UseGlobalDelete) {
  // Find the destructor for the type, if applicable.  If the
  // destructor is virtual, we'll just emit the vcall and return.
  const CXXDestructorDecl *Dtor = nullptr;
  if (const RecordType *RT = ElementType->getAs<RecordType>()) {
    CXXRecordDecl *RD = cast<CXXRecordDecl>(RT->getDecl());
    if (RD->hasDefinition() && !RD->hasTrivialDestructor()) {
      Dtor = RD->getDestructor();

      if (Dtor->isVirtual()) {
        if (UseGlobalDelete) {
          // If we're supposed to call the global delete, make sure we do so
          // even if the destructor throws.

          // Derive the complete-object pointer, which is what we need
          // to pass to the deallocation function.
          llvm::Value *completePtr =
            CGF.CGM.getCXXABI().adjustToCompleteObject(CGF, Ptr, ElementType);

          CGF.EHStack.pushCleanup<CallObjectDelete>(NormalAndEHCleanup,
                                                    completePtr, OperatorDelete,
                                                    ElementType);
        }

        // FIXME: Provide a source location here.
        CXXDtorType DtorType = UseGlobalDelete ? Dtor_Complete : Dtor_Deleting;
        CGF.CGM.getCXXABI().EmitVirtualDestructorCall(CGF, Dtor, DtorType,
                                                      SourceLocation(), Ptr);

        if (UseGlobalDelete) {
          CGF.PopCleanupBlock();
        }
        
        return;
      }
    }
  }

  // Make sure that we call delete even if the dtor throws.
  // This doesn't have to a conditional cleanup because we're going
  // to pop it off in a second.
  CGF.EHStack.pushCleanup<CallObjectDelete>(NormalAndEHCleanup,
                                            Ptr, OperatorDelete, ElementType);

  if (Dtor)
    CGF.EmitCXXDestructorCall(Dtor, Dtor_Complete,
                              /*ForVirtualBase=*/false,
                              /*Delegating=*/false,
                              Ptr);
  else if (CGF.getLangOpts().ObjCAutoRefCount &&
           ElementType->isObjCLifetimeType()) {
    switch (ElementType.getObjCLifetime()) {
    case Qualifiers::OCL_None:
    case Qualifiers::OCL_ExplicitNone:
    case Qualifiers::OCL_Autoreleasing:
      break;

    case Qualifiers::OCL_Strong: {
      // Load the pointer value.
      llvm::Value *PtrValue = CGF.Builder.CreateLoad(Ptr, 
                                             ElementType.isVolatileQualified());
        
      CGF.EmitARCRelease(PtrValue, ARCPreciseLifetime);
      break;
    }
        
    case Qualifiers::OCL_Weak:
      CGF.EmitARCDestroyWeak(Ptr);
      break;
    }
  }
           
  CGF.PopCleanupBlock();
}

namespace {
  /// Calls the given 'operator delete' on an array of objects.
  struct CallArrayDelete : EHScopeStack::Cleanup {
    llvm::Value *Ptr;
    const FunctionDecl *OperatorDelete;
    llvm::Value *NumElements;
    QualType ElementType;
    CharUnits CookieSize;

    CallArrayDelete(llvm::Value *Ptr,
                    const FunctionDecl *OperatorDelete,
                    llvm::Value *NumElements,
                    QualType ElementType,
                    CharUnits CookieSize)
      : Ptr(Ptr), OperatorDelete(OperatorDelete), NumElements(NumElements),
        ElementType(ElementType), CookieSize(CookieSize) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      const FunctionProtoType *DeleteFTy =
        OperatorDelete->getType()->getAs<FunctionProtoType>();
      assert(DeleteFTy->getNumParams() == 1 || DeleteFTy->getNumParams() == 2);

      CallArgList Args;
      
      // Pass the pointer as the first argument.
      QualType VoidPtrTy = DeleteFTy->getParamType(0);
      llvm::Value *DeletePtr
        = CGF.Builder.CreateBitCast(Ptr, CGF.ConvertType(VoidPtrTy));
      Args.add(RValue::get(DeletePtr), VoidPtrTy);

      // Pass the original requested size as the second argument.
      if (DeleteFTy->getNumParams() == 2) {
        QualType size_t = DeleteFTy->getParamType(1);
        llvm::IntegerType *SizeTy
          = cast<llvm::IntegerType>(CGF.ConvertType(size_t));
        
        CharUnits ElementTypeSize =
          CGF.CGM.getContext().getTypeSizeInChars(ElementType);

        // The size of an element, multiplied by the number of elements.
        llvm::Value *Size
          = llvm::ConstantInt::get(SizeTy, ElementTypeSize.getQuantity());
        Size = CGF.Builder.CreateMul(Size, NumElements);

        // Plus the size of the cookie if applicable.
        if (!CookieSize.isZero()) {
          llvm::Value *CookieSizeV
            = llvm::ConstantInt::get(SizeTy, CookieSize.getQuantity());
          Size = CGF.Builder.CreateAdd(Size, CookieSizeV);
        }

        Args.add(RValue::get(Size), size_t);
      }

      // Emit the call to delete.
      EmitNewDeleteCall(CGF, OperatorDelete, DeleteFTy, Args);
    }
  };
}

/// Emit the code for deleting an array of objects.
static void EmitArrayDelete(CodeGenFunction &CGF,
                            const CXXDeleteExpr *E,
                            llvm::Value *deletedPtr,
                            QualType elementType) {
  llvm::Value *numElements = nullptr;
  llvm::Value *allocatedPtr = nullptr;
  CharUnits cookieSize;
  CGF.CGM.getCXXABI().ReadArrayCookie(CGF, deletedPtr, E, elementType,
                                      numElements, allocatedPtr, cookieSize);

  assert(allocatedPtr && "ReadArrayCookie didn't set allocated pointer");

  // Make sure that we call delete even if one of the dtors throws.
  const FunctionDecl *operatorDelete = E->getOperatorDelete();
  CGF.EHStack.pushCleanup<CallArrayDelete>(NormalAndEHCleanup,
                                           allocatedPtr, operatorDelete,
                                           numElements, elementType,
                                           cookieSize);

  // Destroy the elements.
  if (QualType::DestructionKind dtorKind = elementType.isDestructedType()) {
    assert(numElements && "no element count for a type with a destructor!");

    llvm::Value *arrayEnd =
      CGF.Builder.CreateInBoundsGEP(deletedPtr, numElements, "delete.end");

    // Note that it is legal to allocate a zero-length array, and we
    // can never fold the check away because the length should always
    // come from a cookie.
    CGF.emitArrayDestroy(deletedPtr, arrayEnd, elementType,
                         CGF.getDestroyer(dtorKind),
                         /*checkZeroLength*/ true,
                         CGF.needsEHCleanup(dtorKind));
  }

  // Pop the cleanup block.
  CGF.PopCleanupBlock();
}

void CodeGenFunction::EmitCXXDeleteExpr(const CXXDeleteExpr *E) {
  const Expr *Arg = E->getArgument();
  llvm::Value *Ptr = EmitScalarExpr(Arg);

  // Null check the pointer.
  llvm::BasicBlock *DeleteNotNull = createBasicBlock("delete.notnull");
  llvm::BasicBlock *DeleteEnd = createBasicBlock("delete.end");

  llvm::Value *IsNull = Builder.CreateIsNull(Ptr, "isnull");

  Builder.CreateCondBr(IsNull, DeleteEnd, DeleteNotNull);
  EmitBlock(DeleteNotNull);

  // We might be deleting a pointer to array.  If so, GEP down to the
  // first non-array element.
  // (this assumes that A(*)[3][7] is converted to [3 x [7 x %A]]*)
  QualType DeleteTy = Arg->getType()->getAs<PointerType>()->getPointeeType();
  if (DeleteTy->isConstantArrayType()) {
    llvm::Value *Zero = Builder.getInt32(0);
    SmallVector<llvm::Value*,8> GEP;

    GEP.push_back(Zero); // point at the outermost array

    // For each layer of array type we're pointing at:
    while (const ConstantArrayType *Arr
             = getContext().getAsConstantArrayType(DeleteTy)) {
      // 1. Unpeel the array type.
      DeleteTy = Arr->getElementType();

      // 2. GEP to the first element of the array.
      GEP.push_back(Zero);
    }

    Ptr = Builder.CreateInBoundsGEP(Ptr, GEP, "del.first");
  }

  assert(ConvertTypeForMem(DeleteTy) ==
         cast<llvm::PointerType>(Ptr->getType())->getElementType());

  if (E->isArrayForm()) {
    EmitArrayDelete(*this, E, Ptr, DeleteTy);
  } else {
    EmitObjectDelete(*this, E->getOperatorDelete(), Ptr, DeleteTy,
                     E->isGlobalDelete());
  }

  EmitBlock(DeleteEnd);
}

static llvm::Constant *getBadTypeidFn(CodeGenFunction &CGF) {
  // void __cxa_bad_typeid();
  llvm::FunctionType *FTy = llvm::FunctionType::get(CGF.VoidTy, false);
  
  return CGF.CGM.CreateRuntimeFunction(FTy, "__cxa_bad_typeid");
}

static void EmitBadTypeidCall(CodeGenFunction &CGF) {
  llvm::Value *Fn = getBadTypeidFn(CGF);
  CGF.EmitRuntimeCallOrInvoke(Fn).setDoesNotReturn();
  CGF.Builder.CreateUnreachable();
}

/// \brief Gets the offset to the virtual base that contains the vfptr for
/// MS-ABI polymorphic types.
static llvm::Value *getPolymorphicOffset(CodeGenFunction &CGF,
                                         const CXXRecordDecl *RD,
                                         llvm::Value *Value) {
  const ASTContext &Context = RD->getASTContext();
  for (const CXXBaseSpecifier &Base : RD->vbases())
    if (Context.getASTRecordLayout(Base.getType()->getAsCXXRecordDecl())
            .hasExtendableVFPtr())
      return CGF.CGM.getCXXABI().GetVirtualBaseClassOffset(
          CGF, Value, RD, Base.getType()->getAsCXXRecordDecl());
  llvm_unreachable("One of our vbases should be polymorphic.");
}

static llvm::Value *EmitTypeidFromVTable(CodeGenFunction &CGF,
                                         const Expr *E, 
                                         llvm::Type *StdTypeInfoPtrTy) {
  // Get the vtable pointer.
  llvm::Value *ThisPtr = CGF.EmitLValue(E).getAddress();

  // C++ [expr.typeid]p2:
  //   If the glvalue expression is obtained by applying the unary * operator to
  //   a pointer and the pointer is a null pointer value, the typeid expression
  //   throws the std::bad_typeid exception.
  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E->IgnoreParens())) {
    if (UO->getOpcode() == UO_Deref) {
      llvm::BasicBlock *BadTypeidBlock = 
        CGF.createBasicBlock("typeid.bad_typeid");
      llvm::BasicBlock *EndBlock =
        CGF.createBasicBlock("typeid.end");

      llvm::Value *IsNull = CGF.Builder.CreateIsNull(ThisPtr);
      CGF.Builder.CreateCondBr(IsNull, BadTypeidBlock, EndBlock);

      CGF.EmitBlock(BadTypeidBlock);
      EmitBadTypeidCall(CGF);
      CGF.EmitBlock(EndBlock);
    }
  }

  llvm::Value *Value = CGF.GetVTablePtr(ThisPtr, 
                                        StdTypeInfoPtrTy->getPointerTo());

  // Load the type info.
  Value = CGF.Builder.CreateConstInBoundsGEP1_64(Value, -1ULL);
  return CGF.Builder.CreateLoad(Value);
}

llvm::Value *CodeGenFunction::EmitCXXTypeidExpr(const CXXTypeidExpr *E) {
  llvm::Type *StdTypeInfoPtrTy = 
    ConvertType(E->getType())->getPointerTo();
  
  if (E->isTypeOperand()) {
    llvm::Constant *TypeInfo =
        CGM.GetAddrOfRTTIDescriptor(E->getTypeOperand(getContext()));
    return Builder.CreateBitCast(TypeInfo, StdTypeInfoPtrTy);
  }

  // C++ [expr.typeid]p2:
  //   When typeid is applied to a glvalue expression whose type is a
  //   polymorphic class type, the result refers to a std::type_info object
  //   representing the type of the most derived object (that is, the dynamic
  //   type) to which the glvalue refers.
  if (E->isPotentiallyEvaluated())
    return EmitTypeidFromVTable(*this, E->getExprOperand(), 
                                StdTypeInfoPtrTy);

  QualType OperandTy = E->getExprOperand()->getType();
  return Builder.CreateBitCast(CGM.GetAddrOfRTTIDescriptor(OperandTy),
                               StdTypeInfoPtrTy);
}

static llvm::Constant *getItaniumDynamicCastFn(CodeGenFunction &CGF) {
  // void *__dynamic_cast(const void *sub,
  //                      const abi::__class_type_info *src,
  //                      const abi::__class_type_info *dst,
  //                      std::ptrdiff_t src2dst_offset);
  
  llvm::Type *Int8PtrTy = CGF.Int8PtrTy;
  llvm::Type *PtrDiffTy = 
    CGF.ConvertType(CGF.getContext().getPointerDiffType());

  llvm::Type *Args[4] = { Int8PtrTy, Int8PtrTy, Int8PtrTy, PtrDiffTy };

  llvm::FunctionType *FTy = llvm::FunctionType::get(Int8PtrTy, Args, false);

  // Mark the function as nounwind readonly.
  llvm::Attribute::AttrKind FuncAttrs[] = { llvm::Attribute::NoUnwind,
                                            llvm::Attribute::ReadOnly };
  llvm::AttributeSet Attrs = llvm::AttributeSet::get(
      CGF.getLLVMContext(), llvm::AttributeSet::FunctionIndex, FuncAttrs);

  return CGF.CGM.CreateRuntimeFunction(FTy, "__dynamic_cast", Attrs);
}

static llvm::Constant *getBadCastFn(CodeGenFunction &CGF) {
  // void __cxa_bad_cast();
  llvm::FunctionType *FTy = llvm::FunctionType::get(CGF.VoidTy, false);
  return CGF.CGM.CreateRuntimeFunction(FTy, "__cxa_bad_cast");
}

static void EmitBadCastCall(CodeGenFunction &CGF) {
  llvm::Value *Fn = getBadCastFn(CGF);
  CGF.EmitRuntimeCallOrInvoke(Fn).setDoesNotReturn();
  CGF.Builder.CreateUnreachable();
}

/// \brief Compute the src2dst_offset hint as described in the
/// Itanium C++ ABI [2.9.7]
static CharUnits computeOffsetHint(ASTContext &Context,
                                   const CXXRecordDecl *Src,
                                   const CXXRecordDecl *Dst) {
  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/false);

  // If Dst is not derived from Src we can skip the whole computation below and
  // return that Src is not a public base of Dst.  Record all inheritance paths.
  if (!Dst->isDerivedFrom(Src, Paths))
    return CharUnits::fromQuantity(-2ULL);

  unsigned NumPublicPaths = 0;
  CharUnits Offset;

  // Now walk all possible inheritance paths.
  for (CXXBasePaths::paths_iterator I = Paths.begin(), E = Paths.end();
       I != E; ++I) {
    if (I->Access != AS_public) // Ignore non-public inheritance.
      continue;

    ++NumPublicPaths;

    for (CXXBasePath::iterator J = I->begin(), JE = I->end(); J != JE; ++J) {
      // If the path contains a virtual base class we can't give any hint.
      // -1: no hint.
      if (J->Base->isVirtual())
        return CharUnits::fromQuantity(-1ULL);

      if (NumPublicPaths > 1) // Won't use offsets, skip computation.
        continue;

      // Accumulate the base class offsets.
      const ASTRecordLayout &L = Context.getASTRecordLayout(J->Class);
      Offset += L.getBaseClassOffset(J->Base->getType()->getAsCXXRecordDecl());
    }
  }

  // -2: Src is not a public base of Dst.
  if (NumPublicPaths == 0)
    return CharUnits::fromQuantity(-2ULL);

  // -3: Src is a multiple public base type but never a virtual base type.
  if (NumPublicPaths > 1)
    return CharUnits::fromQuantity(-3ULL);

  // Otherwise, the Src type is a unique public nonvirtual base type of Dst.
  // Return the offset of Src from the origin of Dst.
  return Offset;
}

static llvm::Value *
EmitItaniumDynamicCastCall(CodeGenFunction &CGF, llvm::Value *Value,
                    QualType SrcTy, QualType DestTy,
                    llvm::BasicBlock *CastEnd) {
  llvm::Type *PtrDiffLTy = 
    CGF.ConvertType(CGF.getContext().getPointerDiffType());
  llvm::Type *DestLTy = CGF.ConvertType(DestTy);

  if (const PointerType *PTy = DestTy->getAs<PointerType>()) {
    if (PTy->getPointeeType()->isVoidType()) {
      // C++ [expr.dynamic.cast]p7:
      //   If T is "pointer to cv void," then the result is a pointer to the
      //   most derived object pointed to by v.

      // Get the vtable pointer.
      llvm::Value *VTable = CGF.GetVTablePtr(Value, PtrDiffLTy->getPointerTo());

      // Get the offset-to-top from the vtable.
      llvm::Value *OffsetToTop = 
        CGF.Builder.CreateConstInBoundsGEP1_64(VTable, -2ULL);
      OffsetToTop = CGF.Builder.CreateLoad(OffsetToTop, "offset.to.top");

      // Finally, add the offset to the pointer.
      Value = CGF.EmitCastToVoidPtr(Value);
      Value = CGF.Builder.CreateInBoundsGEP(Value, OffsetToTop);

      return CGF.Builder.CreateBitCast(Value, DestLTy);
    }
  }

  QualType SrcRecordTy;
  QualType DestRecordTy;
  
  if (const PointerType *DestPTy = DestTy->getAs<PointerType>()) {
    SrcRecordTy = SrcTy->castAs<PointerType>()->getPointeeType();
    DestRecordTy = DestPTy->getPointeeType();
  } else {
    SrcRecordTy = SrcTy;
    DestRecordTy = DestTy->castAs<ReferenceType>()->getPointeeType();
  }

  assert(SrcRecordTy->isRecordType() && "source type must be a record type!");
  assert(DestRecordTy->isRecordType() && "dest type must be a record type!");

  llvm::Value *SrcRTTI =
    CGF.CGM.GetAddrOfRTTIDescriptor(SrcRecordTy.getUnqualifiedType());
  llvm::Value *DestRTTI =
    CGF.CGM.GetAddrOfRTTIDescriptor(DestRecordTy.getUnqualifiedType());

  // Compute the offset hint.
  const CXXRecordDecl *SrcDecl = SrcRecordTy->getAsCXXRecordDecl();
  const CXXRecordDecl *DestDecl = DestRecordTy->getAsCXXRecordDecl();
  llvm::Value *OffsetHint =
    llvm::ConstantInt::get(PtrDiffLTy,
                           computeOffsetHint(CGF.getContext(), SrcDecl,
                                             DestDecl).getQuantity());

  // Emit the call to __dynamic_cast.
  Value = CGF.EmitCastToVoidPtr(Value);

  llvm::Value *args[] = { Value, SrcRTTI, DestRTTI, OffsetHint };
  Value = CGF.EmitNounwindRuntimeCall(getItaniumDynamicCastFn(CGF), args);
  Value = CGF.Builder.CreateBitCast(Value, DestLTy);

  /// C++ [expr.dynamic.cast]p9:
  ///   A failed cast to reference type throws std::bad_cast
  if (DestTy->isReferenceType()) {
    llvm::BasicBlock *BadCastBlock = 
      CGF.createBasicBlock("dynamic_cast.bad_cast");

    llvm::Value *IsNull = CGF.Builder.CreateIsNull(Value);
    CGF.Builder.CreateCondBr(IsNull, BadCastBlock, CastEnd);

    CGF.EmitBlock(BadCastBlock);
    EmitBadCastCall(CGF);
  }

  return Value;
}

static llvm::Value *EmitDynamicCastToNull(CodeGenFunction &CGF,
                                          QualType DestTy) {
  llvm::Type *DestLTy = CGF.ConvertType(DestTy);
  if (DestTy->isPointerType())
    return llvm::Constant::getNullValue(DestLTy);

  /// C++ [expr.dynamic.cast]p9:
  ///   A failed cast to reference type throws std::bad_cast
  EmitBadCastCall(CGF);

  CGF.EmitBlock(CGF.createBasicBlock("dynamic_cast.end"));
  return llvm::UndefValue::get(DestLTy);
}

namespace {
struct MSDynamicCastBuilder {
  MSDynamicCastBuilder(CodeGenFunction &CGF, const CXXDynamicCastExpr *DCE);
  llvm::Value *emitDynamicCastCall(llvm::Value *Value);
  llvm::Value *emitDynamicCast(llvm::Value *Value);

  CodeGenFunction &CGF;
  CGBuilderTy &Builder;
  llvm::PointerType *Int8PtrTy;
  QualType SrcTy, DstTy;
  const CXXRecordDecl *SrcDecl;
  bool IsPtrCast, IsCastToVoid, IsCastOfNull;
};
} // namespace

MSDynamicCastBuilder::MSDynamicCastBuilder(CodeGenFunction &CGF,
                                           const CXXDynamicCastExpr *DCE)
    : CGF(CGF), Builder(CGF.Builder), Int8PtrTy(CGF.Int8PtrTy),
      SrcDecl(nullptr) {
  DstTy = DCE->getTypeAsWritten();
  IsPtrCast = DstTy->isPointerType();
  // Get the PointeeTypes.  After this point the original types are not used.
  DstTy = IsPtrCast ? DstTy->castAs<PointerType>()->getPointeeType()
                    : DstTy->castAs<ReferenceType>()->getPointeeType();
  IsCastToVoid = DstTy->isVoidType();
  IsCastOfNull = DCE->isAlwaysNull();
  if (IsCastOfNull)
    return;
  SrcTy = DCE->getSubExpr()->getType();
  SrcTy = IsPtrCast ? SrcTy->castAs<PointerType>()->getPointeeType() : SrcTy;
  SrcDecl = SrcTy->getAsCXXRecordDecl();
  // If we don't need a base adjustment, we don't need a SrcDecl so clear it
  // here.  Later we use the existance of the SrcDecl to determine the need for
  // a base adjustment.
  if (CGF.getContext().getASTRecordLayout(SrcDecl).hasExtendableVFPtr())
    SrcDecl = nullptr;
}

llvm::Value *MSDynamicCastBuilder::emitDynamicCastCall(llvm::Value *Value) {
  llvm::IntegerType *Int32Ty = CGF.Int32Ty;
  llvm::Value *Offset = llvm::ConstantInt::get(Int32Ty, 0);
  Value = Builder.CreateBitCast(Value, Int8PtrTy);
  // If we need to perform a base adjustment, do it here.
  if (SrcDecl) {
    Offset = getPolymorphicOffset(CGF, SrcDecl, Value);
    Value = Builder.CreateInBoundsGEP(Value, Offset);
    Offset = Builder.CreateTrunc(Offset, Int32Ty);
  }
  if (IsCastToVoid) {
    // PVOID __RTCastToVoid(
    //   PVOID inptr)
    llvm::Type *ArgTypes[] = {Int8PtrTy};
    llvm::Constant *Function = CGF.CGM.CreateRuntimeFunction(
        llvm::FunctionType::get(Int8PtrTy, ArgTypes, false), "__RTCastToVoid");
    llvm::Value *Args[] = {Value};
    return CGF.EmitRuntimeCall(Function, Args);
  }
  // PVOID __RTDynamicCast(
  //   PVOID inptr,
  //   LONG VfDelta,
  //   PVOID SrcType,
  //   PVOID TargetType,
  //   BOOL isReference)
  llvm::Type *ArgTypes[] = {Int8PtrTy, Int32Ty, Int8PtrTy, Int8PtrTy, Int32Ty};
  llvm::Constant *Function = CGF.CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(Int8PtrTy, ArgTypes, false), "__RTDynamicCast");
  llvm::Value *Args[] = {
    Value, Offset,
      CGF.CGM.GetAddrOfRTTIDescriptor(SrcTy.getUnqualifiedType()),
      CGF.CGM.GetAddrOfRTTIDescriptor(DstTy.getUnqualifiedType()),
      llvm::ConstantInt::get(Int32Ty, IsPtrCast ? 0 : 1)};
  return CGF.EmitRuntimeCall(Function, Args);
}

llvm::Value *MSDynamicCastBuilder::emitDynamicCast(llvm::Value *Value) {
  // Note about undefined behavior: If the dynamic cast is casting to a
  // reference type and the input is null, we hit a grey area in the standard.
  // Here we're interpreting the behavior as undefined.  The effects are the
  // following:  If the compiler determines that the argument is statically null
  // or if the argument is dynamically null but does not require base
  // adjustment, __RTDynamicCast will be called with a null argument and the
  // isreference bit set.  In this case __RTDynamicCast will throw
  // std::bad_cast. If the argument is dynamically null and a base adjustment is
  // required the resulting code will produce an out of bounds memory reference
  // when trying to read VBTblPtr.  In Itanium mode clang also emits a vtable
  // load that fails at run time.
  llvm::PointerType *DstLTy = CGF.ConvertType(DstTy)->getPointerTo();
  if (IsCastOfNull && IsPtrCast)
    return Builder.CreateBitCast(Value, DstLTy);
  if (IsCastOfNull || !IsPtrCast || !SrcDecl)
    return Builder.CreateBitCast(emitDynamicCastCall(Value), DstLTy);
  // !IsCastOfNull && IsPtrCast && SrcDecl
  // In this case we have a pointer that requires a base adjustment.  An
  // adjustment is only required if the pointer is actually valid so here we
  // perform a null check before doing the base adjustment and calling
  // __RTDynamicCast.  In the case that the argument is null we simply return
  // null without calling __RTDynamicCast.
  llvm::BasicBlock *EntryBlock = Builder.GetInsertBlock();
  llvm::BasicBlock *CallBlock = CGF.createBasicBlock("dynamic_cast.valid");
  llvm::BasicBlock *ExitBlock = CGF.createBasicBlock("dynamic_cast.call");
  Builder.CreateCondBr(Builder.CreateIsNull(Value), ExitBlock, CallBlock);
  // Emit the call block and code for it.
  CGF.EmitBlock(CallBlock);
  Value = emitDynamicCastCall(Value);
  // Emit the call block and the phi nodes for it.
  CGF.EmitBlock(ExitBlock);
  llvm::PHINode *ValuePHI = Builder.CreatePHI(Int8PtrTy, 2);
  ValuePHI->addIncoming(Value, CallBlock);
  ValuePHI->addIncoming(llvm::Constant::getNullValue(Int8PtrTy), EntryBlock);
  return Builder.CreateBitCast(ValuePHI, DstLTy);
}

llvm::Value *CodeGenFunction::EmitDynamicCast(llvm::Value *Value,
                                              const CXXDynamicCastExpr *DCE) {
  if (getTarget().getCXXABI().isMicrosoft()) {
    MSDynamicCastBuilder Builder(*this, DCE);
    return Builder.emitDynamicCast(Value);
  }

  QualType DestTy = DCE->getTypeAsWritten();

  if (DCE->isAlwaysNull())
    return EmitDynamicCastToNull(*this, DestTy);

  QualType SrcTy = DCE->getSubExpr()->getType();

  // C++ [expr.dynamic.cast]p4: 
  //   If the value of v is a null pointer value in the pointer case, the result
  //   is the null pointer value of type T.
  bool ShouldNullCheckSrcValue = SrcTy->isPointerType();

  llvm::BasicBlock *CastNull = nullptr;
  llvm::BasicBlock *CastNotNull = nullptr;
  llvm::BasicBlock *CastEnd = createBasicBlock("dynamic_cast.end");
  
  if (ShouldNullCheckSrcValue) {
    CastNull = createBasicBlock("dynamic_cast.null");
    CastNotNull = createBasicBlock("dynamic_cast.notnull");

    llvm::Value *IsNull = Builder.CreateIsNull(Value);
    Builder.CreateCondBr(IsNull, CastNull, CastNotNull);
    EmitBlock(CastNotNull);
  }

  Value = EmitItaniumDynamicCastCall(*this, Value, SrcTy, DestTy, CastEnd);

  if (ShouldNullCheckSrcValue) {
    EmitBranch(CastEnd);

    EmitBlock(CastNull);
    EmitBranch(CastEnd);
  }

  EmitBlock(CastEnd);

  if (ShouldNullCheckSrcValue) {
    llvm::PHINode *PHI = Builder.CreatePHI(Value->getType(), 2);
    PHI->addIncoming(Value, CastNotNull);
    PHI->addIncoming(llvm::Constant::getNullValue(Value->getType()), CastNull);

    Value = PHI;
  }

  return Value;
}

void CodeGenFunction::EmitLambdaExpr(const LambdaExpr *E, AggValueSlot Slot) {
  RunCleanupsScope Scope(*this);
  LValue SlotLV = MakeAddrLValue(Slot.getAddr(), E->getType(),
                                 Slot.getAlignment());

  CXXRecordDecl::field_iterator CurField = E->getLambdaClass()->field_begin();
  for (LambdaExpr::capture_init_iterator i = E->capture_init_begin(),
                                         e = E->capture_init_end();
       i != e; ++i, ++CurField) {
    // Emit initialization
    
    LValue LV = EmitLValueForFieldInitialization(SlotLV, *CurField);
    ArrayRef<VarDecl *> ArrayIndexes;
    if (CurField->getType()->isArrayType())
      ArrayIndexes = E->getCaptureInitIndexVars(i);
    EmitInitializerForField(*CurField, LV, *i, ArrayIndexes);
  }
}