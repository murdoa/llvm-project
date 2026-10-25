//===-- M6809InstrInfo.cpp - M6809 Instruction Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809InstrInfo.h"
#include "M6809.h"
#include "M6809Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "M6809GenInstrInfo.inc"

void M6809InstrInfo::anchor() {}

M6809InstrInfo::M6809InstrInfo(const M6809Subtarget &STI)
    : M6809GenInstrInfo(STI, RI, M6809::ADJCALLSTACKDOWN,
                        M6809::ADJCALLSTACKUP),
      RI(STI) {}

void M6809InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, Register DestReg,
                                 Register SrcReg, bool KillSrc,
                                 bool RenamableDest, bool RenamableSrc) const {
  unsigned Opc;
  if (M6809::ACC8RegClass.contains(DestReg, SrcReg))
    Opc = M6809::TFR8;
  else if (M6809::TFR16RegClass.contains(DestReg, SrcReg))
    Opc = M6809::TFR16;
  else
    llvm_unreachable("M6809: cannot copy between these registers");

  BuildMI(MBB, I, DL, get(Opc), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

static MachineMemOperand *getFrameMMO(MachineBasicBlock &MBB, int FI,
                                      MachineMemOperand::Flags Flags) {
  MachineFunction &MF = *MBB.getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(MF, FI),
                                 Flags, MFI.getObjectSize(FI),
                                 MFI.getObjectAlign(FI));
}

void M6809InstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  unsigned Opc = RC->hasSuperClassEq(&M6809::ACC8RegClass) ? M6809::ST8m
                                                           : M6809::ST16m;
  BuildMI(MBB, MI, DL, get(Opc))
      .addReg(SrcReg, getKillRegState(IsKill))
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addMemOperand(getFrameMMO(MBB, FrameIndex, MachineMemOperand::MOStore))
      .setMIFlag(Flags);
}

void M6809InstrInfo::loadRegFromStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
    int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    unsigned SubReg, MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  unsigned Opc = RC->hasSuperClassEq(&M6809::ACC8RegClass) ? M6809::LD8m
                                                           : M6809::LD16m;
  BuildMI(MBB, MI, DL, get(Opc), DestReg)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addMemOperand(getFrameMMO(MBB, FrameIndex, MachineMemOperand::MOLoad))
      .setMIFlag(Flags);
}
