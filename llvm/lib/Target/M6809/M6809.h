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
class ModulePass;
class M6809TargetMachine;
class PassRegistry;

FunctionPass *createM6809ISelDag(M6809TargetMachine &TM,
                                 CodeGenOptLevel OptLevel);
FunctionPass *createM6809SplitDOperandsPass();
FunctionPass *createM6809PostRACleanupPass();
ModulePass *createM6809DirectPagePass();

void initializeM6809AsmPrinterPass(PassRegistry &);
void initializeM6809DAGToDAGISelLegacyPass(PassRegistry &);
void initializeM6809SplitDOperandsPass(PassRegistry &);
void initializeM6809PostRACleanupPass(PassRegistry &);
void initializeM6809DirectPagePass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_M6809_H
