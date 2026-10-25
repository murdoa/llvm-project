//===-- M6800.h - Top-level interface for M6800 -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6800_M6800_H
#define LLVM_LIB_TARGET_M6800_M6800_H

#include "MCTargetDesc/M6800MCTargetDesc.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class FunctionPass;
class M6800TargetMachine;
class PassRegistry;

FunctionPass *createM6800ISelDag(M6800TargetMachine &TM,
                                 CodeGenOptLevel OptLevel);
FunctionPass *createM6800SplitDOperandsPass();
FunctionPass *createM6800PostRACleanupPass();
FunctionPass *createM6800FrameAccessPass();

void initializeM6800AsmPrinterPass(PassRegistry &);
void initializeM6800DAGToDAGISelLegacyPass(PassRegistry &);
void initializeM6800SplitDOperandsPass(PassRegistry &);
void initializeM6800PostRACleanupPass(PassRegistry &);
void initializeM6800FrameAccessPass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6800_M6800_H
