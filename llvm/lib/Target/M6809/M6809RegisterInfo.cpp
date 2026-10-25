//===-- M6809RegisterInfo.cpp - M6809 Register Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809RegisterInfo.h"
#include "M6809.h"
#include "M6809Subtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "M6809GenRegisterInfo.inc"

M6809RegisterInfo::M6809RegisterInfo(const M6809Subtarget &STI)
    : M6809GenRegisterInfo(M6809::PC) {}

const MCPhysReg *
M6809RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_M6809_SaveList;
}

const uint32_t *
M6809RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                        CallingConv::ID CC) const {
  return CSR_M6809_RegMask;
}

BitVector M6809RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(M6809::S);
  Reserved.set(M6809::PC);
  Reserved.set(M6809::CC);
  Reserved.set(M6809::DP);
  return Reserved;
}

// Frame objects are addressed off,S. Object offsets are relative to S on
// entry, and the prologue moves S down by the frame size.
bool M6809RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  MachineInstr &MI = *II;
  const MachineFrameInfo &MFI = MI.getMF()->getFrameInfo();
  int FI = MI.getOperand(FIOperandNum).getIndex();

  int64_t Offset = MFI.getObjectOffset(FI) + MFI.getStackSize() + SPAdj +
                   MI.getOperand(FIOperandNum + 1).getImm();

  // Indexed mode takes a full 16-bit offset, so any frame offset fits.
  MI.getOperand(FIOperandNum).ChangeToRegister(M6809::S, /*isDef=*/false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

Register M6809RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return M6809::S;
}
