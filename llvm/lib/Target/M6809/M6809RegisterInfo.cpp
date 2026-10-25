//===-- M6809RegisterInfo.cpp - M6809 Register Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809RegisterInfo.h"
#include "M6809.h"
#include "M6809FrameLowering.h"
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

const uint32_t *M6809RegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

BitVector M6809RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(M6809::S);
  Reserved.set(M6809::PC);
  Reserved.set(M6809::CC);
  Reserved.set(M6809::DP);
  // V is never allocated; W (with E and F) only on the HD6309.
  Reserved.set(M6809::V);
  if (!MF.getSubtarget<M6809Subtarget>().has6309())
    for (MCPhysReg R : {M6809::E, M6809::F, M6809::W})
      Reserved.set(R);
  if (getFrameLowering(MF)->hasFP(MF))
    Reserved.set(M6809::U);
  return Reserved;
}

// Frame objects are addressed off,S, or off,U when there is a frame pointer.
// Object offsets are relative to S on entry; the prologue moves S down by
// the frame size, and points U two bytes below S on entry.
bool M6809RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  MachineInstr &MI = *II;
  const MachineFrameInfo &MFI = MI.getMF()->getFrameInfo();
  int FI = MI.getOperand(FIOperandNum).getIndex();

  int64_t Offset =
      MFI.getObjectOffset(FI) + MI.getOperand(FIOperandNum + 1).getImm();
  Register Base = getFrameRegister(*MI.getMF());
  if (Base == M6809::U)
    Offset += M6809FrameLowering::FPSaveSize;
  else
    Offset += MFI.getStackSize() + SPAdj;

  // Indexed mode takes a full 16-bit offset, so any frame offset fits.
  MI.getOperand(FIOperandNum).ChangeToRegister(Base, /*isDef=*/false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

Register M6809RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return getFrameLowering(MF)->hasFP(MF) ? M6809::U : M6809::S;
}
