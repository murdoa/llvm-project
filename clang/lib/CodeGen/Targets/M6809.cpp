//===- M6809.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// M6809 ABI Implementation
//
// Aggregates are passed by value in the argument area and returned through a
// hidden pointer, as DefaultABIInfo does, except that zero-sized ones (a GNU
// C extension) take no space. Arguments are packed with no padding, so the
// variadic area is read one value after another.
//===----------------------------------------------------------------------===//

namespace {

class M6809ABIInfo : public DefaultABIInfo {
public:
  M6809ABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  ABIArgInfo classifyArgumentType(QualType Ty) const {
    if (isAggregateTypeForABI(Ty) && getContext().getTypeSize(Ty) == 0 &&
        !getRecordArgABI(Ty, getCXXABI()))
      return ABIArgInfo::getIgnore();
    return DefaultABIInfo::classifyArgumentType(Ty);
  }

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override {
    return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*IsIndirect=*/false,
                            getContext().getTypeInfoInChars(Ty),
                            CharUnits::One(), /*AllowHigherAlign=*/false, Slot);
  }
};

class M6809TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  M6809TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<M6809ABIInfo>(CGT)) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    if (GV->isDeclaration())
      return;
    const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (FD && FD->hasAttr<M6809InterruptAttr>())
      cast<llvm::Function>(GV)->addFnAttr("interrupt");
  }
};

} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createM6809TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<M6809TargetCodeGenInfo>(CGM.getTypes());
}
