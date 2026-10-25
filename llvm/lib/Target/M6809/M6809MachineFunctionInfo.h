//===-- M6809MachineFunctionInfo.h - M6809 per-function info ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6809_M6809MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_M6809_M6809MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class M6809FunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  /// Fixed stack object at the first variadic argument, for va_start.
  int VarArgsFrameIndex = 0;

public:
  M6809FunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_M6809MACHINEFUNCTIONINFO_H
