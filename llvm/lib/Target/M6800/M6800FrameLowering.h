//===-- M6800FrameLowering.h - Frame info for M6800 -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6800_M6800FRAMELOWERING_H
#define LLVM_LIB_TARGET_M6800_M6800FRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

class M6800Subtarget;

class M6800FrameLowering : public TargetFrameLowering {
  const M6800Subtarget &STI;

public:
  explicit M6800FrameLowering(const M6800Subtarget &STI)
      // Stack objects keep their alignment within the frame, but SP is
      // never realigned (see M6800RegisterInfo::canRealignStack).
      : TargetFrameLowering(StackGrowsDown, Align(1), /*LocalAreaOffset=*/0,
                            Align(1), /*StackRealignable=*/true),
        STI(STI) {}

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

protected:
  bool hasFPImpl(const MachineFunction &MF) const override { return false; }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6800_M6800FRAMELOWERING_H
