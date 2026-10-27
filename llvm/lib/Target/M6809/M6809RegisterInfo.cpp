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
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "M6809GenRegisterInfo.inc"

M6809RegisterInfo::M6809RegisterInfo(const M6809Subtarget &STI)
    : M6809GenRegisterInfo(M6809::PC) {}

const MCPhysReg *
M6809RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  // For IRQ, NMI and SWI the CPU saves every register, and the handler's RTI
  // restores them, so a handler need not save any, except W on the HD6309,
  // which the CPU does not save in emulation mode.
  if (MF->getFunction().hasFnAttribute("interrupt"))
    return MF->getSubtarget<M6809Subtarget>().has6309()
               ? CSR_HD6309_Interrupt_SaveList
               : CSR_NoRegs_SaveList;
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
// A value that is tested against zero for EQ or NE, such as a loop
// counter, should not be in U: LEAX, LEAY and ADDD set Z, so "leax -1,x"
// needs no test after it, where "leau -1,u" needs a "cmpu #0".
static bool isZeroTested(Register Reg, const MachineRegisterInfo &MRI) {
  for (const MachineInstr &MI : MRI.use_nodbg_instructions(Reg)) {
    if (MI.getOpcode() != M6809::CB16ri || MI.getOperand(0).getReg() != Reg ||
        MI.getOperand(1).getImm() != 0)
      continue;
    int64_t CC = MI.getOperand(2).getImm();
    if (CC == M6809CC::EQ || CC == M6809CC::NE)
      return true;
  }
  return false;
}

bool M6809RegisterInfo::getRegAllocationHints(
    Register VirtReg, ArrayRef<MCPhysReg> Order,
    SmallSetVector<MCPhysReg, 16> &Hints, const MachineFunction &MF,
    const VirtRegMap *VRM, const LiveRegMatrix *Matrix) const {
  bool Hard = TargetRegisterInfo::getRegAllocationHints(VirtReg, Order, Hints,
                                                        MF, VRM, Matrix);
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  if (Hard || !isZeroTested(VirtReg, MRI))
    return Hard;
  // The copy hints stay first, unless they name U; then X and Y.
  bool HintsU = Hints.remove(M6809::U);
  for (MCPhysReg R : {M6809::X, M6809::Y})
    if (is_contained(Order, R))
      Hints.insert(R);
  if (HintsU)
    Hints.insert(M6809::U);
  return false;
}

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

bool M6809RegisterInfo::shouldCoalesce(
    MachineInstr *MI, const TargetRegisterClass *SrcRC, unsigned SubReg,
    const TargetRegisterClass *DstRC, unsigned DstSubReg,
    const TargetRegisterClass *NewRC, LiveIntervals &LIS) const {
  const MachineRegisterInfo &MRI = MI->getMF()->getRegInfo();
  if (M6809::isPinnedPhysCopy(MI->getOperand(0).getReg(), NewRC, MRI) ||
      M6809::isPinnedPhysCopy(MI->getOperand(1).getReg(), NewRC, MRI))
    return false;
  return NewRC != &M6809::ACC16RegClass || (SrcRC == NewRC && DstRC == NewRC);
}
