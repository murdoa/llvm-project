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
      .addImm(M6809Mem::Auto)
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
      .addImm(M6809Mem::Auto)
      .addMemOperand(getFrameMMO(MBB, FrameIndex, MachineMemOperand::MOLoad))
      .setMIFlag(Flags);
}

// CLR clears C, so a rematerialized zero could break a carry chain that the
// allocator placed it inside. LD #0 leaves C alone and is always safe.
void M6809InstrInfo::reMaterialize(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   Register DestReg, unsigned SubIdx,
                                   const MachineInstr &Orig,
                                   LaneBitmask UsedLanes) const {
  if (Orig.getOpcode() != M6809::CLR8) {
    TargetInstrInfo::reMaterialize(MBB, I, DestReg, SubIdx, Orig, UsedLanes);
    return;
  }
  MachineInstr *MI = BuildMI(*MBB.getParent(), Orig.getDebugLoc(),
                             get(M6809::LD8i), DestReg)
                         .addImm(0);
  MBB.insert(I, MI);
  if (SubIdx)
    MI->getOperand(0).setSubReg(SubIdx);
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
// The 8-bit forms used to build a 16-bit operation on D. Carry operations
// (Carry set) must do the low byte first; their register-register form
// reads the pushed value's low byte from 1,s, then the high byte with ,s++.
struct Logic16Parts {
  unsigned Imm, Mem, Pop;
  bool Carry = false;
};
} // namespace

static bool getLogic16Parts(unsigned Opc, Logic16Parts &P, char &Form) {
  switch (Opc) {
  case M6809::ADC16i: Form = 'i'; P = {M6809::ADC8i, M6809::ADC8m, M6809::ADC8pop2, true}; return true;
  case M6809::ADC16m: Form = 'm'; P = {M6809::ADC8i, M6809::ADC8m, M6809::ADC8pop2, true}; return true;
  case M6809::ADC16rr: Form = 'r'; P = {M6809::ADC8i, M6809::ADC8m, M6809::ADC8pop2, true}; return true;
  case M6809::SBC16i: Form = 'i'; P = {M6809::SBC8i, M6809::SBC8m, M6809::SBC8pop2, true}; return true;
  case M6809::SBC16m: Form = 'm'; P = {M6809::SBC8i, M6809::SBC8m, M6809::SBC8pop2, true}; return true;
  case M6809::SBC16rr: Form = 'r'; P = {M6809::SBC8i, M6809::SBC8m, M6809::SBC8pop2, true}; return true;
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

// Sets the flags for "Lhs cc 0" in fewer bytes than "cmp #0". TST and
// "subd #0" set all four flags as a compare with zero would (V and C come
// out clear). LEAX/LEAY set only Z, so they serve only EQ and NE.
//
// Often the instruction just before has already set the flags from R, and
// no test is needed at all.
static bool flagsAlreadySet(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, Register R,
                            int64_t CC) {
  if (MI == MBB.begin())
    return false;
  MachineBasicBlock::iterator Prev = std::prev(MI);
  while (Prev->isDebugInstr() || Prev->isKill()) {
    if (Prev == MBB.begin())
      return false;
    --Prev;
  }
  if (Prev->getNumExplicitDefs() != 1 || !Prev->getOperand(0).isReg() ||
      Prev->getOperand(0).getReg() != R)
    return false;

  bool EqNe = CC == M6809CC::EQ || CC == M6809CC::NE;
  bool Sign = EqNe || CC == M6809CC::MI || CC == M6809CC::PL;
  // Signed orderings also work after instructions that clear V.
  bool VClear = Sign || CC == M6809CC::LT || CC == M6809CC::GE ||
                CC == M6809CC::GT || CC == M6809CC::LE;
  switch (Prev->getOpcode()) {
  case M6809::LEA16:
    // LEAX/LEAY set Z; LEAU does not.
    return EqNe && (R == M6809::X || R == M6809::Y);
  case M6809::LD8i:
  case M6809::LD8m:
  case M6809::LD16i:
  case M6809::LD16m:
  case M6809::AND8i:
  case M6809::AND8m:
  case M6809::AND8pop:
  case M6809::OR8i:
  case M6809::OR8m:
  case M6809::OR8pop:
  case M6809::EOR8i:
  case M6809::EOR8m:
  case M6809::EOR8pop:
    return VClear; // N and Z from the result, V cleared.
  case M6809::ADD8i:
  case M6809::ADD8m:
  case M6809::ADD8pop:
  case M6809::SUB8i:
  case M6809::SUB8m:
  case M6809::SUB8pop:
  case M6809::ADDDi:
  case M6809::ADDDm:
  case M6809::ADDDpop:
  case M6809::SUBDi:
  case M6809::SUBDm:
  case M6809::SUBDpop:
    return Sign; // N and Z from the result; V is a real overflow.
  default:
    return false;
  }
}

void M6809InstrInfo::emitCompareWithZero(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         const DebugLoc &DL,
                                         const MachineOperand &Lhs,
                                         int64_t CC) const {
  Register R = Lhs.getReg();
  if (flagsAlreadySet(MBB, MI, R, CC))
    return;
  bool EqNe = CC == M6809CC::EQ || CC == M6809CC::NE;
  if (M6809::ACC8RegClass.contains(R)) {
    BuildMI(MBB, MI, DL, get(M6809::TST8)).add(Lhs);
  } else if (R == M6809::D) {
    BuildMI(MBB, MI, DL, get(M6809::SUBDi), R).addReg(R).addImm(0);
  } else if (EqNe && (R == M6809::X || R == M6809::Y)) {
    BuildMI(MBB, MI, DL, get(M6809::LEA16), R)
        .addReg(R)
        .addImm(0)
        .addImm(M6809Mem::Auto);
  } else {
    BuildMI(MBB, MI, DL, get(M6809::CMP16i)).add(Lhs).addImm(0);
  }
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

  // One-bit shifts of D: shift one byte, then rotate the carry into the
  // other. Left shifts start with the low byte (B), right shifts with A.
  unsigned FirstOpc = 0;
  switch (Opc) {
  case M6809::LSL16:
    FirstOpc = M6809::ASL8;
    break;
  case M6809::LSR16:
    FirstOpc = M6809::LSR8;
    break;
  case M6809::ASR16:
    FirstOpc = M6809::ASR8;
    break;
  case M6809::ROL16:
    FirstOpc = M6809::ROL8;
    break;
  case M6809::ROR16:
    FirstOpc = M6809::ROR8;
    break;
  }
  if (Opc == M6809::SHL16_8 || Opc == M6809::LSR16_8 ||
      Opc == M6809::ASR16_8) {
    bool Left = Opc == M6809::SHL16_8;
    BuildMI(MBB, MI, DL, get(M6809::TFR8), Left ? M6809::A : M6809::B)
        .addReg(Left ? M6809::B : M6809::A);
    MachineInstrBuilder Fill;
    if (Opc == M6809::SHL16_8)
      Fill = BuildMI(MBB, MI, DL, get(M6809::CLR8), M6809::B);
    else if (Opc == M6809::LSR16_8)
      Fill = BuildMI(MBB, MI, DL, get(M6809::CLR8), M6809::A);
    else
      Fill = BuildMI(MBB, MI, DL, get(M6809::SEX), M6809::D).addReg(M6809::D);
    Fill.addReg(M6809::D, RegState::ImplicitDefine);
    MI.eraseFromParent();
    return true;
  }

  if (FirstOpc) {
    bool Left = Opc == M6809::LSL16 || Opc == M6809::ROL16;
    Register First = Left ? M6809::B : M6809::A;
    Register Second = Left ? M6809::A : M6809::B;
    BuildMI(MBB, MI, DL, get(FirstOpc), First).addReg(First);
    BuildMI(MBB, MI, DL, get(Left ? M6809::ROL8 : M6809::ROR8), Second)
        .addReg(Second)
        .addReg(M6809::D, RegState::ImplicitDefine);
    MI.eraseFromParent();
    return true;
  }

  Logic16Parts P;
  char Form;
  if (getLogic16Parts(Opc, P, Form)) {
    // D = A:B, so the high byte is done on A and the low byte on B. Big-endian
    // memory puts the high byte first. Carry operations go low byte first.
    auto EmitHalf = [&](bool High) {
      Register R = High ? M6809::A : M6809::B;
      MachineInstrBuilder MIB;
      switch (Form) {
      case 'i': {
        int64_t Imm = MI.getOperand(2).getImm();
        MIB = BuildMI(MBB, MI, DL, get(P.Imm), R)
                  .addReg(R)
                  .addImm(High ? (Imm >> 8) & 0xFF : Imm & 0xFF);
        break;
      }
      case 'm': {
        MIB = BuildMI(MBB, MI, DL, get(P.Mem), R)
                  .addReg(R)
                  .addReg(MI.getOperand(2).getReg());
        addOffset(MIB, MI.getOperand(3), High ? 0 : 1);
        MIB.add(MI.getOperand(4)); // mode
        MIB.cloneMemRefs(MI);
        break;
      }
      default:
        if (P.Carry && !High) {
          // Low byte of the pushed value, still on the stack.
          MIB = BuildMI(MBB, MI, DL, get(P.Mem), R)
                    .addReg(R)
                    .addReg(M6809::S)
                    .addImm(1)
                    .addImm(M6809Mem::Auto);
        } else {
          MIB = BuildMI(MBB, MI, DL, get(P.Pop), R).addReg(R);
        }
        break;
      }
      return MIB;
    };

    if (Form == 'r') {
      const MachineOperand &Rhs = MI.getOperand(2);
      BuildMI(MBB, MI, DL, get(M6809::PSHS16))
          .addReg(Rhs.getReg(), getKillRegState(Rhs.isKill()));
    }
    EmitHalf(!P.Carry);
    MachineInstrBuilder Last = EmitHalf(P.Carry);
    Last.addReg(M6809::D, RegState::ImplicitDefine);
    MI.eraseFromParent();
    return true;
  }

  if (Opc == M6809::ADD16ri) {
    Register Dst = MI.getOperand(0).getReg();
    const MachineOperand &Imm = MI.getOperand(2);
    if (Dst == M6809::D)
      BuildMI(MBB, MI, DL, get(M6809::ADDDi), Dst).addReg(Dst).add(Imm);
    else
      BuildMI(MBB, MI, DL, get(M6809::LEA16), Dst)
          .addReg(Dst)
          .add(Imm)
          .addImm(M6809Mem::Auto);
    MI.eraseFromParent();
    return true;
  }

  if (isCompareAndBranch(Opc)) {
    unsigned N = MI.getNumExplicitOperands();
    const MachineOperand &Lhs = MI.getOperand(0);
    int64_t CC = MI.getOperand(N - 2).getImm();
    MachineBasicBlock *Dest = MI.getOperand(N - 1).getMBB();
    bool Is8 = Opc == M6809::CB8ri || Opc == M6809::CB8rm ||
               Opc == M6809::CB8rr;

    switch (Opc) {
    case M6809::CB8ri:
    case M6809::CB16ri:
      if (MI.getOperand(1).getImm() == 0) {
        emitCompareWithZero(MBB, MI, DL, Lhs, CC);
        break;
      }
      BuildMI(MBB, MI, DL, get(Is8 ? M6809::CMP8i : M6809::CMP16i))
          .add(Lhs)
          .add(MI.getOperand(1));
      break;
    case M6809::CB8rm:
    case M6809::CB16rm:
      BuildMI(MBB, MI, DL, get(Is8 ? M6809::CMP8m : M6809::CMP16m))
          .add(Lhs)
          .add(MI.getOperand(1))
          .add(MI.getOperand(2))
          .add(MI.getOperand(3))
          .cloneMemRefs(MI);
      break;
    default: {
      const MachineOperand &Rhs = MI.getOperand(1);
      BuildMI(MBB, MI, DL, get(Is8 ? M6809::PSHS8 : M6809::PSHS16))
          .addReg(Rhs.getReg(), getKillRegState(Rhs.isKill()));
      BuildMI(MBB, MI, DL, get(Is8 ? M6809::CMP8pop : M6809::CMP16pop))
          .add(Lhs);
      break;
    }
    }
    BuildMI(MBB, MI, DL, get(M6809::LBcc)).addImm(CC).addMBB(Dest);
    MI.eraseFromParent();
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Branch analysis
//===----------------------------------------------------------------------===//

bool M6809InstrInfo::isCompareAndBranch(unsigned Opc) {
  switch (Opc) {
  case M6809::CB8ri:
  case M6809::CB8rm:
  case M6809::CB8rr:
  case M6809::CB16ri:
  case M6809::CB16rm:
  case M6809::CB16rr:
    return true;
  default:
    return false;
  }
}

static bool isCondBranch(unsigned Opc) {
  return Opc == M6809::LBcc || M6809InstrInfo::isCompareAndBranch(Opc);
}

// Fills Cond from a conditional branch and returns its target.
static MachineBasicBlock *parseCondBranch(const MachineInstr &MI,
                                          SmallVectorImpl<MachineOperand> &Cond) {
  Cond.push_back(MachineOperand::CreateImm(MI.getOpcode()));
  unsigned N = MI.getNumExplicitOperands();
  for (unsigned I = 0; I + 1 < N; ++I) {
    MachineOperand MO = MI.getOperand(I);
    if (MO.isReg())
      MO.setIsKill(false);
    Cond.push_back(MO);
  }
  return MI.getOperand(N - 1).getMBB();
}

bool M6809InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!isUnpredicatedTerminator(*I))
      break;
    if (!I->isBranch())
      return true;

    unsigned Opc = I->getOpcode();
    if (Opc == M6809::LBRA) {
      MachineBasicBlock *Target = I->getOperand(0).getMBB();
      if (!AllowModify) {
        TBB = Target;
        continue;
      }
      // Anything after an unconditional branch is dead.
      MBB.erase(std::next(I), MBB.end());
      Cond.clear();
      FBB = nullptr;
      if (MBB.isLayoutSuccessor(Target)) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        continue;
      }
      TBB = Target;
      continue;
    }

    if (!isCondBranch(Opc))
      return true;
    // Only one conditional branch per block is understood.
    if (!Cond.empty())
      return true;
    FBB = TBB;
    TBB = parseCondBranch(*I, Cond);
  }
  return false;
}

unsigned M6809InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  unsigned Count = 0;
  int Bytes = 0;
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (I->getOpcode() != M6809::LBRA && !isCondBranch(I->getOpcode()))
      break;
    Bytes += getInstSizeInBytes(*I);
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }
  if (BytesRemoved)
    *BytesRemoved = Bytes;
  return Count;
}

unsigned M6809InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                      MachineBasicBlock *TBB,
                                      MachineBasicBlock *FBB,
                                      ArrayRef<MachineOperand> Cond,
                                      const DebugLoc &DL,
                                      int *BytesAdded) const {
  assert(TBB && "insertBranch needs a target");
  int Bytes = 0;

  if (Cond.empty()) {
    assert(!FBB && "unconditional branch with two targets");
    MachineInstr &MI = *BuildMI(&MBB, DL, get(M6809::LBRA)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded = getInstSizeInBytes(MI);
    return 1;
  }

  MachineInstrBuilder MIB = BuildMI(&MBB, DL, get(Cond[0].getImm()));
  for (const MachineOperand &MO : Cond.drop_front())
    MIB.add(MO);
  MIB.addMBB(TBB);
  Bytes += getInstSizeInBytes(*MIB);

  if (!FBB) {
    if (BytesAdded)
      *BytesAdded = Bytes;
    return 1;
  }
  MachineInstr &MI = *BuildMI(&MBB, DL, get(M6809::LBRA)).addMBB(FBB);
  Bytes += getInstSizeInBytes(MI);
  if (BytesAdded)
    *BytesAdded = Bytes;
  return 2;
}

bool M6809InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  MachineOperand &CC = Cond.back();
  CC.setImm(M6809CC::getOpposite(static_cast<M6809CC::CondCode>(CC.getImm())));
  return false;
}
