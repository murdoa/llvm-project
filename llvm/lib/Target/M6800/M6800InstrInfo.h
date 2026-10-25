//===-- M6800InstrInfo.h - M6800 Instruction Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6800_M6800INSTRINFO_H
#define LLVM_LIB_TARGET_M6800_M6800INSTRINFO_H

#include "M6800RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "M6800GenInstrInfo.inc"

namespace llvm {

class M6800Subtarget;

class M6800InstrInfo : public M6800GenInstrInfo {
  const M6800RegisterInfo RI;
  const M6800Subtarget &STI;
  virtual void anchor();

  void pushD(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
             const DebugLoc &DL) const;
  void pullD(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
             const DebugLoc &DL) const;

public:
  explicit M6800InstrInfo(const M6800Subtarget &STI);

  /// Marks undef the reads by XGDX, PSHX and PSH8 of registers with no live
  /// part, from the block's live-ins forward.
  static void markUndefSaves(MachineBasicBlock &MBB,
                             const TargetRegisterInfo *TRI);

  const M6800RegisterInfo &getRegisterInfo() const { return RI; }

  /// True if Reg (or a part of it) is live just after MI. Needs the block's
  /// live-ins, so only after register allocation.
  bool isLiveAfter(const MachineInstr &MI, MCRegister Reg) const;

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
    return &M6800::PTR16RegClass;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6800_M6800INSTRINFO_H
