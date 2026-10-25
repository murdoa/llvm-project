//===--- M6800.cpp - Implement M6800 target feature support ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Motorola 6809 / Hitachi HD6309 TargetInfo.
//
//===----------------------------------------------------------------------===//

#include "M6800.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

const char *const M6800TargetInfo::GCCRegNames[] = {
    "a", "b", "d", "x", "sp", "pc", "cc",
};

ArrayRef<const char *> M6800TargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

void M6800TargetInfo::getTargetDefines(const LangOptions &Opts,
                                       MacroBuilder &Builder) const {
  Builder.defineMacro("__m6800__");
  Builder.defineMacro("__M6800__");
  if (CPU == "6301") {
    Builder.defineMacro("__hd6301__");
    Builder.defineMacro("__HD6301__");
  }
}
