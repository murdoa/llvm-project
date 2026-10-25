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
#include "llvm/CodeGen/LivePhysRegs.h"
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
      RI(STI), STI(STI) {}

void M6809InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, Register DestReg,
                                 Register SrcReg, bool KillSrc,
                                 bool RenamableDest, bool RenamableSrc) const {
  unsigned Opc;
  auto Is8 = [](Register R) {
    return R == M6809::A || R == M6809::B || R == M6809::E || R == M6809::F;
  };
  if (M6809::ACC8RegClass.contains(DestReg, SrcReg)) {
    Opc = M6809::TFR8;
  } else if (M6809::XFER16RegClass.contains(DestReg, SrcReg)) {
    Opc = M6809::TFR16;
  } else if (STI.has6309() && Is8(DestReg) && Is8(SrcReg)) {
    // The halves of W: "tfr f,b".
    BuildMI(MBB, I, DL, get(M6809::TFRrr))
        .addReg(SrcReg, getKillRegState(KillSrc))
        .addReg(DestReg, RegState::Undef)
        .addReg(DestReg, RegState::ImplicitDefine);
    return;
  } else {
    llvm_unreachable("M6809: cannot copy between these registers");
  }

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

// The memory form of a pseudo whose right-hand register operand (RhsIdx) can
// be read from memory instead.
static unsigned getMemForm(unsigned Opc, unsigned &RhsIdx) {
  RhsIdx = 2;
  switch (Opc) {
  case M6809::ADD8rr: return M6809::ADD8m;
  case M6809::SUB8rr: return M6809::SUB8m;
  case M6809::AND8rr: return M6809::AND8m;
  case M6809::OR8rr: return M6809::OR8m;
  case M6809::EOR8rr: return M6809::EOR8m;
  case M6809::ADDDrr: return M6809::ADDDm;
  case M6809::SUBDrr: return M6809::SUBDm;
  case M6809::ADC16rr: return M6809::ADC16m;
  case M6809::SBC16rr: return M6809::SBC16m;
  case M6809::AND16rr: return M6809::AND16m;
  case M6809::OR16rr: return M6809::OR16m;
  case M6809::EOR16rr: return M6809::EOR16m;
  }
  RhsIdx = 1;
  switch (Opc) {
  case M6809::CB8rr: return M6809::CB8rm;
  case M6809::CB16rr: return M6809::CB16rm;
  case M6809::SBCBRrr: return M6809::SBCBRm;
  }
  return 0;
}

// A spilled right-hand operand is read from its stack slot: "subd 4,s"
// instead of "ldx 4,s; pshs x; subd ,s++". A compare whose left-hand
// operand is spilled swaps its operands.
MachineInstr *M6809InstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
    int FrameIndex, MachineInstr *&CopyMI, LiveIntervals *LIS,
    VirtRegMap *VRM) const {
  if (Ops.size() != 1)
    return nullptr;
  unsigned RhsIdx;
  unsigned MemOpc = getMemForm(MI.getOpcode(), RhsIdx);
  if (!MemOpc || MI.getOperand(Ops[0]).getSubReg())
    return nullptr;
  bool IsCB = MI.getOpcode() == M6809::CB8rr || MI.getOpcode() == M6809::CB16rr;
  bool Swap = IsCB && Ops[0] == 0;
  if (Swap) {
    auto CC = static_cast<M6809CC::CondCode>(MI.getOperand(2).getImm());
    if (CC >= M6809CC::VC && CC <= M6809CC::MI)
      return nullptr;
  }
  if (Ops[0] != RhsIdx && !Swap)
    return nullptr;

  MachineInstrBuilder MIB =
      BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(MemOpc));
  for (unsigned I = 0, N = MI.getNumExplicitOperands(); I != N; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (Swap && I == 0)
      continue;
    if (I == RhsIdx) {
      if (Swap)
        MIB.add(MI.getOperand(1));
      MIB.addFrameIndex(FrameIndex).addImm(0).addImm(M6809Mem::Auto);
      continue;
    }
    if (Swap && I == 2) { // the condition
      MIB.addImm(M6809CC::getSwapped(
          static_cast<M6809CC::CondCode>(MO.getImm())));
      continue;
    }
    MIB.add(MO);
  }
  return MIB;
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
  case M6809::COM8:
    return VClear; // N and Z from the result, V cleared.
  case M6809::INC8:
  case M6809::DEC8:
  case M6809::NEG8:
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
  } else if (R == M6809::D && STI.has6309() && CC != M6809CC::HI &&
             CC != M6809CC::LS && CC != M6809CC::HS && CC != M6809CC::LO) {
    // TSTD leaves C alone, so it serves every condition but the unsigned
    // ones.
    BuildMI(MBB, MI, DL, get(M6809::TST16r)).add(Lhs);
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

// The last of the byte instructions that make up a 16-bit operation on D
// says that all of D is now defined. It reads D too: the other byte was
// written by the instruction before, or passes through unchanged, and must
// stay live up to here.
static void addWholeD(MachineInstrBuilder &MIB) {
  MIB.addReg(M6809::D, RegState::Implicit)
      .addReg(M6809::D, RegState::ImplicitDefine);
}

// The HD6309 does in one instruction what the 6809 needs a sequence for:
// register-register arithmetic, 16-bit logic on memory, and 16-bit shifts,
// negation and tests of D.
bool M6809InstrInfo::expand6309(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  unsigned Opc = MI.getOpcode();

  // "op rhs,dst": dst = dst op rhs.
  auto RegReg = [&](unsigned RROpc, const MachineOperand &Rhs, Register Dst,
                    bool Writes = true) {
    MachineInstrBuilder MIB =
        BuildMI(MBB, MI, DL, get(RROpc))
            .addReg(Rhs.getReg(), getKillRegState(Rhs.isKill()))
            .addReg(Dst);
    if (Writes)
      MIB.addReg(Dst, RegState::ImplicitDefine);
    MI.eraseFromParent();
    return true;
  };
  switch (Opc) {
  case M6809::ADD8rr:
  case M6809::ADDDrr:
    return RegReg(M6809::ADDR, MI.getOperand(2), MI.getOperand(0).getReg());
  case M6809::SUB8rr:
  case M6809::SUBDrr:
    return RegReg(M6809::SUBR, MI.getOperand(2), MI.getOperand(0).getReg());
  case M6809::AND8rr:
  case M6809::AND16rr:
    return RegReg(M6809::ANDR, MI.getOperand(2), MI.getOperand(0).getReg());
  case M6809::OR8rr:
  case M6809::OR16rr:
    return RegReg(M6809::ORR, MI.getOperand(2), MI.getOperand(0).getReg());
  case M6809::EOR8rr:
  case M6809::EOR16rr:
    return RegReg(M6809::EORR, MI.getOperand(2), MI.getOperand(0).getReg());
  case M6809::ADC16rr:
    return RegReg(M6809::ADCR, MI.getOperand(2), M6809::D);
  case M6809::SBC16rr:
    return RegReg(M6809::SBCR, MI.getOperand(2), M6809::D);
  case M6809::ADDR16:
    return RegReg(M6809::ADDR, MI.getOperand(2), MI.getOperand(0).getReg());
  case M6809::SUBR16:
    return RegReg(M6809::SUBR, MI.getOperand(2), MI.getOperand(0).getReg());
  case M6809::ANDR16:
    return RegReg(M6809::ANDR, MI.getOperand(2), MI.getOperand(0).getReg());
  case M6809::ORR16:
    return RegReg(M6809::ORR, MI.getOperand(2), MI.getOperand(0).getReg());
  case M6809::EORR16:
    return RegReg(M6809::EORR, MI.getOperand(2), MI.getOperand(0).getReg());
  case M6809::ADCR16:
    return RegReg(M6809::ADCR, MI.getOperand(2), MI.getOperand(0).getReg());
  case M6809::SBCR16:
    return RegReg(M6809::SBCR, MI.getOperand(2), MI.getOperand(0).getReg());
  default:
    break;
  }

  // 16-bit logic and carry ops on memory: one instruction, not two.
  unsigned MemOpc = 0;
  switch (Opc) {
  case M6809::AND16m: MemOpc = M6809::ANDDm; break;
  case M6809::OR16m: MemOpc = M6809::ORDm; break;
  case M6809::EOR16m: MemOpc = M6809::EORDm; break;
  case M6809::ADC16m: MemOpc = M6809::ADCDm; break;
  case M6809::SBC16m: MemOpc = M6809::SBCDm; break;
  default: break;
  }
  if (MemOpc) {
    BuildMI(MBB, MI, DL, get(MemOpc), M6809::D)
        .addReg(M6809::D)
        .add(MI.getOperand(2))
        .add(MI.getOperand(3))
        .add(MI.getOperand(4))
        .cloneMemRefs(MI);
    MI.eraseFromParent();
    return true;
  }

  if (Opc == M6809::MUL16i || Opc == M6809::MUL16m || Opc == M6809::MUL16rr) {
    MachineInstrBuilder Mul;
    if (Opc == M6809::MUL16i) {
      Mul = BuildMI(MBB, MI, DL, get(M6809::MULDi)).add(MI.getOperand(2));
    } else if (Opc == M6809::MUL16m) {
      Mul = BuildMI(MBB, MI, DL, get(M6809::MULDm))
                .add(MI.getOperand(2))
                .add(MI.getOperand(3))
                .add(MI.getOperand(4))
                .cloneMemRefs(MI);
    } else {
      const MachineOperand &Rhs = MI.getOperand(2);
      BuildMI(MBB, MI, DL, get(M6809::PSHS16))
          .addReg(Rhs.getReg(), getKillRegState(Rhs.isKill()));
      Mul = BuildMI(MBB, MI, DL, get(M6809::MULDm))
                .addReg(M6809::S)
                .addImm(0)
                .addImm(M6809Mem::PostInc2)
                .addReg(M6809::S, RegState::Implicit)
                .addReg(M6809::S, RegState::ImplicitDefine);
    }
    // tfr w,d
    BuildMI(MBB, MI, DL, get(M6809::TFRrr))
        .addReg(M6809::W, RegState::Kill)
        .addReg(M6809::D, RegState::Undef)
        .addReg(M6809::D, RegState::ImplicitDefine);
    MI.eraseFromParent();
    return true;
  }

  // Divide through DIVQ: tfr d,w; sexw (or clrd); divq <op>; then the
  // quotient is in W and the remainder in D.
  bool IsDiv = Opc == M6809::SDIV16i || Opc == M6809::SDIV16m ||
               Opc == M6809::SDIV16rr || Opc == M6809::UDIV16i ||
               Opc == M6809::UDIV16m || Opc == M6809::UDIV16rr;
  bool IsRem = Opc == M6809::SREM16i || Opc == M6809::SREM16m ||
               Opc == M6809::SREM16rr || Opc == M6809::UREM16i ||
               Opc == M6809::UREM16m || Opc == M6809::UREM16rr;
  if (IsDiv || IsRem) {
    bool Unsigned = Opc == M6809::UDIV16i || Opc == M6809::UDIV16m ||
                    Opc == M6809::UDIV16rr || Opc == M6809::UREM16i ||
                    Opc == M6809::UREM16m || Opc == M6809::UREM16rr;
    bool Imm = Opc == M6809::SDIV16i || Opc == M6809::SREM16i ||
               Opc == M6809::UDIV16i || Opc == M6809::UREM16i;
    bool Reg = Opc == M6809::SDIV16rr || Opc == M6809::SREM16rr ||
               Opc == M6809::UDIV16rr || Opc == M6809::UREM16rr;
    if (Reg) {
      const MachineOperand &Rhs = MI.getOperand(2);
      BuildMI(MBB, MI, DL, get(M6809::PSHS16))
          .addReg(Rhs.getReg(), getKillRegState(Rhs.isKill()));
    }
    // tfr d,w
    BuildMI(MBB, MI, DL, get(M6809::TFRrr))
        .addReg(M6809::D)
        .addReg(M6809::W, RegState::Undef)
        .addReg(M6809::W, RegState::ImplicitDefine);
    if (Unsigned)
      BuildMI(MBB, MI, DL, get(M6809::CLR16r), M6809::D);
    else
      BuildMI(MBB, MI, DL, get(M6809::SEXW), M6809::D);
    if (Imm) {
      BuildMI(MBB, MI, DL, get(M6809::DIVQi)).add(MI.getOperand(2));
    } else if (Reg) {
      BuildMI(MBB, MI, DL, get(M6809::DIVQm))
          .addReg(M6809::S)
          .addImm(0)
          .addImm(M6809Mem::PostInc2)
          .addReg(M6809::S, RegState::Implicit)
          .addReg(M6809::S, RegState::ImplicitDefine);
    } else {
      BuildMI(MBB, MI, DL, get(M6809::DIVQm))
          .add(MI.getOperand(2))
          .add(MI.getOperand(3))
          .add(MI.getOperand(4))
          .cloneMemRefs(MI);
    }
    if (IsDiv)
      BuildMI(MBB, MI, DL, get(M6809::TFRrr))
          .addReg(M6809::W, RegState::Kill)
          .addReg(M6809::D, RegState::Undef)
          .addReg(M6809::D, RegState::ImplicitDefine);
    MI.eraseFromParent();
    return true;
  }

  unsigned InhOpc = 0;
  switch (Opc) {
  case M6809::NEG16: InhOpc = M6809::NEG16r; break;
  case M6809::LSL16: InhOpc = M6809::ASL16r; break;
  case M6809::LSR16: InhOpc = M6809::LSR16r; break;
  case M6809::ASR16: InhOpc = M6809::ASR16r; break;
  case M6809::ROL16: InhOpc = M6809::ROL16r; break;
  case M6809::ROR16: InhOpc = M6809::ROR16r; break;
  default: break;
  }
  if (InhOpc) {
    BuildMI(MBB, MI, DL, get(InhOpc), M6809::D).addReg(M6809::D);
    MI.eraseFromParent();
    return true;
  }
  return false;
}

bool M6809InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  unsigned Opc = MI.getOpcode();

  if (STI.has6309() && expand6309(MI))
    return true;

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

  // One-bit rotates (see M6809InstrInfo.td).
  if (Opc == M6809::ROL1_8 || Opc == M6809::ROL1_16) {
    Register Lo = Opc == M6809::ROL1_8 ? MI.getOperand(0).getReg()
                                       : Register(M6809::B);
    BuildMI(MBB, MI, DL, get(M6809::ASL8), Lo).addReg(Lo);
    if (Opc == M6809::ROL1_16)
      BuildMI(MBB, MI, DL, get(M6809::ROL8), M6809::A).addReg(M6809::A);
    MachineInstrBuilder Adc =
        BuildMI(MBB, MI, DL, get(M6809::ADC8i), Lo).addReg(Lo).addImm(0);
    if (Opc == M6809::ROL1_16)
      addWholeD(Adc);
    MI.eraseFromParent();
    return true;
  }
  if (Opc == M6809::ROR1_8 || Opc == M6809::ROR1_16) {
    Register Lo = Opc == M6809::ROR1_8 ? MI.getOperand(0).getReg()
                                       : Register(M6809::B);
    BuildMI(MBB, MI, DL, get(M6809::PSHS8)).addReg(Lo);
    BuildMI(MBB, MI, DL, get(M6809::LSRm))
        .addReg(M6809::S)
        .addImm(0)
        .addImm(M6809Mem::PostInc1)
        .addReg(M6809::S, RegState::Implicit)
        .addReg(M6809::S, RegState::ImplicitDefine);
    if (Opc == M6809::ROR1_16)
      BuildMI(MBB, MI, DL, get(M6809::ROR8), M6809::A).addReg(M6809::A);
    MachineInstrBuilder Ror =
        BuildMI(MBB, MI, DL, get(M6809::ROR8), Lo).addReg(Lo);
    if (Opc == M6809::ROR1_16)
      addWholeD(Ror);
    MI.eraseFromParent();
    return true;
  }
  if (Opc == M6809::SWAPB) {
    MachineInstrBuilder Exg = BuildMI(MBB, MI, DL, get(M6809::EXGrr))
                                  .addReg(M6809::A)
                                  .addReg(M6809::B);
    addWholeD(Exg);
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
    addWholeD(Fill);
    MI.eraseFromParent();
    return true;
  }

  if (FirstOpc) {
    bool Left = Opc == M6809::LSL16 || Opc == M6809::ROL16;
    Register First = Left ? M6809::B : M6809::A;
    Register Second = Left ? M6809::A : M6809::B;
    BuildMI(MBB, MI, DL, get(FirstOpc), First).addReg(First);
    MachineInstrBuilder MIB2 =
        BuildMI(MBB, MI, DL, get(Left ? M6809::ROL8 : M6809::ROR8), Second)
            .addReg(Second);
    addWholeD(MIB2);
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
        unsigned Byte = High ? (Imm >> 8) & 0xFF : Imm & 0xFF;
        // Bytes that leave the half alone, clear it, set it or flip it.
        if (!P.Carry) {
          bool IsAnd = P.Imm == M6809::AND8i, IsOr = P.Imm == M6809::OR8i;
          if ((IsAnd && Byte == 0xFF) || (!IsAnd && Byte == 0))
            return MIB;
          if (IsAnd && Byte == 0)
            return BuildMI(MBB, MI, DL, get(M6809::CLR8), R);
          if (IsOr && Byte == 0xFF)
            return BuildMI(MBB, MI, DL, get(M6809::LD8i), R).addImm(0xFF);
          if (!IsAnd && !IsOr && Byte == 0xFF)
            return BuildMI(MBB, MI, DL, get(M6809::COM8), R).addReg(R);
        }
        MIB = BuildMI(MBB, MI, DL, get(P.Imm), R).addReg(R).addImm(Byte);
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
    MachineInstrBuilder First = EmitHalf(!P.Carry);
    MachineInstrBuilder Last = EmitHalf(P.Carry);
    if (!Last)
      Last = First;
    if (Last)
      addWholeD(Last);
    MI.eraseFromParent();
    return true;
  }

  if (Opc == M6809::NEG16) {
    // nega; negb; sbca #0. NEGB sets C when B was not zero, which borrows
    // from the high byte.
    BuildMI(MBB, MI, DL, get(M6809::NEG8), M6809::A).addReg(M6809::A);
    BuildMI(MBB, MI, DL, get(M6809::NEG8), M6809::B).addReg(M6809::B);
    MachineInstrBuilder Sbca = BuildMI(MBB, MI, DL, get(M6809::SBC8i), M6809::A)
                                   .addReg(M6809::A)
                                   .addImm(0);
    addWholeD(Sbca);
    MI.eraseFromParent();
    return true;
  }

  if (Opc == M6809::ADD16ri) {
    Register Dst = MI.getOperand(0).getReg();
    const MachineOperand &Imm = MI.getOperand(2);
    if (Dst == M6809::D)
      BuildMI(MBB, MI, DL, get(M6809::ADDDi), Dst).addReg(Dst).add(Imm);
    else if (Dst == M6809::W)
      BuildMI(MBB, MI, DL, get(M6809::ADDWi), Dst).addReg(Dst).add(Imm);
    else
      BuildMI(MBB, MI, DL, get(M6809::LEA16), Dst)
          .addReg(Dst)
          .add(Imm)
          .addImm(M6809Mem::Auto);
    MI.eraseFromParent();
    return true;
  }

  if (Opc == M6809::SBCBRi || Opc == M6809::SBCBRm || Opc == M6809::SBCBRrr) {
    unsigned N = MI.getNumExplicitOperands();
    unsigned SbcOpc = Opc == M6809::SBCBRi   ? M6809::SBC16i
                      : Opc == M6809::SBCBRm ? M6809::SBC16m
                                             : M6809::SBC16rr;
    // Is D (still holding lhs) needed after the branch?
    LivePhysRegs Live(getRegisterInfo());
    Live.addLiveOuts(MBB);
    for (MachineBasicBlock::iterator I = MBB.end(); --I != MI.getIterator();)
      Live.stepBackward(*I);
    bool KeepD = !Live.available(MBB.getParent()->getRegInfo(), M6809::D);
    // The pushed D moves an S-relative operand along by two.
    bool SRel = SbcOpc == M6809::SBC16m && MI.getOperand(1).isReg() &&
                MI.getOperand(1).getReg() == M6809::S;
    if (KeepD)
      BuildMI(MBB, MI, DL, get(M6809::PSHS16)).addReg(M6809::D);
    MachineInstrBuilder Sbc =
        BuildMI(MBB, MI, DL, get(SbcOpc), M6809::D).addReg(M6809::D);
    for (unsigned I = 1; I + 2 < N; ++I) {
      if (KeepD && SRel && I == 2)
        addOffset(Sbc, MI.getOperand(I), 2);
      else
        Sbc.add(MI.getOperand(I));
    }
    Sbc.cloneMemRefs(MI);
    if (KeepD)
      BuildMI(MBB, MI, DL, get(M6809::PULS16), M6809::D);
    BuildMI(MBB, MI, DL, get(M6809::LBcc))
        .add(MI.getOperand(N - 2))
        .add(MI.getOperand(N - 1));
    MI.eraseFromParent();
    expandPostRAPseudo(*Sbc);
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
      if (STI.has6309()) {
        // "cmpr rhs,lhs" sets the flags for lhs - rhs.
        BuildMI(MBB, MI, DL, get(M6809::CMPR))
            .addReg(Rhs.getReg(), getKillRegState(Rhs.isKill()))
            .add(Lhs);
        break;
      }
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
  case M6809::SBCBRi:
  case M6809::SBCBRm:
  case M6809::SBCBRrr:
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
