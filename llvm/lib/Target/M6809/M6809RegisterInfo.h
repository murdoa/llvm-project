//===-- M6809RegisterInfo.h - M6809 Register Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6809_M6809REGISTERINFO_H
#define LLVM_LIB_TARGET_M6809_M6809REGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "M6809GenRegisterInfo.inc"

namespace llvm {

class M6809Subtarget;

class M6809RegisterInfo : public M6809GenRegisterInfo {
public:
  explicit M6809RegisterInfo(const M6809Subtarget &STI);

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const override;
  const uint32_t *getNoPreservedMask() const override;
  BitVector getReservedRegs(const MachineFunction &MF) const override;

  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  Register getFrameRegister(const MachineFunction &MF) const override;

  bool getRegAllocationHints(Register VirtReg, ArrayRef<MCPhysReg> Order,
                             SmallSetVector<MCPhysReg, 16> &Hints,
                             const MachineFunction &MF,
                             const VirtRegMap *VRM,
                             const LiveRegMatrix *Matrix) const override;

  // Values that an instruction forces into D (or X/Y/U) may live in any
  // 16-bit register between uses. Letting the allocator widen split ranges
  // is what makes "D op D" (both sides computed in D) allocatable.
  // Don't merge a copy that moves a value out of D (or into it) if that
  // would pin both sides to D. See M6809SplitDOperands.cpp.
  bool shouldCoalesce(MachineInstr *MI, const TargetRegisterClass *SrcRC,
                      unsigned SubReg, const TargetRegisterClass *DstRC,
                      unsigned DstSubReg, const TargetRegisterClass *NewRC,
                      LiveIntervals &LIS) const override {
    return NewRC != &M6809::ACC16RegClass || (SrcRC == NewRC && DstRC == NewRC);
  }

  const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &) const override {
    if (M6809::GR16RegClass.hasSubClassEq(RC))
      return &M6809::GR16RegClass;
    return RC;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_M6809REGISTERINFO_H
