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
  else if (M6809::XFER16RegClass.contains(DestReg, SrcReg))
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

//===----------------------------------------------------------------------===//
// Post-RA pseudo expansion
//===----------------------------------------------------------------------===//

// Register-register pseudo -> the matching ",s+" / ",s++" pop form.
static unsigned getPopOpcode(unsigned Opc) {
  switch (Opc) {
  case M6809::ADD8rr: return M6809::ADD8pop;
  case M6809::SUB8rr: return M6809::SUB8pop;
  case M6809::AND8rr: return M6809::AND8pop;
  case M6809::OR8rr: return M6809::OR8pop;
  case M6809::EOR8rr: return M6809::EOR8pop;
  case M6809::ADDDrr: return M6809::ADDDpop;
  case M6809::SUBDrr: return M6809::SUBDpop;
  default: return 0;
  }
}

namespace {
// The 8-bit forms used to build a 16-bit logic operation on D.
struct Logic16Parts {
  unsigned Imm, Mem, Pop;
};
} // namespace

static bool getLogic16Parts(unsigned Opc, Logic16Parts &P, char &Form) {
  switch (Opc) {
  case M6809::AND16i: Form = 'i'; P = {M6809::AND8i, M6809::AND8m, M6809::AND8pop}; return true;
  case M6809::AND16m: Form = 'm'; P = {M6809::AND8i, M6809::AND8m, M6809::AND8pop}; return true;
  case M6809::AND16rr: Form = 'r'; P = {M6809::AND8i, M6809::AND8m, M6809::AND8pop}; return true;
  case M6809::OR16i: Form = 'i'; P = {M6809::OR8i, M6809::OR8m, M6809::OR8pop}; return true;
  case M6809::OR16m: Form = 'm'; P = {M6809::OR8i, M6809::OR8m, M6809::OR8pop}; return true;
  case M6809::OR16rr: Form = 'r'; P = {M6809::OR8i, M6809::OR8m, M6809::OR8pop}; return true;
  case M6809::EOR16i: Form = 'i'; P = {M6809::EOR8i, M6809::EOR8m, M6809::EOR8pop}; return true;
  case M6809::EOR16m: Form = 'm'; P = {M6809::EOR8i, M6809::EOR8m, M6809::EOR8pop}; return true;
  case M6809::EOR16rr: Form = 'r'; P = {M6809::EOR8i, M6809::EOR8m, M6809::EOR8pop}; return true;
  default: return false;
  }
}

// Adds a memory offset operand, moved on by Delta bytes.
static void addOffset(MachineInstrBuilder &MIB, const MachineOperand &MO,
                      int64_t Delta) {
  if (MO.isImm()) {
    MIB.addImm(MO.getImm() + Delta);
    return;
  }
  MachineOperand Copy = MO;
  Copy.setOffset(MO.getOffset() + Delta);
  MIB.add(Copy);
}

bool M6809InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  unsigned Opc = MI.getOpcode();

  if (unsigned PopOpc = getPopOpcode(Opc)) {
    Register Dst = MI.getOperand(0).getReg();
    const MachineOperand &Rhs = MI.getOperand(2);
    bool Is8 = M6809::ACC8RegClass.contains(Dst);
    BuildMI(MBB, MI, DL, get(Is8 ? M6809::PSHS8 : M6809::PSHS16))
        .addReg(Rhs.getReg(), getKillRegState(Rhs.isKill()));
    BuildMI(MBB, MI, DL, get(PopOpc), Dst).addReg(Dst);
    MI.eraseFromParent();
    return true;
  }

  Logic16Parts P;
  char Form;
  if (getLogic16Parts(Opc, P, Form)) {
    // D = A:B, so the high byte is done on A and the low byte on B. Big-endian
    // memory puts the high byte first.
    MachineInstrBuilder Hi, Lo;
    switch (Form) {
    case 'i': {
      int64_t Imm = MI.getOperand(2).getImm();
      Hi = BuildMI(MBB, MI, DL, get(P.Imm), M6809::A)
               .addReg(M6809::A)
               .addImm((Imm >> 8) & 0xFF);
      Lo = BuildMI(MBB, MI, DL, get(P.Imm), M6809::B)
               .addReg(M6809::B)
               .addImm(Imm & 0xFF);
      break;
    }
    case 'm': {
      const MachineOperand &Base = MI.getOperand(2);
      const MachineOperand &Off = MI.getOperand(3);
      Hi = BuildMI(MBB, MI, DL, get(P.Mem), M6809::A)
               .addReg(M6809::A)
               .addReg(Base.getReg());
      addOffset(Hi, Off, 0);
      Lo = BuildMI(MBB, MI, DL, get(P.Mem), M6809::B)
               .addReg(M6809::B)
               .addReg(Base.getReg());
      addOffset(Lo, Off, 1);
      Hi.cloneMemRefs(MI);
      Lo.cloneMemRefs(MI);
      break;
    }
    default: {
      const MachineOperand &Rhs = MI.getOperand(2);
      BuildMI(MBB, MI, DL, get(M6809::PSHS16))
          .addReg(Rhs.getReg(), getKillRegState(Rhs.isKill()));
      Hi = BuildMI(MBB, MI, DL, get(P.Pop), M6809::A).addReg(M6809::A);
      Lo = BuildMI(MBB, MI, DL, get(P.Pop), M6809::B).addReg(M6809::B);
      break;
    }
    }
    Lo.addReg(M6809::D, RegState::ImplicitDefine);
    MI.eraseFromParent();
    return true;
  }

  return false;
}
