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
// hidden pointer, as DefaultABIInfo does. Arguments are packed with no
// padding, so the variadic area is read one value after another.
//===----------------------------------------------------------------------===//

namespace {

class M6809ABIInfo : public DefaultABIInfo {
public:
  M6809ABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override {
    return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*IsIndirect=*/false,
                            getContext().getTypeInfoInChars(Ty),
                            CharUnits::One(), /*AllowHigherAlign=*/false,
                            Slot);
  }
};

class M6809TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  M6809TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<M6809ABIInfo>(CGT)) {}
};

} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createM6809TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<M6809TargetCodeGenInfo>(CGM.getTypes());
}
