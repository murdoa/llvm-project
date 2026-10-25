//===-- M6800RegisterInfo.h - M6800 Register Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6800_M6800REGISTERINFO_H
#define LLVM_LIB_TARGET_M6800_M6800REGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "M6800GenRegisterInfo.inc"

namespace llvm {

class M6800Subtarget;

class M6800RegisterInfo : public M6800GenRegisterInfo {
public:
  explicit M6800RegisterInfo(const M6800Subtarget &STI);

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const override;
  const uint32_t *getNoPreservedMask() const override;
  BitVector getReservedRegs(const MachineFunction &MF) const override;

  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  Register getFrameRegister(const MachineFunction &MF) const override;

  // The 6800 has no alignment requirements. An over-aligned local keeps its
  // alignment within the frame but SP is never realigned. A frame that could
  // not be realigned would make the local a dynamic alloca, which needs a
  // frame pointer.
  bool canRealignStack(const MachineFunction &) const override {
    return false;
  }

  // Values that an instruction forces into D (or X) may live in any
  // 16-bit register between uses. Letting the allocator widen split ranges
  // is what makes "D op D" (both sides computed in D) allocatable.
  // Don't merge a copy that moves a value out of D (or into it) if that
  // would pin both sides to D. See M6800SplitDOperands.cpp.
  bool shouldCoalesce(MachineInstr *MI, const TargetRegisterClass *SrcRC,
                      unsigned SubReg, const TargetRegisterClass *DstRC,
                      unsigned DstSubReg, const TargetRegisterClass *NewRC,
                      LiveIntervals &LIS) const override {
    return NewRC != &M6800::ACC16RegClass || (SrcRC == NewRC && DstRC == NewRC);
  }

  const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &) const override {
    if (M6800::GR16RegClass.hasSubClassEq(RC))
      return &M6800::GR16RegClass;
    return RC;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6800_M6800REGISTERINFO_H
