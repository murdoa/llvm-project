//===-- M6800TargetInfo.cpp - M6800 Target Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/M6800TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

Target &llvm::getTheM6800Target() {
  static Target TheM6800Target;
  return TheM6800Target;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeM6800TargetInfo() {
  RegisterTarget<Triple::m6800> X(getTheM6800Target(), "m6800",
                                  "Motorola 6800 / Hitachi HD6301 "
                                  "[experimental]",
                                  "M6800");
}
