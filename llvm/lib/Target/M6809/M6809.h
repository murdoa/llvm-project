//===-- M6809.h - Top-level interface for M6809 -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6809_M6809_H
#define LLVM_LIB_TARGET_M6809_M6809_H

#include "MCTargetDesc/M6809MCTargetDesc.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class FunctionPass;
class M6809TargetMachine;
class PassRegistry;

FunctionPass *createM6809ISelDag(M6809TargetMachine &TM,
                                 CodeGenOptLevel OptLevel);

void initializeM6809AsmPrinterPass(PassRegistry &);
void initializeM6809DAGToDAGISelLegacyPass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_M6809_H
