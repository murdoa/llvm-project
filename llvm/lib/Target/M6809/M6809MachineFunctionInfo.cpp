//===-- M6809MachineFunctionInfo.cpp - M6809 per-function info ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809MachineFunctionInfo.h"

using namespace llvm;

void M6809FunctionInfo::anchor() {}

MachineFunctionInfo *M6809FunctionInfo::clone(
    BumpPtrAllocator &Allocator, MachineFunction &DestMF,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
    const {
  return DestMF.cloneInfo<M6809FunctionInfo>(*this);
}
