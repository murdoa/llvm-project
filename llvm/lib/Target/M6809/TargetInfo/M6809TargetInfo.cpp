//===-- M6809TargetInfo.cpp - M6809 Target Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/M6809TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

Target &llvm::getTheM6809Target() {
  static Target TheM6809Target;
  return TheM6809Target;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeM6809TargetInfo() {
  RegisterTarget<Triple::m6809> X(getTheM6809Target(), "m6809",
                                  "Motorola 6809 / Hitachi HD6309 "
                                  "[experimental]",
                                  "M6809");
}
