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
      // The fixed frame is not realigned. A local with more than byte
      // alignment becomes a dynamic alloca, which DYNALLOC aligns.
      : TargetFrameLowering(StackGrowsDown, Align(1), /*LocalAreaOffset=*/0,
                            Align(1), /*StackRealignable=*/false),
        STI(STI) {}

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

protected:
  bool hasFPImpl(const MachineFunction &MF) const override;

private:
  void emitFree(MachineFunction &MF, MachineBasicBlock &MBB,
                MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                int64_t StackSize, bool XLive) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6800_M6800FRAMELOWERING_H
