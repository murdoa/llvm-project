//===-- M6800MachineFunctionInfo.cpp - M6800 per-function info ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800MachineFunctionInfo.h"

using namespace llvm;

void M6800FunctionInfo::anchor() {}

MachineFunctionInfo *M6800FunctionInfo::clone(
    BumpPtrAllocator &Allocator, MachineFunction &DestMF,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
    const {
  return DestMF.cloneInfo<M6800FunctionInfo>(*this);
}
