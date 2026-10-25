//===--- M6809.cpp - Implement M6809 target feature support ---------------===//
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

#include "M6809.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

const char *const M6809TargetInfo::GCCRegNames[] = {
    "a", "b", "d", "x", "y", "u", "s", "pc", "cc", "dp",
};

ArrayRef<const char *> M6809TargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

void M6809TargetInfo::getTargetDefines(const LangOptions &Opts,
                                       MacroBuilder &Builder) const {
  Builder.defineMacro("__m6809__");
  Builder.defineMacro("__M6809__");
  if (CPU == "6309") {
    Builder.defineMacro("__hd6309__");
    Builder.defineMacro("__HD6309__");
  }
}
