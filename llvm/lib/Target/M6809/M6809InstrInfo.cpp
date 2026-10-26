//===-- M6809InstrInfo.cpp - M6809 Instruction Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809InstrInfo.h"
#include "M6809.h"
#include "M6809FrameLowering.h"
#include "M6809MachineFunctionInfo.h"
#include "M6809MCInstLower.h"
#include "M6809Subtarget.h"
#include "MCTargetDesc/M6809OpcodeTable.h"
#include "TargetInfo/M6809TargetInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
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

// Spill code goes where the flags are not live, so the flags the load or
// store sets are dead.
static void markFlagsDead(MachineInstr &MI) {
  for (MachineOperand &MO : MI.implicit_operands())
    if (MO.isDef() && MO.getReg() == M6809::CC)
      MO.setIsDead();
}

// ST16m and LD16m take GR16, which leaves out S. A virtual register of a
// class with S in it, such as PTR16, is narrowed to the registers both
// classes have.
static void constrainForSpill(MachineBasicBlock &MBB, Register Reg, bool Is8) {
  if (Is8 || !Reg.isVirtual())
    return;
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  [[maybe_unused]] const TargetRegisterClass *RC =
      MRI.constrainRegClass(Reg, &M6809::GR16RegClass);
  assert(RC && "M6809: no 16-bit spill class for this register");
}

void M6809InstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  bool Is8 = RC->hasSuperClassEq(&M6809::ACC8RegClass);
  constrainForSpill(MBB, SrcReg, Is8);
  MachineInstr *Store =
      BuildMI(MBB, MI, DL, get(Is8 ? M6809::ST8m : M6809::ST16m))
          .addReg(SrcReg, getKillRegState(IsKill))
          .addFrameIndex(FrameIndex)
          .addImm(0)
          .addImm(M6809Mem::Auto)
          .addMemOperand(
              getFrameMMO(MBB, FrameIndex, MachineMemOperand::MOStore))
          .setMIFlag(Flags);
  markFlagsDead(*Store);
}

void M6809InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator MI,
                                          Register DestReg, int FrameIndex,
                                          const TargetRegisterClass *RC,
                                          Register VReg, unsigned SubReg,
                                          MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  bool Is8 = RC->hasSuperClassEq(&M6809::ACC8RegClass);
  constrainForSpill(MBB, DestReg, Is8);
  MachineInstr *Load =
      BuildMI(MBB, MI, DL, get(Is8 ? M6809::LD8m : M6809::LD16m), DestReg)
          .addFrameIndex(FrameIndex)
          .addImm(0)
          .addImm(M6809Mem::Auto)
          .addMemOperand(
              getFrameMMO(MBB, FrameIndex, MachineMemOperand::MOLoad))
          .setMIFlag(Flags);
  markFlagsDead(*Load);
}

// Does MI load or store the whole of a stack slot? Operand 1 is the base,
// operand 2 the offset.
static bool isWholeSlot(const MachineInstr &MI, unsigned Bytes,
                        int &FrameIndex) {
  const MachineOperand &Base = MI.getOperand(1);
  const MachineOperand &Offset = MI.getOperand(2);
  if (!Base.isFI() || !Offset.isImm() || Offset.getImm() != 0)
    return false;
  const MachineFrameInfo &MFI = MI.getMF()->getFrameInfo();
  if (MFI.getObjectSize(Base.getIndex()) != Bytes)
    return false;
  FrameIndex = Base.getIndex();
  return true;
}

Register M6809InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                             int &FrameIndex) const {
  unsigned Opc = MI.getOpcode();
  if ((Opc == M6809::LD8m || Opc == M6809::LD16m) &&
      isWholeSlot(MI, Opc == M6809::LD8m ? 1 : 2, FrameIndex))
    return MI.getOperand(0).getReg();
  return Register();
}

Register M6809InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                            int &FrameIndex) const {
  unsigned Opc = MI.getOpcode();
  if ((Opc == M6809::ST8m || Opc == M6809::ST16m) &&
      isWholeSlot(MI, Opc == M6809::ST8m ? 1 : 2, FrameIndex))
    return MI.getOperand(0).getReg();
  return Register();
}

// The memory form of a pseudo whose right-hand register operand (RhsIdx) can
// be read from memory instead.
static unsigned getMemForm(unsigned Opc, unsigned &RhsIdx) {
  RhsIdx = 2;
  switch (Opc) {
  case M6809::ADD8rr:
    return M6809::ADD8m;
  case M6809::SUB8rr:
    return M6809::SUB8m;
  case M6809::AND8rr:
    return M6809::AND8m;
  case M6809::OR8rr:
    return M6809::OR8m;
  case M6809::EOR8rr:
    return M6809::EOR8m;
  case M6809::ADDDrr:
    return M6809::ADDDm;
  case M6809::SUBDrr:
    return M6809::SUBDm;
  case M6809::ADC16rr:
    return M6809::ADC16m;
  case M6809::SBC16rr:
    return M6809::SBC16m;
  case M6809::AND16rr:
    return M6809::AND16m;
  case M6809::OR16rr:
    return M6809::OR16m;
  case M6809::EOR16rr:
    return M6809::EOR16m;
  }
  RhsIdx = 1;
  switch (Opc) {
  case M6809::CB8rr:
    return M6809::CB8rm;
  case M6809::CB16rr:
    return M6809::CB16rm;
  case M6809::SBCBRrr:
    return M6809::SBCBRm;
  }
  return 0;
}

// A spilled right-hand operand is read from its stack slot: "subd 4,s"
// instead of "ldx 4,s; pshs x; subd ,s++". A compare whose left-hand
// operand is spilled swaps its operands.
MachineInstr *
M6809InstrInfo::foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                                      ArrayRef<unsigned> Ops, int FrameIndex,
                                      MachineInstr *&CopyMI, LiveIntervals *LIS,
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
      MIB.addImm(
          M6809CC::getSwapped(static_cast<M6809CC::CondCode>(MO.getImm())));
      continue;
    }
    MIB.add(MO);
  }
  return MIB;
}

// Constant loads define CC, which the generic check treats as a reason not
// to rematerialize. The only flag live between instructions before register
// allocation is the borrow that SBCBR* reads, and LD #imm leaves C alone.
bool M6809InstrInfo::isReMaterializableImpl(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case M6809::LD8i:
  case M6809::CLR8:
    return true;
  case M6809::LD16i:
    // Three or four bytes against two for a reload from the frame.
    return !MI.getMF()->getFunction().hasOptSize();
  }
  return TargetInstrInfo::isReMaterializableImpl(MI);
}

// Instructions that keep C although they are modelled as writing CC: the
// loads, stores and moves the register allocator inserts, which can land
// inside a carry chain. Their CC defs are marked dead even when C flows
// through them.
static bool keepsCarry(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case M6809::LD8i:
  case M6809::LD16i:
  case M6809::LD8m:
  case M6809::LD16m:
  case M6809::ST8m:
  case M6809::ST16m:
  case M6809::LEA16:
    return true;
  default:
    return false;
  }
}

bool M6809InstrInfo::isCarryReadFrom(const MachineBasicBlock &MBB,
                                     MachineBasicBlock::const_iterator I) {
  for (MachineBasicBlock::const_iterator E = MBB.end(); I != E; ++I) {
    if (I->readsRegister(M6809::CC, /*TRI=*/nullptr))
      return true;
    if (I->definesRegister(M6809::CC, /*TRI=*/nullptr) && !keepsCarry(*I))
      return false;
  }
  for (const MachineBasicBlock *Succ : MBB.successors())
    if (Succ->isLiveIn(M6809::CC))
      return true;
  return false;
}

// CLR clears C, so a rematerialized zero could break a carry chain that the
// allocator placed it inside. LD #0 leaves C alone: use it unless CC is
// known to be dead.
void M6809InstrInfo::reMaterialize(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   Register DestReg, unsigned SubIdx,
                                   const MachineInstr &Orig,
                                   LaneBitmask UsedLanes) const {
  if (Orig.getOpcode() != M6809::CLR8 || !isCarryReadFrom(MBB, I)) {
    TargetInstrInfo::reMaterialize(MBB, I, DestReg, SubIdx, Orig, UsedLanes);
    return;
  }
  MachineInstr *MI =
      BuildMI(*MBB.getParent(), Orig.getDebugLoc(), get(M6809::LD8i), DestReg)
          .addImm(0);
  MI->getOperand(MI->getNumOperands() - 1).setIsDead(); // the CC def
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
  case M6809::ADD8rr:
    return M6809::ADD8pop;
  case M6809::SUB8rr:
    return M6809::SUB8pop;
  case M6809::AND8rr:
    return M6809::AND8pop;
  case M6809::OR8rr:
    return M6809::OR8pop;
  case M6809::EOR8rr:
    return M6809::EOR8pop;
  case M6809::ADDDrr:
    return M6809::ADDDpop;
  case M6809::SUBDrr:
    return M6809::SUBDpop;
  default:
    return 0;
  }
}

// The memory forms of the pseudos getPopOpcode handles.
static unsigned getScratchOpcode(unsigned Opc) {
  switch (Opc) {
  case M6809::ADD8rr:
    return M6809::ADD8m;
  case M6809::SUB8rr:
    return M6809::SUB8m;
  case M6809::AND8rr:
    return M6809::AND8m;
  case M6809::OR8rr:
    return M6809::OR8m;
  case M6809::EOR8rr:
    return M6809::EOR8m;
  case M6809::ADDDrr:
    return M6809::ADDDm;
  case M6809::SUBDrr:
    return M6809::SUBDm;
  default:
    return 0;
  }
}

bool M6809InstrInfo::usesScratchSlot(unsigned Opc) {
  return getScratchOpcode(Opc) || Opc == M6809::CB8rr || Opc == M6809::CB16rr;
}

// Stores Rhs in the function's scratch slot, if it has one, and returns the
// slot's address as base and offset.
static bool storeToScratch(MachineInstr &MI, const MachineOperand &Rhs,
                           const TargetInstrInfo &TII, Register &Base,
                           int64_t &Off) {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  int FI = MF.getInfo<M6809FunctionInfo>()->getScratchFrameIndex();
  if (FI < 0)
    return false;
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  Base = MF.getSubtarget().getRegisterInfo()->getFrameRegister(MF);
  Off =
      MFI.getObjectOffset(FI) +
      (Base == M6809::U ? M6809FrameLowering::FPSaveSize : MFI.getStackSize());
  bool Is8 = M6809::ACC8RegClass.contains(Rhs.getReg());
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(Is8 ? M6809::ST8m : M6809::ST16m))
      .addReg(Rhs.getReg()) // may also be the other operand
      .addReg(Base)
      .addImm(Off)
      .addImm(M6809Mem::Auto);
  return true;
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
  case M6809::ADC16i:
    Form = 'i';
    P = {M6809::ADC8i, M6809::ADC8m, M6809::ADC8pop2, true};
    return true;
  case M6809::ADC16m:
    Form = 'm';
    P = {M6809::ADC8i, M6809::ADC8m, M6809::ADC8pop2, true};
    return true;
  case M6809::ADC16rr:
    Form = 'r';
    P = {M6809::ADC8i, M6809::ADC8m, M6809::ADC8pop2, true};
    return true;
  case M6809::SBC16i:
    Form = 'i';
    P = {M6809::SBC8i, M6809::SBC8m, M6809::SBC8pop2, true};
    return true;
  case M6809::SBC16m:
    Form = 'm';
    P = {M6809::SBC8i, M6809::SBC8m, M6809::SBC8pop2, true};
    return true;
  case M6809::SBC16rr:
    Form = 'r';
    P = {M6809::SBC8i, M6809::SBC8m, M6809::SBC8pop2, true};
    return true;
  case M6809::AND16i:
    Form = 'i';
    P = {M6809::AND8i, M6809::AND8m, M6809::AND8pop};
    return true;
  case M6809::AND16m:
    Form = 'm';
    P = {M6809::AND8i, M6809::AND8m, M6809::AND8pop};
    return true;
  case M6809::AND16rr:
    Form = 'r';
    P = {M6809::AND8i, M6809::AND8m, M6809::AND8pop};
    return true;
  case M6809::OR16i:
    Form = 'i';
    P = {M6809::OR8i, M6809::OR8m, M6809::OR8pop};
    return true;
  case M6809::OR16m:
    Form = 'm';
    P = {M6809::OR8i, M6809::OR8m, M6809::OR8pop};
    return true;
  case M6809::OR16rr:
    Form = 'r';
    P = {M6809::OR8i, M6809::OR8m, M6809::OR8pop};
    return true;
  case M6809::EOR16i:
    Form = 'i';
    P = {M6809::EOR8i, M6809::EOR8m, M6809::EOR8pop};
    return true;
  case M6809::EOR16m:
    Form = 'm';
    P = {M6809::EOR8i, M6809::EOR8m, M6809::EOR8pop};
    return true;
  case M6809::EOR16rr:
    Form = 'r';
    P = {M6809::EOR8i, M6809::EOR8m, M6809::EOR8pop};
    return true;
  default:
    return false;
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
// Register moves and pushes leave the flags alone; they may sit between
// the instruction that set the flags from R and the test, as long as they
// do not write R.
static bool keepsFlags(const MachineInstr &MI, Register R,
                       const TargetRegisterInfo &TRI) {
  if (MI.isDebugInstr() || MI.isKill())
    return true;
  switch (MI.getOpcode()) {
  case TargetOpcode::COPY:
  case M6809::TFR8:
  case M6809::TFR16:
  case M6809::PSHS8:
  case M6809::PSHS16:
    return !MI.modifiesRegister(R, &TRI) &&
           !MI.modifiesRegister(M6809::CC, &TRI);
  default:
    return false;
  }
}

// Whether Def, which writes R, leaves the flags as "R cc 0" would.
static bool setsFlagsFor(const MachineInstr &Def, Register R, int64_t CC) {
  if (Def.getNumExplicitDefs() != 1 || !Def.getOperand(0).isReg() ||
      Def.getOperand(0).getReg() != R)
    return false;

  bool EqNe = CC == M6809CC::EQ || CC == M6809CC::NE;
  bool Sign = EqNe || CC == M6809CC::MI || CC == M6809CC::PL;
  // Signed orderings also work after instructions that clear V.
  bool VClear = Sign || CC == M6809CC::LT || CC == M6809CC::GE ||
                CC == M6809CC::GT || CC == M6809CC::LE;
  switch (Def.getOpcode()) {
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

// Whether Def can move down past I: neither reads or writes what the
// other writes, and I does not read the flags Def sets. Only the flags I
// sets are lost, which the test does not want.
static bool canSinkPast(const MachineInstr &Def, const MachineInstr &I,
                        const TargetRegisterInfo &TRI) {
  if (I.isDebugInstr())
    return true;
  // LEA and TFR have no pattern, so they are marked as having side
  // effects; they have none.
  bool Pure = I.getOpcode() == M6809::LEA16 || I.getOpcode() == M6809::TFR8 ||
              I.getOpcode() == M6809::TFR16 || !I.hasUnmodeledSideEffects();
  if (!Pure || I.isCall() || I.isTerminator() || I.isInlineAsm() ||
      I.readsRegister(M6809::CC, &TRI))
    return false;
  if (Def.mayLoadOrStore() && I.mayLoadOrStore())
    return false;
  for (const MachineOperand &MO : Def.operands()) {
    if (!MO.isReg() || !MO.getReg() || MO.getReg() == M6809::CC)
      continue;
    // I may not read or write any register Def reads or writes.
    if (I.readsRegister(MO.getReg(), &TRI) ||
        I.modifiesRegister(MO.getReg(), &TRI))
      return false;
  }
  return true;
}

// Often an instruction before the test has already set the flags from R,
// and no test is needed at all. If one or two independent instructions
// (such as the LEAX of a pointer after the LEAY of a counter) overwrote
// those flags since, the instruction moves down to the test.
static bool flagsAlreadySet(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, Register R,
                            int64_t CC) {
  const TargetRegisterInfo &TRI =
      *MBB.getParent()->getSubtarget().getRegisterInfo();
  SmallVector<MachineInstr *, 4> Between;
  unsigned Clobbers = 0;
  for (MachineBasicBlock::iterator I = MI; I != MBB.begin();) {
    --I;
    if (keepsFlags(*I, R, TRI)) {
      Between.push_back(&*I);
      continue;
    }
    if (I->modifiesRegister(R, &TRI)) {
      if (!setsFlagsFor(*I, R, CC))
        return false;
      if (!Clobbers)
        return true;
      if (!all_of(Between,
                  [&](MachineInstr *B) { return canSinkPast(*I, *B, TRI); }))
        return false;
      MBB.splice(MI, &MBB, I);
      return true;
    }
    if (++Clobbers > 2 || Between.size() > 6)
      return false;
    Between.push_back(&*I);
  }
  return false;
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
  // TST leaves C alone, so it serves every condition but the unsigned ones.
  bool Unsigned = CC == M6809CC::HI || CC == M6809CC::LS || CC == M6809CC::HS ||
                  CC == M6809CC::LO;
  if (M6809::ACC8RegClass.contains(R)) {
    if (Unsigned)
      BuildMI(MBB, MI, DL, get(M6809::CMP8i)).add(Lhs).addImm(0);
    else
      BuildMI(MBB, MI, DL, get(M6809::TST8)).add(Lhs);
  } else if (R == M6809::D && STI.has6309() && !Unsigned) {
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
  case M6809::AND16m:
    MemOpc = M6809::ANDDm;
    break;
  case M6809::OR16m:
    MemOpc = M6809::ORDm;
    break;
  case M6809::EOR16m:
    MemOpc = M6809::EORDm;
    break;
  case M6809::ADC16m:
    MemOpc = M6809::ADCDm;
    break;
  case M6809::SBC16m:
    MemOpc = M6809::SBCDm;
    break;
  default:
    break;
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

  // Block copy and fill: the length goes in W, then TFM.
  if (Opc == M6809::MEMCPY16 || Opc == M6809::MEMCPY16i ||
      Opc == M6809::MEMSET16 || Opc == M6809::MEMSET16i) {
    bool Cpy = Opc == M6809::MEMCPY16 || Opc == M6809::MEMCPY16i;
    Register Dst = MI.getOperand(0).getReg();
    if (!Cpy)
      BuildMI(MBB, MI, DL, get(M6809::PSHS8)).add(MI.getOperand(2));
    const MachineOperand &Len = MI.getOperand(Cpy ? 4 : 3);
    if (Len.isImm())
      BuildMI(MBB, MI, DL, get(M6809::LD16i), M6809::W).add(Len);
    else if (Len.getReg() != M6809::W)
      BuildMI(MBB, MI, DL, get(M6809::TFRrr))
          .addReg(Len.getReg(), getKillRegState(Len.isKill()))
          .addReg(M6809::W, RegState::Undef)
          .addReg(M6809::W, RegState::ImplicitDefine);
    if (Cpy) {
      Register Src = MI.getOperand(1).getReg();
      BuildMI(MBB, MI, DL, get(M6809::TFMpp))
          .addReg(Src)
          .addReg(Dst)
          .addReg(Src, RegState::ImplicitDefine)
          .addReg(Dst, RegState::ImplicitDefine);
    } else {
      BuildMI(MBB, MI, DL, get(M6809::TFMnp))
          .addReg(M6809::S)
          .addReg(Dst)
          .addReg(Dst, RegState::ImplicitDefine);
      BuildMI(MBB, MI, DL, get(M6809::LEASi)).addImm(1);
    }
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
  case M6809::NEG16:
    InhOpc = M6809::NEG16r;
    break;
  case M6809::LSL16:
    InhOpc = M6809::ASL16r;
    break;
  case M6809::LSR16:
    InhOpc = M6809::LSR16r;
    break;
  case M6809::ASR16:
    InhOpc = M6809::ASR16r;
    break;
  case M6809::ROL16:
    InhOpc = M6809::ROL16r;
    break;
  case M6809::ROR16:
    InhOpc = M6809::ROR16r;
    break;
  default:
    break;
  }
  if (InhOpc) {
    BuildMI(MBB, MI, DL, get(InhOpc), M6809::D)
        .addReg(M6809::D, getUndefRegState(MI.getOperand(1).isUndef()));
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
    Register Base;
    int64_t Off;
    if (storeToScratch(MI, Rhs, *this, Base, Off)) {
      BuildMI(MBB, MI, DL, get(getScratchOpcode(Opc)), Dst)
          .addReg(Dst)
          .addReg(Base)
          .addImm(Off)
          .addImm(M6809Mem::Auto);
      MI.eraseFromParent();
      return true;
    }
    bool Is8 = M6809::ACC8RegClass.contains(Dst);
    BuildMI(MBB, MI, DL, get(Is8 ? M6809::PSHS8 : M6809::PSHS16))
        .addReg(Rhs.getReg(), getKillRegState(Rhs.isKill()));
    BuildMI(MBB, MI, DL, get(PopOpc), Dst).addReg(Dst);
    MI.eraseFromParent();
    return true;
  }

  // One-bit rotates (see M6809InstrInfo.td).
  if (Opc == M6809::ROL1_8 || Opc == M6809::ROL1_16) {
    Register Lo =
        Opc == M6809::ROL1_8 ? MI.getOperand(0).getReg() : Register(M6809::B);
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
    Register Lo =
        Opc == M6809::ROR1_8 ? MI.getOperand(0).getReg() : Register(M6809::B);
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
  if (Opc == M6809::SIGN8) {
    // asl r; ld r,#0; sbc r,#0: r = 0 - C. The load keeps C, which the
    // implicit use of CC says.
    Register R = MI.getOperand(0).getReg();
    BuildMI(MBB, MI, DL, get(M6809::ASL8), R).addReg(R);
    BuildMI(MBB, MI, DL, get(M6809::LD8i), R)
        .addImm(0)
        .addReg(M6809::CC, RegState::Implicit);
    BuildMI(MBB, MI, DL, get(M6809::SBC8i), R).addReg(R).addImm(0);
    MI.eraseFromParent();
    return true;
  }
  if (Opc == M6809::SIGN16) {
    // [tfr src,d;] tfr a,b; sex; tfr a,b. After "tfr d,src", D still holds
    // the value.
    Register Src = MI.getOperand(1).getReg();
    if (Src != M6809::D) {
      MachineInstr *Prev = MI.getPrevNode();
      bool InD = Prev && Prev->getOpcode() == M6809::TFR16 &&
                 Prev->getOperand(0).getReg() == Src &&
                 Prev->getOperand(1).getReg() == M6809::D;
      if (InD)
        Prev->getOperand(1).setIsKill(false);
      else
        BuildMI(MBB, MI, DL, get(M6809::TFR16), M6809::D)
            .addReg(Src, getKillRegState(MI.getOperand(1).isKill()));
    }
    BuildMI(MBB, MI, DL, get(M6809::TFR8), M6809::B).addReg(M6809::A);
    BuildMI(MBB, MI, DL, get(M6809::SEX), M6809::D).addReg(M6809::D);
    MachineInstrBuilder Tfr =
        BuildMI(MBB, MI, DL, get(M6809::TFR8), M6809::B).addReg(M6809::A);
    addWholeD(Tfr);
    MI.eraseFromParent();
    return true;
  }
  if (Opc == M6809::ABXp) {
    BuildMI(MBB, MI, DL, get(M6809::ABX));
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
  if (Opc == M6809::SHL16_8 || Opc == M6809::LSR16_8 || Opc == M6809::ASR16_8) {
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
    // The source can be undef when only some of the result's bits are used.
    RegState Undef = getUndefRegState(MI.getOperand(1).isUndef());
    BuildMI(MBB, MI, DL, get(FirstOpc), First).addReg(First, Undef);
    MachineInstrBuilder MIB2 =
        BuildMI(MBB, MI, DL, get(Left ? M6809::ROL8 : M6809::ROR8), Second)
            .addReg(Second, Undef);
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
    bool Is8 =
        Opc == M6809::CB8ri || Opc == M6809::CB8rm || Opc == M6809::CB8rr;

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
      Register Base;
      int64_t Off;
      if (storeToScratch(MI, Rhs, *this, Base, Off)) {
        BuildMI(MBB, MI, DL, get(Is8 ? M6809::CMP8m : M6809::CMP16m))
            .add(Lhs)
            .addReg(Base)
            .addImm(Off)
            .addImm(M6809Mem::Auto);
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
static MachineBasicBlock *
parseCondBranch(const MachineInstr &MI, SmallVectorImpl<MachineOperand> &Cond) {
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

unsigned M6809InstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
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

//===----------------------------------------------------------------------===//
// Instruction sizes
//===----------------------------------------------------------------------===//

unsigned M6809InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  if (MI.isMetaInstruction())
    return 0;
  const MachineFunction &MF = *MI.getMF();
  if (MI.isInlineAsm())
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              MF.getTarget().getMCAsmInfo());
  unsigned Guess = MI.getDesc().getSize();
  // Branches start short and become long only in the assembler.
  if (MI.isBranch())
    return Guess;
  for (const MachineOperand &MO : MI.operands())
    if (MO.isReg() && MO.getReg().isVirtual())
      return Guess;
  if (MI.isPseudo() && !MI.isCall()) {
    // Expanded after register allocation, or lowered by M6809MCInstLower.
    switch (MI.getOpcode()) {
    case M6809::LD8dr:
    case M6809::LD16dr:
    case M6809::ST16dr:
    case M6809::LEA16dr:
    case M6809::LD8pi:
    case M6809::LD16pi:
    case M6809::LD8pd:
    case M6809::LD16pd:
    case M6809::ST8pi:
    case M6809::ST16pi:
    case M6809::ST8pd:
    case M6809::ST16pd:
      break;
    default:
      return Guess;
    }
  }
  for (const MachineOperand &MO : MI.operands())
    if (MO.isFI())
      return Guess;

  MCContext &Ctx = const_cast<MachineFunction &>(MF).getContext();
  if (!Emitter || EmitterCtx != &Ctx) {
    Emitter.reset(
        getTheM6809Target().createMCCodeEmitter(*MF.getTarget().getMCInstrInfo(), Ctx));
    EmitterCtx = &Ctx;
  }
  if (!Emitter)
    return Guess;
  MCInst Inst;
  M6809MCInstLower(Ctx, nullptr).lower(&MI, Inst);
  unsigned Reg = M6809::NoRegister;
  for (const MCOperand &Op : Inst)
    if (Op.isReg()) {
      Reg = Op.getReg().id();
      break;
    }
  if (!M6809::findOpcodeEntry(Inst.getOpcode(), Reg))
    return Guess;
  SmallVector<char, 8> Code;
  SmallVector<MCFixup, 2> Fixups;
  Emitter->encodeInstruction(Inst, Code, Fixups, STI);
  return Code.size();
}

//===----------------------------------------------------------------------===//
// Machine outliner
//===----------------------------------------------------------------------===//
//
// JSR and RTS change nothing but S and PC, so an outlined sequence needs no
// registers saved and may even keep the flags live across the call. What
// changes is S: inside the outlined function the return address sits under
// the stack, two bytes deeper. A sequence is outlined only if it leaves S
// where it found it and never pulls what was pushed before it; its S-relative
// offsets that reach past what it pushed itself then move along by two. A
// sequence that ends in a return is jumped to instead, and S is unchanged.

namespace {
enum M6809OutlinerKind { OutlinerCall, OutlinerTail };
} // namespace

static unsigned getRegListBytes(unsigned Mask) {
  unsigned Bytes = 0;
  for (unsigned Bit = 0; Bit != 8; ++Bit)
    if (Mask & (1u << Bit))
      Bytes += Bit >= 4 ? 2 : 1; // X, Y, U/S and PC are words
  return Bytes;
}

// The memory operand of MI with base S, as the index of its base register.
static std::optional<unsigned> getStackMemOperand(const MachineInstr &MI) {
  if (!MI.mayLoadOrStore() && MI.getOpcode() != M6809::LEA16 &&
      MI.getOpcode() != M6809::LEASm)
    return std::nullopt;
  for (unsigned I = 0, E = MI.getNumExplicitOperands(); I + 2 < E; ++I) {
    const MachineOperand &Base = MI.getOperand(I);
    if (Base.isReg() && Base.getReg() == M6809::S &&
        (MI.getOperand(I + 1).isImm() || MI.getOperand(I + 1).isGlobal()) &&
        MI.getOperand(I + 2).isImm())
      return I;
  }
  return std::nullopt;
}

// How many bytes MI pushes (negative: pulls), or nullopt if it changes S in
// a way the outliner does not follow.
static std::optional<int> getStackDelta(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case M6809::PSHS8:
    return 1;
  case M6809::PULS8:
    return -1;
  case M6809::PSHS16:
  case M6809::PSHSW:
    return 2;
  case M6809::PULS16:
  case M6809::PULSW:
    return -2;
  case M6809::PSHSr:
    return getRegListBytes(MI.getOperand(0).getImm());
  case M6809::PULSr:
  case M6809::PULSret:
    return -int(getRegListBytes(MI.getOperand(0).getImm()));
  case M6809::ADD8pop:
  case M6809::SUB8pop:
  case M6809::AND8pop:
  case M6809::OR8pop:
  case M6809::EOR8pop:
  case M6809::CMP8pop:
    return -1;
  case M6809::ADC8pop2:
  case M6809::SBC8pop2:
  case M6809::ADDDpop:
  case M6809::SUBDpop:
  case M6809::CMP16pop:
    return -2;
  case M6809::LEASi:
    return -int(MI.getOperand(0).getImm());
  default:
    break;
  }
  if (std::optional<unsigned> I = getStackMemOperand(MI)) {
    switch (MI.getOperand(*I + 2).getImm() & M6809Mem::ModeMask) {
    case M6809Mem::PostInc1:
      return -1;
    case M6809Mem::PostInc2:
      return -2;
    case M6809Mem::PreDec1:
      return 1;
    case M6809Mem::PreDec2:
      return 2;
    default:
      break;
    }
    if (MI.getOpcode() == M6809::LEASm)
      return -int(MI.getOperand(*I + 1).getImm());
  }
  if (MI.isCall() && !MI.isReturn())
    return 0;
  if (MI.modifiesRegister(M6809::S, /*TRI=*/nullptr))
    return std::nullopt;
  return 0;
}

// Walks a sequence that is to be called. Returns false if it pulls more
// than it pushed, leaves S moved, addresses the stack in a way whose offset
// cannot be moved, or calls a function: a callee may read arguments that the
// caller stored below the sequence's pushes. Each S-relative offset that
// reaches below the bytes pushed so far is passed to Fix.
static bool walkCalledSequence(
    iterator_range<MachineBasicBlock::iterator> Seq,
    function_ref<void(MachineInstr &, unsigned)> Fix) {
  int Depth = 0;
  for (MachineInstr &MI : Seq) {
    if (MI.isDebugInstr())
      continue;
    if (MI.isCall())
      return false;
    if (std::optional<unsigned> I = getStackMemOperand(MI)) {
      unsigned Mode = MI.getOperand(*I + 2).getImm() & M6809Mem::ModeMask;
      const MachineOperand &Off = MI.getOperand(*I + 1);
      switch (Mode) {
      case M6809Mem::Auto:
        if (MI.getOpcode() != M6809::LEASm &&
            (!Off.isImm() || Off.getImm() >= Depth))
          Fix(MI, *I + 1);
        break;
      case M6809Mem::PostInc1:
      case M6809Mem::PostInc2:
      case M6809Mem::PreDec1:
      case M6809Mem::PreDec2:
        break;
      default:
        return false;
      }
    }
    std::optional<int> D = getStackDelta(MI);
    if (!D)
      return false;
    // A pop reads the top of the stack first.
    Depth += *D;
    if (Depth < 0)
      return false;
  }
  return Depth == 0;
}

std::optional<std::unique_ptr<outliner::OutlinedFunction>>
M6809InstrInfo::getOutliningCandidateInfo(
    const MachineModuleInfo &MMI,
    std::vector<outliner::Candidate> &RepeatedSequenceLocs,
    unsigned MinRepeats) const {
  outliner::Candidate &C = RepeatedSequenceLocs[0];
  bool Tail = C.back().isReturn();
  unsigned FrameOverhead = 0;
  if (!Tail) {
    FrameOverhead = 1; // rts
    // An offset moved from 14 or 15 to 16 or 17 needs a longer postbyte.
    bool OK = walkCalledSequence(
        make_range(C.begin(), C.end()), [&](MachineInstr &MI, unsigned Idx) {
          const MachineOperand &Off = MI.getOperand(Idx);
          if (Off.isImm() && (Off.getImm() == 14 || Off.getImm() == 15))
            ++FrameOverhead;
        });
    if (!OK)
      return std::nullopt;
  }
  unsigned SequenceSize = 0;
  for (MachineInstr &MI : C)
    SequenceSize += getInstSizeInBytes(MI);
  // jsr sym or jmp sym.
  for (outliner::Candidate &Cand : RepeatedSequenceLocs)
    Cand.setCallInfo(Tail ? OutlinerTail : OutlinerCall, 3);
  if (RepeatedSequenceLocs.size() < MinRepeats)
    return std::nullopt;
  return std::make_unique<outliner::OutlinedFunction>(
      RepeatedSequenceLocs, SequenceSize, FrameOverhead,
      Tail ? OutlinerTail : OutlinerCall);
}

outliner::InstrType
M6809InstrInfo::getOutliningTypeImpl(const MachineModuleInfo &MMI,
                                     MachineBasicBlock::iterator &MIT,
                                     unsigned Flags) const {
  MachineInstr &MI = *MIT;
  if (MI.isInlineAsm() || MI.isPosition() || MI.isCFIInstruction())
    return outliner::InstrType::Illegal;
  if (MI.getOpcode() == M6809::RTI)
    return outliner::InstrType::Illegal;
  if (MI.isReturn())
    return outliner::InstrType::Legal;
  if (MI.isBranch() || MI.isIndirectBranch() || MI.isTerminator())
    return outliner::InstrType::Illegal;
  for (const MachineOperand &MO : MI.operands())
    if (MO.isMBB() || MO.isJTI() || MO.isCPI() || MO.isFI() ||
        MO.isBlockAddress() || MO.isMCSymbol())
      return outliner::InstrType::Illegal;
  // The frame pointer's set-up and the like.
  if (MI.getFlag(MachineInstr::FrameSetup) &&
      MI.modifiesRegister(M6809::U, /*TRI=*/nullptr) &&
      MI.getOpcode() == M6809::LEA16)
    return outliner::InstrType::Illegal;
  std::optional<int> Delta = getStackDelta(MI);
  if (!Delta)
    return outliner::InstrType::Illegal;
  // S read other than as an address, say "tfr s,x": its value would differ
  // by the return address.
  if (*Delta == 0 && !MI.isCall() && !getStackMemOperand(MI) &&
      MI.readsRegister(M6809::S, /*TRI=*/nullptr))
    return outliner::InstrType::Illegal;
  return outliner::InstrType::Legal;
}

void M6809InstrInfo::buildOutlinedFrame(
    MachineBasicBlock &MBB, MachineFunction &MF,
    const outliner::OutlinedFunction &OF) const {
  if (OF.FrameConstructionID == OutlinerTail)
    return;
  [[maybe_unused]] bool OK = walkCalledSequence(
      make_range(MBB.begin(), MBB.end()), [](MachineInstr &MI, unsigned Idx) {
        MachineOperand &Off = MI.getOperand(Idx);
        if (Off.isImm())
          Off.setImm(Off.getImm() + 2);
        else
          Off.setOffset(Off.getOffset() + 2);
      });
  assert(OK && "M6809: outlined an unbalanced sequence");
  MBB.insert(MBB.end(), BuildMI(MF, DebugLoc(), get(M6809::RTS)));
}

MachineBasicBlock::iterator M6809InstrInfo::insertOutlinedCall(
    Module &M, MachineBasicBlock &MBB, MachineBasicBlock::iterator &It,
    MachineFunction &MF, outliner::Candidate &C) const {
  bool Tail = C.CallConstructionID == OutlinerTail;
  It = MBB.insert(It, BuildMI(MF, DebugLoc(),
                              get(Tail ? M6809::TAILJMPe : M6809::JSRe))
                          .addGlobalAddress(M.getNamedValue(MF.getName())));
  return It;
}
