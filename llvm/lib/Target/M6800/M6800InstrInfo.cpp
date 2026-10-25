//===-- M6800InstrInfo.cpp - M6800 Instruction Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800InstrInfo.h"
#include "M6800.h"
#include "M6800Subtarget.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "M6800GenInstrInfo.inc"

void M6800InstrInfo::anchor() {}

M6800InstrInfo::M6800InstrInfo(const M6800Subtarget &STI)
    : M6800GenInstrInfo(STI, RI, M6800::ADJCALLSTACKDOWN,
                        M6800::ADJCALLSTACKUP),
      RI(STI), STI(STI) {}

bool M6800InstrInfo::isLiveAfter(const MachineInstr &MI, MCRegister Reg) const {
  const MachineBasicBlock &MBB = *MI.getParent();
  LivePhysRegs Live(getRegisterInfo());
  Live.addLiveOuts(MBB);
  for (auto I = MBB.rbegin(); &*I != &MI; ++I)
    Live.stepBackward(*I);
  return !Live.available(MBB.getParent()->getRegInfo(), Reg);
}

// XGDX swaps D and X. Either may hold nothing yet.
static MachineInstr *buildXGDX(const M6800InstrInfo &TII,
                               MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I,
                               const DebugLoc &DL, bool UndefD, bool UndefX) {
  MachineInstr *MI = BuildMI(MBB, I, DL, TII.get(M6800::XGDX));
  for (MachineOperand &MO : MI->implicit_operands()) {
    if (MO.isDef())
      continue;
    if ((MO.getReg() == M6800::D && UndefD) ||
        (MO.getReg() == M6800::X && UndefX))
      MO.setIsUndef();
  }
  return MI;
}

void M6800InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, Register DestReg,
                                 Register SrcReg, bool KillSrc,
                                 bool RenamableDest, bool RenamableSrc) const {
  if (DestReg == SrcReg)
    return;
  // TAB and TBA set N and Z but leave C alone, so carry chains survive.
  if (DestReg == M6800::B && SrcReg == M6800::A) {
    BuildMI(MBB, I, DL, get(M6800::TAB));
    return;
  }
  if (DestReg == M6800::A && SrcReg == M6800::B) {
    BuildMI(MBB, I, DL, get(M6800::TBA));
    return;
  }
  // Between D and X: XGDX when the source dies, else through the stack.
  // Kill flags are not always kept after allocation, so ask the liveness of
  // the COPY being lowered when there is one.
  if (I != MBB.end() && I->isCopy() && I->getOperand(0).getReg() == DestReg &&
      I->getOperand(1).getReg() == SrcReg)
    KillSrc = !isLiveAfter(*I, SrcReg);
  if (DestReg == M6800::X && SrcReg == M6800::D) {
    if (KillSrc) {
      buildXGDX(*this, MBB, I, DL, false, true);
      return;
    }
    pushD(MBB, I, DL);
    BuildMI(MBB, I, DL, get(M6800::PULX), M6800::X);
    return;
  }
  if (DestReg == M6800::D && SrcReg == M6800::X) {
    if (KillSrc) {
      buildXGDX(*this, MBB, I, DL, true, false);
      return;
    }
    BuildMI(MBB, I, DL, get(M6800::PSHX)).addReg(M6800::X);
    pullD(MBB, I, DL);
    return;
  }
  llvm_unreachable("M6800: cannot copy between these registers");
}

static MachineMemOperand *getFrameMMO(MachineBasicBlock &MBB, int FI,
                                      MachineMemOperand::Flags Flags) {
  MachineFunction &MF = *MBB.getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(MF, FI),
                                 Flags, MFI.getObjectSize(FI),
                                 MFI.getObjectAlign(FI));
}

void M6800InstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  unsigned Opc = RC->hasSuperClassEq(&M6800::ACC8RegClass) ? M6800::ST8m
                                                           : M6800::ST16m;
  BuildMI(MBB, MI, DL, get(Opc))
      .addReg(SrcReg, getKillRegState(IsKill))
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addImm(M6800Mem::Auto)
      .addMemOperand(getFrameMMO(MBB, FrameIndex, MachineMemOperand::MOStore))
      .setMIFlag(Flags);
}

void M6800InstrInfo::loadRegFromStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
    int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    unsigned SubReg, MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  unsigned Opc = RC->hasSuperClassEq(&M6800::ACC8RegClass) ? M6800::LD8m
                                                           : M6800::LD16m;
  BuildMI(MBB, MI, DL, get(Opc), DestReg)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addImm(M6800Mem::Auto)
      .addMemOperand(getFrameMMO(MBB, FrameIndex, MachineMemOperand::MOLoad))
      .setMIFlag(Flags);
}

// The memory form of a pseudo whose right-hand register operand (RhsIdx) can
// be read from memory instead.
static unsigned getMemForm(unsigned Opc, unsigned &RhsIdx) {
  RhsIdx = 2;
  switch (Opc) {
  case M6800::ADD8rr: return M6800::ADD8m;
  case M6800::SUB8rr: return M6800::SUB8m;
  case M6800::AND8rr: return M6800::AND8m;
  case M6800::OR8rr: return M6800::OR8m;
  case M6800::EOR8rr: return M6800::EOR8m;
  case M6800::ADDDrr: return M6800::ADDDm;
  case M6800::SUBDrr: return M6800::SUBDm;
  case M6800::ADC16rr: return M6800::ADC16m;
  case M6800::SBC16rr: return M6800::SBC16m;
  case M6800::AND16rr: return M6800::AND16m;
  case M6800::OR16rr: return M6800::OR16m;
  case M6800::EOR16rr: return M6800::EOR16m;
  }
  RhsIdx = 1;
  switch (Opc) {
  case M6800::CB8rr: return M6800::CB8rm;
  case M6800::CB16rr: return M6800::CB16rm;
  case M6800::SBCBRrr: return M6800::SBCBRm;
  }
  return 0;
}

// A spilled right-hand operand is read from its stack slot: "subd 4,x"
// instead of reloading it and going through the stack. An 8-bit compare
// whose left-hand operand is spilled swaps its operands.
MachineInstr *M6800InstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
    int FrameIndex, MachineInstr *&CopyMI, LiveIntervals *LIS,
    VirtRegMap *VRM) const {
  if (Ops.size() != 1)
    return nullptr;
  unsigned RhsIdx;
  unsigned MemOpc = getMemForm(MI.getOpcode(), RhsIdx);
  if (!MemOpc || MI.getOperand(Ops[0]).getSubReg())
    return nullptr;
  bool Swap = MI.getOpcode() == M6800::CB8rr && Ops[0] == 0;
  if (Swap) {
    auto CC = static_cast<M6800CC::CondCode>(MI.getOperand(2).getImm());
    if (CC >= M6800CC::VC && CC <= M6800CC::MI)
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
      MIB.addFrameIndex(FrameIndex).addImm(0).addImm(M6800Mem::Auto);
      continue;
    }
    if (Swap && I == 2) { // the condition
      MIB.addImm(M6800CC::getSwapped(
          static_cast<M6800CC::CondCode>(MO.getImm())));
      continue;
    }
    MIB.add(MO);
  }
  return MIB;
}

// CLR clears C, so a rematerialized zero could break a carry chain that the
// allocator placed it inside. LD #0 leaves C alone and is always safe.
void M6800InstrInfo::reMaterialize(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   Register DestReg, unsigned SubIdx,
                                   const MachineInstr &Orig,
                                   LaneBitmask UsedLanes) const {
  if (Orig.getOpcode() != M6800::CLR8) {
    TargetInstrInfo::reMaterialize(MBB, I, DestReg, SubIdx, Orig, UsedLanes);
    return;
  }
  MachineInstr *MI = BuildMI(*MBB.getParent(), Orig.getDebugLoc(),
                             get(M6800::LD8i), DestReg)
                         .addImm(0);
  MBB.insert(I, MI);
  if (SubIdx)
    MI->getOperand(0).setSubReg(SubIdx);
}

//===----------------------------------------------------------------------===//
// Post-RA pseudo expansion
//===----------------------------------------------------------------------===//

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

// Adds the memory operand at MI's operand Idx (base, offset, mode), moved on
// by Delta bytes. When Pushed bytes have gone on the stack since, a stack
// slot moves on by that much too.
static void addMem(MachineInstrBuilder &MIB, const MachineInstr &MI,
                   unsigned Idx, int64_t Delta = 0, unsigned Pushed = 0) {
  const MachineOperand &Base = MI.getOperand(Idx);
  if (Base.isReg() && Base.getReg() == M6800::SP)
    Delta += Pushed;
  MIB.add(Base);
  addOffset(MIB, MI.getOperand(Idx + 1), Delta);
  MIB.add(MI.getOperand(Idx + 2));
}

// An "n,x" operand, for bytes just pushed and addressed after TSX.
static void addXMem(MachineInstrBuilder &MIB, int64_t Off) {
  MIB.addReg(M6800::X).addImm(Off).addImm(M6800Mem::Auto);
}

// The last of the byte instructions that make up a 16-bit operation on D
// says that all of D is now defined. It reads D too: the other byte was
// written by the instruction before, or passes through unchanged, and must
// stay live up to here.
static void addWholeD(MachineInstrBuilder &MIB) {
  MIB.addReg(M6800::D, RegState::Implicit)
      .addReg(M6800::D, RegState::ImplicitDefine);
}

void M6800InstrInfo::pushD(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator I,
                           const DebugLoc &DL) const {
  BuildMI(MBB, I, DL, get(M6800::PSH8)).addReg(M6800::B);
  BuildMI(MBB, I, DL, get(M6800::PSH8)).addReg(M6800::A);
}

void M6800InstrInfo::pullD(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator I,
                           const DebugLoc &DL) const {
  BuildMI(MBB, I, DL, get(M6800::PUL8), M6800::A);
  BuildMI(MBB, I, DL, get(M6800::PUL8), M6800::B)
      .addReg(M6800::D, RegState::ImplicitDefine);
}

// Often the instruction just before a compare with zero has already set the
// flags from R, and no test is needed at all.
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

  bool EqNe = CC == M6800CC::EQ || CC == M6800CC::NE;
  bool Sign = EqNe || CC == M6800CC::MI || CC == M6800CC::PL;
  // Signed orderings also work after instructions that clear V.
  bool VClear = Sign || CC == M6800CC::LT || CC == M6800CC::GE ||
                CC == M6800CC::GT || CC == M6800CC::LE;
  switch (Prev->getOpcode()) {
  case M6800::LD8i:
  case M6800::LD8m:
  case M6800::LD16i:
  case M6800::LD16m:
  case M6800::AND8i:
  case M6800::AND8m:
  case M6800::OR8i:
  case M6800::OR8m:
  case M6800::EOR8i:
  case M6800::EOR8m:
    return VClear; // N and Z from the result, V cleared.
  case M6800::INC8:
  case M6800::DEC8:
  case M6800::NEG8:
  case M6800::ADD8i:
  case M6800::ADD8m:
  case M6800::SUB8i:
  case M6800::SUB8m:
  case M6800::ADDDi:
  case M6800::ADDDm:
  case M6800::SUBDi:
  case M6800::SUBDm:
    return Sign; // N and Z from the result; V is a real overflow.
  default:
    return false;
  }
}

// Sets the flags for "Lhs cc 0": TST for a byte, "subd #0" for D (which
// leaves D as it was) and "cpx #0" for X.
void M6800InstrInfo::emitCompareWithZero(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         const DebugLoc &DL,
                                         const MachineOperand &Lhs,
                                         int64_t CC) const {
  Register R = Lhs.getReg();
  if (flagsAlreadySet(MBB, MI, R, CC))
    return;
  if (M6800::ACC8RegClass.contains(R))
    BuildMI(MBB, MI, DL, get(M6800::TST8)).add(Lhs);
  else if (R == M6800::D)
    BuildMI(MBB, MI, DL, get(M6800::SUBDi), R).addReg(R).addImm(0);
  else
    BuildMI(MBB, MI, DL, get(M6800::CPXi)).add(Lhs).addImm(0);
}

// The memory form of an 8-bit register-register pseudo.
static unsigned getALU8MemOpc(unsigned Opc) {
  switch (Opc) {
  case M6800::ADD8rr: return M6800::ADD8m;
  case M6800::SUB8rr: return M6800::SUB8m;
  case M6800::AND8rr: return M6800::AND8m;
  case M6800::OR8rr: return M6800::OR8m;
  case M6800::EOR8rr: return M6800::EOR8m;
  default: return 0;
  }
}

namespace {
// The 8-bit forms used to build a 16-bit operation on D. Carry operations
// (Carry set) must do the low byte first.
struct Logic16Parts {
  unsigned Imm, Mem;
  bool Carry = false;
};
} // namespace

static bool getLogic16Parts(unsigned Opc, Logic16Parts &P, char &Form) {
  switch (Opc) {
  case M6800::ADC16i: Form = 'i'; P = {M6800::ADC8i, M6800::ADC8m, true}; return true;
  case M6800::ADC16m: Form = 'm'; P = {M6800::ADC8i, M6800::ADC8m, true}; return true;
  case M6800::ADC16rr: Form = 'r'; P = {M6800::ADC8i, M6800::ADC8m, true}; return true;
  case M6800::SBC16i: Form = 'i'; P = {M6800::SBC8i, M6800::SBC8m, true}; return true;
  case M6800::SBC16m: Form = 'm'; P = {M6800::SBC8i, M6800::SBC8m, true}; return true;
  case M6800::SBC16rr: Form = 'r'; P = {M6800::SBC8i, M6800::SBC8m, true}; return true;
  case M6800::AND16i: Form = 'i'; P = {M6800::AND8i, M6800::AND8m}; return true;
  case M6800::AND16m: Form = 'm'; P = {M6800::AND8i, M6800::AND8m}; return true;
  case M6800::AND16rr: Form = 'r'; P = {M6800::AND8i, M6800::AND8m}; return true;
  case M6800::OR16i: Form = 'i'; P = {M6800::OR8i, M6800::OR8m}; return true;
  case M6800::OR16m: Form = 'm'; P = {M6800::OR8i, M6800::OR8m}; return true;
  case M6800::OR16rr: Form = 'r'; P = {M6800::OR8i, M6800::OR8m}; return true;
  case M6800::EOR16i: Form = 'i'; P = {M6800::EOR8i, M6800::EOR8m}; return true;
  case M6800::EOR16m: Form = 'm'; P = {M6800::EOR8i, M6800::EOR8m}; return true;
  case M6800::EOR16rr: Form = 'r'; P = {M6800::EOR8i, M6800::EOR8m}; return true;
  default: return false;
  }
}

bool M6800InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  unsigned Opc = MI.getOpcode();

  // 8-bit register-register: ABA/SBA for A op= B, otherwise the right
  // operand is pushed and read back through X: "pshb; tsx; anda 0,x; ins".
  if (unsigned MemOpc = getALU8MemOpc(Opc)) {
    Register Dst = MI.getOperand(0).getReg();
    const MachineOperand &Rhs = MI.getOperand(2);
    if (Dst == M6800::A && Rhs.getReg() == M6800::B &&
        (Opc == M6800::ADD8rr || Opc == M6800::SUB8rr)) {
      BuildMI(MBB, MI, DL,
              get(Opc == M6800::ADD8rr ? M6800::ABA : M6800::SBA));
    } else {
      BuildMI(MBB, MI, DL, get(M6800::PSH8))
          .addReg(Rhs.getReg(), getKillRegState(Rhs.isKill()));
      BuildMI(MBB, MI, DL, get(M6800::TSX));
      MachineInstrBuilder Op =
          BuildMI(MBB, MI, DL, get(MemOpc), Dst).addReg(Dst);
      addXMem(Op, 0);
      BuildMI(MBB, MI, DL, get(M6800::INS));
    }
    MI.eraseFromParent();
    return true;
  }

  // D op X: "pshx; tsx; addd 0,x; pulx".
  if (Opc == M6800::ADDDrr || Opc == M6800::SUBDrr) {
    BuildMI(MBB, MI, DL, get(M6800::PSHX)).add(MI.getOperand(2));
    BuildMI(MBB, MI, DL, get(M6800::TSX));
    MachineInstrBuilder Op =
        BuildMI(MBB, MI, DL,
                get(Opc == M6800::ADDDrr ? M6800::ADDDm : M6800::SUBDm),
                M6800::D)
            .addReg(M6800::D);
    addXMem(Op, 0);
    BuildMI(MBB, MI, DL, get(M6800::PULX), M6800::X);
    MI.eraseFromParent();
    return true;
  }

  switch (Opc) {
  case M6800::SEX: {
    // tba; asla; ldaa #0; sbca #0: A = 0 - 0 - (sign of B).
    BuildMI(MBB, MI, DL, get(M6800::TBA));
    BuildMI(MBB, MI, DL, get(M6800::ASL8), M6800::A).addReg(M6800::A);
    BuildMI(MBB, MI, DL, get(M6800::LD8i), M6800::A).addImm(0);
    MachineInstrBuilder Sbc = BuildMI(MBB, MI, DL, get(M6800::SBC8i), M6800::A)
                                  .addReg(M6800::A)
                                  .addImm(0);
    addWholeD(Sbc);
    MI.eraseFromParent();
    return true;
  }
  case M6800::CLRA: {
    MachineInstrBuilder Clr = BuildMI(MBB, MI, DL, get(M6800::CLR8), M6800::A);
    addWholeD(Clr);
    MI.eraseFromParent();
    return true;
  }
  case M6800::ASR16:
  case M6800::ROL16:
  case M6800::ROR16: {
    // asra; rorb / rolb; rola / rora; rorb.
    bool Left = Opc == M6800::ROL16;
    Register First = Left ? M6800::B : M6800::A;
    Register Second = Left ? M6800::A : M6800::B;
    unsigned FirstOpc = Opc == M6800::ASR16   ? M6800::ASR8
                        : Opc == M6800::ROL16 ? M6800::ROL8
                                              : M6800::ROR8;
    BuildMI(MBB, MI, DL, get(FirstOpc), First).addReg(First);
    MachineInstrBuilder MIB2 =
        BuildMI(MBB, MI, DL, get(Left ? M6800::ROL8 : M6800::ROR8), Second)
            .addReg(Second);
    addWholeD(MIB2);
    MI.eraseFromParent();
    return true;
  }
  case M6800::NEG16: {
    // nega; negb; sbca #0. NEGB sets C when B was not zero, which borrows
    // from the high byte.
    BuildMI(MBB, MI, DL, get(M6800::NEG8), M6800::A).addReg(M6800::A);
    BuildMI(MBB, MI, DL, get(M6800::NEG8), M6800::B).addReg(M6800::B);
    MachineInstrBuilder Sbca = BuildMI(MBB, MI, DL, get(M6800::SBC8i), M6800::A)
                                   .addReg(M6800::A)
                                   .addImm(0);
    addWholeD(Sbca);
    MI.eraseFromParent();
    return true;
  }
  case M6800::SHL16_8:
  case M6800::LSR16_8:
  case M6800::ASR16_8: {
    // tba; clrb / tab; clra / tab; asla; ldaa #0; sbca #0.
    MachineInstrBuilder Fill;
    if (Opc == M6800::SHL16_8) {
      BuildMI(MBB, MI, DL, get(M6800::TBA));
      Fill = BuildMI(MBB, MI, DL, get(M6800::CLR8), M6800::B);
    } else {
      BuildMI(MBB, MI, DL, get(M6800::TAB));
      if (Opc == M6800::LSR16_8) {
        Fill = BuildMI(MBB, MI, DL, get(M6800::CLR8), M6800::A);
      } else {
        BuildMI(MBB, MI, DL, get(M6800::ASL8), M6800::A).addReg(M6800::A);
        BuildMI(MBB, MI, DL, get(M6800::LD8i), M6800::A).addImm(0);
        Fill = BuildMI(MBB, MI, DL, get(M6800::SBC8i), M6800::A)
                   .addReg(M6800::A)
                   .addImm(0);
      }
    }
    addWholeD(Fill);
    MI.eraseFromParent();
    return true;
  }
  case M6800::ADD16ri: {
    Register Dst = MI.getOperand(0).getReg();
    const MachineOperand &Imm = MI.getOperand(2);
    if (Dst == M6800::D) {
      BuildMI(MBB, MI, DL, get(M6800::ADDDi), Dst).addReg(Dst).add(Imm);
    } else if (Imm.isImm() && Imm.getImm() >= -3 && Imm.getImm() <= 3) {
      // inx/dex, one byte each. They set only Z, which nothing reads.
      int64_t N = Imm.getImm();
      for (int64_t K = N < 0 ? -N : N; K; --K)
        BuildMI(MBB, MI, DL, get(N < 0 ? M6800::DEX : M6800::INX));
    } else {
      // xgdx; addd #n; xgdx leaves D as it was.
      bool DLive = isLiveAfter(MI, M6800::D);
      buildXGDX(*this, MBB, MI, DL, !DLive, false);
      BuildMI(MBB, MI, DL, get(M6800::ADDDi), M6800::D)
          .addReg(M6800::D)
          .add(Imm);
      buildXGDX(*this, MBB, MI, DL, false, false);
    }
    MI.eraseFromParent();
    return true;
  }
  default:
    break;
  }

  Logic16Parts P;
  char Form;
  if (getLogic16Parts(Opc, P, Form)) {
    // D = A:B, so the high byte is done on A and the low byte on B. Big-endian
    // memory puts the high byte first. Carry operations go low byte first.
    // The register form reads X's bytes from the stack: "pshx; tsx; anda
    // 0,x; andb 1,x; pulx".
    auto EmitHalf = [&](bool High) {
      Register R = High ? M6800::A : M6800::B;
      MachineInstrBuilder MIB;
      switch (Form) {
      case 'i': {
        int64_t Imm = MI.getOperand(2).getImm();
        unsigned Byte = High ? (Imm >> 8) & 0xFF : Imm & 0xFF;
        // Bytes that leave the half alone, clear it, set it or flip it.
        if (!P.Carry) {
          bool IsAnd = P.Imm == M6800::AND8i, IsOr = P.Imm == M6800::OR8i;
          if ((IsAnd && Byte == 0xFF) || (!IsAnd && Byte == 0))
            return MIB;
          if (IsAnd && Byte == 0)
            return BuildMI(MBB, MI, DL, get(M6800::CLR8), R);
          if (IsOr && Byte == 0xFF)
            return BuildMI(MBB, MI, DL, get(M6800::LD8i), R).addImm(0xFF);
          if (!IsAnd && !IsOr && Byte == 0xFF)
            return BuildMI(MBB, MI, DL, get(M6800::COM8), R).addReg(R);
        }
        MIB = BuildMI(MBB, MI, DL, get(P.Imm), R).addReg(R).addImm(Byte);
        break;
      }
      case 'm':
        MIB = BuildMI(MBB, MI, DL, get(P.Mem), R).addReg(R);
        addMem(MIB, MI, 2, High ? 0 : 1);
        MIB.cloneMemRefs(MI);
        break;
      default:
        MIB = BuildMI(MBB, MI, DL, get(P.Mem), R).addReg(R);
        addXMem(MIB, High ? 0 : 1);
        break;
      }
      return MIB;
    };

    if (Form == 'r') {
      BuildMI(MBB, MI, DL, get(M6800::PSHX)).add(MI.getOperand(2));
      BuildMI(MBB, MI, DL, get(M6800::TSX));
    }
    MachineInstrBuilder First = EmitHalf(!P.Carry);
    MachineInstrBuilder Last = EmitHalf(P.Carry);
    if (!Last)
      Last = First;
    if (Last)
      addWholeD(Last);
    if (Form == 'r')
      BuildMI(MBB, MI, DL, get(M6800::PULX), M6800::X);
    MI.eraseFromParent();
    return true;
  }

  if (Opc == M6800::SBCBRi || Opc == M6800::SBCBRm || Opc == M6800::SBCBRrr) {
    unsigned N = MI.getNumExplicitOperands();
    unsigned SbcOpc = Opc == M6800::SBCBRi   ? M6800::SBC16i
                      : Opc == M6800::SBCBRm ? M6800::SBC16m
                                             : M6800::SBC16rr;
    // The subtraction overwrites D. Keep it when it is needed after the
    // branch; pulls leave the flags alone.
    bool KeepD = isLiveAfter(MI, M6800::D);
    if (KeepD)
      pushD(MBB, MI, DL);
    MachineInstrBuilder Sbc =
        BuildMI(MBB, MI, DL, get(SbcOpc), M6800::D).addReg(M6800::D);
    if (Opc == M6800::SBCBRm)
      addMem(Sbc, MI, 1, 0, KeepD ? 2 : 0);
    else
      Sbc.add(MI.getOperand(1));
    Sbc.cloneMemRefs(MI);
    if (KeepD)
      pullD(MBB, MI, DL);
    BuildMI(MBB, MI, DL, get(M6800::Bcc))
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
    Register L = Lhs.getReg();

    switch (Opc) {
    case M6800::CB8ri:
      if (MI.getOperand(1).getImm() == 0)
        emitCompareWithZero(MBB, MI, DL, Lhs, CC);
      else
        BuildMI(MBB, MI, DL, get(M6800::CMP8i)).add(Lhs).add(MI.getOperand(1));
      break;
    case M6800::CB16ri:
      if (MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 0) {
        emitCompareWithZero(MBB, MI, DL, Lhs, CC);
      } else if (L == M6800::X) {
        BuildMI(MBB, MI, DL, get(M6800::CPXi)).add(Lhs).add(MI.getOperand(1));
      } else if (!isLiveAfter(MI, M6800::D)) {
        BuildMI(MBB, MI, DL, get(M6800::SUBDi), M6800::D)
            .addReg(M6800::D)
            .add(MI.getOperand(1));
      } else {
        // xgdx; cpx #n; xgdx. XGDX leaves the flags alone.
        bool XLive = isLiveAfter(MI, M6800::X);
        buildXGDX(*this, MBB, MI, DL, false, !XLive);
        BuildMI(MBB, MI, DL, get(M6800::CPXi))
            .addReg(M6800::X)
            .add(MI.getOperand(1));
        buildXGDX(*this, MBB, MI, DL, false, false);
      }
      break;
    case M6800::CB8rm: {
      MachineInstrBuilder Cmp = BuildMI(MBB, MI, DL, get(M6800::CMP8m)).add(Lhs);
      addMem(Cmp, MI, 1);
      Cmp.cloneMemRefs(MI);
      break;
    }
    case M6800::CB16rm: {
      // D against memory: SUBD when D dies; "xgdx; cpx mem; xgdx" when the
      // address does not need X; otherwise D is pushed and pulled around
      // SUBD.
      bool DLive = isLiveAfter(MI, M6800::D);
      const MachineOperand &Base = MI.getOperand(1);
      bool NeedsX = Base.isReg() && Base.getReg();
      if (DLive && !NeedsX) {
        bool XLive = isLiveAfter(MI, M6800::X);
        buildXGDX(*this, MBB, MI, DL, false, !XLive);
        MachineInstrBuilder Cmp =
            BuildMI(MBB, MI, DL, get(M6800::CPXm)).addReg(M6800::X);
        addMem(Cmp, MI, 1);
        Cmp.cloneMemRefs(MI);
        buildXGDX(*this, MBB, MI, DL, false, false);
        break;
      }
      if (DLive)
        pushD(MBB, MI, DL);
      MachineInstrBuilder Sub =
          BuildMI(MBB, MI, DL, get(M6800::SUBDm), M6800::D).addReg(M6800::D);
      addMem(Sub, MI, 1, 0, DLive ? 2 : 0);
      Sub.cloneMemRefs(MI);
      if (DLive)
        pullD(MBB, MI, DL);
      break;
    }
    case M6800::CB8rr: {
      Register R = MI.getOperand(1).getReg();
      if (L == M6800::A && R == M6800::B) {
        BuildMI(MBB, MI, DL, get(M6800::CBA));
      } else if (L == M6800::B && R == M6800::A) {
        BuildMI(MBB, MI, DL, get(M6800::CBA));
        CC = M6800CC::getSwapped(static_cast<M6800CC::CondCode>(CC));
      } else {
        // The same register on both sides, through the stack. X is kept.
        bool XLive = isLiveAfter(MI, M6800::X);
        BuildMI(MBB, MI, DL, get(M6800::PSH8)).addReg(R);
        if (XLive)
          BuildMI(MBB, MI, DL, get(M6800::PSHX)).addReg(M6800::X);
        BuildMI(MBB, MI, DL, get(M6800::TSX));
        MachineInstrBuilder Cmp =
            BuildMI(MBB, MI, DL, get(M6800::CMP8m)).addReg(L);
        addXMem(Cmp, XLive ? 2 : 0);
        if (XLive)
          BuildMI(MBB, MI, DL, get(M6800::PULX), M6800::X);
        BuildMI(MBB, MI, DL, get(M6800::INS));
      }
      break;
    }
    case M6800::CB16rr: {
      // D against X: "pshx; tsx; subd 0,x; pulx", with D kept around it
      // when needed.
      bool DLive = isLiveAfter(MI, M6800::D);
      if (DLive)
        pushD(MBB, MI, DL);
      BuildMI(MBB, MI, DL, get(M6800::PSHX)).add(MI.getOperand(1));
      BuildMI(MBB, MI, DL, get(M6800::TSX));
      MachineInstrBuilder Sub =
          BuildMI(MBB, MI, DL, get(M6800::SUBDm), M6800::D).addReg(M6800::D);
      addXMem(Sub, 0);
      BuildMI(MBB, MI, DL, get(M6800::PULX), M6800::X);
      if (DLive)
        pullD(MBB, MI, DL);
      break;
    }
    default:
      llvm_unreachable("unknown compare-and-branch");
    }
    BuildMI(MBB, MI, DL, get(M6800::Bcc)).addImm(CC).addMBB(Dest);
    MI.eraseFromParent();
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Branch analysis
//===----------------------------------------------------------------------===//

bool M6800InstrInfo::isCompareAndBranch(unsigned Opc) {
  switch (Opc) {
  case M6800::CB8ri:
  case M6800::CB8rm:
  case M6800::CB8rr:
  case M6800::CB16ri:
  case M6800::CB16rm:
  case M6800::CB16rr:
  case M6800::SBCBRi:
  case M6800::SBCBRm:
  case M6800::SBCBRrr:
    return true;
  default:
    return false;
  }
}

static bool isCondBranch(unsigned Opc) {
  return Opc == M6800::Bcc || M6800InstrInfo::isCompareAndBranch(Opc);
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

bool M6800InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
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
    if (Opc == M6800::BRA) {
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

unsigned M6800InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  unsigned Count = 0;
  int Bytes = 0;
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (I->getOpcode() != M6800::BRA && !isCondBranch(I->getOpcode()))
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

unsigned M6800InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                      MachineBasicBlock *TBB,
                                      MachineBasicBlock *FBB,
                                      ArrayRef<MachineOperand> Cond,
                                      const DebugLoc &DL,
                                      int *BytesAdded) const {
  assert(TBB && "insertBranch needs a target");
  int Bytes = 0;

  if (Cond.empty()) {
    assert(!FBB && "unconditional branch with two targets");
    MachineInstr &MI = *BuildMI(&MBB, DL, get(M6800::BRA)).addMBB(TBB);
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
  MachineInstr &MI = *BuildMI(&MBB, DL, get(M6800::BRA)).addMBB(FBB);
  Bytes += getInstSizeInBytes(MI);
  if (BytesAdded)
    *BytesAdded = Bytes;
  return 2;
}

bool M6800InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  MachineOperand &CC = Cond.back();
  CC.setImm(M6800CC::getOpposite(static_cast<M6800CC::CondCode>(CC.getImm())));
  return false;
}
