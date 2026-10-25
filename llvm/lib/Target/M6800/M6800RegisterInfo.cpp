//===-- M6800RegisterInfo.cpp - M6800 Register Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800RegisterInfo.h"
#include "M6800.h"
#include "M6800Subtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "M6800GenRegisterInfo.inc"

M6800RegisterInfo::M6800RegisterInfo(const M6800Subtarget &STI)
    : M6800GenRegisterInfo(M6800::PC) {}

const MCPhysReg *
M6800RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_M6800_SaveList;
}

const uint32_t *
M6800RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                        CallingConv::ID CC) const {
  return CSR_M6800_RegMask;
}

const uint32_t *M6800RegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

BitVector M6800RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(M6800::SP);
  Reserved.set(M6800::PC);
  Reserved.set(M6800::CC);
  Reserved.set(M6800::FP);
  return Reserved;
}

// Frame objects become "off,sp" memory operands, where the offset counts
// from SP + 1, the lowest byte in use: the X that TSX gives. M6800FrameAccess
// later turns them into "tsx" and "off,x". Object offsets are relative to
// SP + 1 on entry, which is the return address; the prologue moves SP down
// by the frame size.
bool M6800RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  MachineInstr &MI = *II;
  const MachineFrameInfo &MFI = MI.getMF()->getFrameInfo();
  int FI = MI.getOperand(FIOperandNum).getIndex();

  // With a frame pointer, the same offset from __m6800_fp, which does not
  // move with SP.
  bool FP = getFrameLowering(*MI.getMF())->hasFP(*MI.getMF());
  int64_t Offset = MFI.getObjectOffset(FI) +
                   MI.getOperand(FIOperandNum + 1).getImm() +
                   MFI.getStackSize() + (FP ? 0 : SPAdj);
  MI.getOperand(FIOperandNum)
      .ChangeToRegister(FP ? M6800::FP : M6800::SP, /*isDef=*/false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

Register M6800RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return getFrameLowering(MF)->hasFP(MF) ? M6800::FP : M6800::SP;
}
