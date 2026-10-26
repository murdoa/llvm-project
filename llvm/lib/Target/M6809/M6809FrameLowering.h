//===-- M6809FrameLowering.h - Frame info for M6809 -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6809_M6809FRAMELOWERING_H
#define LLVM_LIB_TARGET_M6809_M6809FRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

class M6809Subtarget;

class M6809FrameLowering : public TargetFrameLowering {
  const M6809Subtarget &STI;

public:
  // Bytes the saved U takes, between S on entry and the frame U points at.
  static constexpr int FPSaveSize = 2;

  explicit M6809FrameLowering(const M6809Subtarget &STI)
      // The stack is never realigned, so stack objects get at most byte
      // alignment whatever the IR asks for.
      : TargetFrameLowering(StackGrowsDown, Align(1), /*LocalAreaOffset=*/0,
                            Align(1), /*StackRealignable=*/false),
        STI(STI) {}

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  bool assignCalleeSavedSpillSlots(
      MachineFunction &MF, const TargetRegisterInfo *TRI,
      std::vector<CalleeSavedInfo> &CSI) const override;
  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;
  bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                           RegScavenger *RS) const override;

  void orderFrameObjects(const MachineFunction &MF,
                         SmallVectorImpl<int> &Objects) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

protected:
  // S moves with variable-sized objects, so the frame is then addressed
  // off U instead.
  bool hasFPImpl(const MachineFunction &MF) const override {
    return MF.getFrameInfo().hasVarSizedObjects();
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_M6809FRAMELOWERING_H
