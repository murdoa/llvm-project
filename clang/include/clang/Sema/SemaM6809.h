//===-- SemaM6809.h - M6809 and M6800 target-specific routines --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to the M6809 and
/// M6800 targets.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAM6809_H
#define LLVM_CLANG_SEMA_SEMAM6809_H

#include "clang/AST/ASTFwd.h"
#include "clang/Sema/SemaBase.h"

namespace clang {
class ParsedAttr;

class SemaM6809 : public SemaBase {
public:
  SemaM6809(Sema &S);

  void handleInterruptAttr(Decl *D, const ParsedAttr &AL);
};
} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAM6809_H
