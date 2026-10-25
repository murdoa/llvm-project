//===-- M6809InstrInfo.h - M6809 Instruction Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6809_M6809INSTRINFO_H
#define LLVM_LIB_TARGET_M6809_M6809INSTRINFO_H

#include "M6809RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "M6809GenInstrInfo.inc"

namespace llvm {

class M6809Subtarget;

class M6809InstrInfo : public M6809GenInstrInfo {
  const M6809RegisterInfo RI;
  const M6809Subtarget &STI;
  virtual void anchor();

  bool expand6309(MachineInstr &MI) const;

public:
  explicit M6809InstrInfo(const M6809Subtarget &STI);

  const M6809RegisterInfo &getRegisterInfo() const { return RI; }

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;

  void storeRegToStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
      bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  void loadRegFromStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
      int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      unsigned SubReg = 0,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

protected:
  MachineInstr *foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                                      ArrayRef<unsigned> Ops, int FrameIndex,
                                      MachineInstr *&CopyMI,
                                      LiveIntervals *LIS = nullptr,
                                      VirtRegMap *VRM = nullptr) const override;

public:
  void emitCompareWithZero(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, const DebugLoc &DL,
                           const MachineOperand &Lhs, int64_t CC) const;

  void reMaterialize(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                     Register DestReg, unsigned SubIdx,
                     const MachineInstr &Orig,
                     LaneBitmask UsedLanes) const override;

  /// True for the CB* compare-and-branch pseudos.
  static bool isCompareAndBranch(unsigned Opc);

  // Branch analysis. A conditional branch is either a CB* compare-and-branch
  // pseudo (before expansion) or an LBcc (after). Cond holds:
  //   [0] opcode, [1..N-2] compare operands (CB* only), [N-1] condition code.
  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify = false) const override;
  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  const TargetRegisterClass *getInlineAsmMemoryOperandRegClass(
      InlineAsm::ConstraintCode C) const override {
    return &M6809::PTR16RegClass;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_M6809INSTRINFO_H
