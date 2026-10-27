//===-- M6809PostRACleanup.cpp - Remove leftovers of register allocation --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// With four 16-bit registers, values move between registers and spill slots
// all the time, and register allocation leaves some moves that do nothing:
//
//   ldd 4,s / std 4,s   the store writes back the value just loaded
//   std 4,s / ldd 4,s   the load reads back the value just stored
//   ldd 4,s / ldd #0    the first load is overwritten before any use
//
// This pass removes them. ST and LD set the flags the same way (N and Z from
// the value, V clear), so dropping the second of a pair changes no flag.
// A dead load or move goes only if no later instruction reads the flags it
// set; CC is reserved, so MachineInstr::isDead never deletes these.
//
// It also turns an addition into D that only feeds an index register, and
// a load that only feeds a transfer, into one instruction:
//
//   pshs x / addd ,s++ / tfr d,x   into   leax d,x
//   ldd 4,s / tfr d,x              into   ldx 4,s
//
// and drops the second of two moves that undo each other, and of two
// constant steps of one index register:
//
//   tfr d,u / tfr u,d              into   tfr d,u
//   leau 13,s / leau 4,u           into   leau 17,s
//
//===----------------------------------------------------------------------===//

#include "M6809.h"
#include "M6809InstrInfo.h"
#include "M6809Subtarget.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "m6809-post-ra-cleanup"
#define PASS_NAME "M6809 post-RA cleanup"

static cl::opt<bool> DisableCleanup("m6809-disable-post-ra-cleanup", cl::Hidden,
                                    cl::desc("Keep the moves that register "
                                             "allocation leaves behind"));

namespace {

class M6809PostRACleanup : public MachineFunctionPass {
public:
  static char ID;
  M6809PostRACleanup() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return PASS_NAME; }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace

char M6809PostRACleanup::ID = 0;

INITIALIZE_PASS(M6809PostRACleanup, DEBUG_TYPE, PASS_NAME, false, false)

// Width of a plain load or store: 1 or 2, or 0 for anything else.
static unsigned loadWidth(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case M6809::LD8m:
    return 1;
  case M6809::LD16m:
    return 2;
  default:
    return 0;
  }
}

static unsigned storeWidth(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case M6809::ST8m:
    return 1;
  case M6809::ST16m:
    return 2;
  default:
    return 0;
  }
}

// Do two loads/stores (register, base, offset, mode) move the same register
// to or from the same place, without the access changing that place?
static bool sameAccess(const MachineInstr &A, const MachineInstr &B,
                       const TargetRegisterInfo &TRI) {
  if (A.hasOrderedMemoryRef() || B.hasOrderedMemoryRef())
    return false;
  Register R = A.getOperand(0).getReg();
  if (B.getOperand(0).getReg() != R)
    return false;
  const MachineOperand &Base = A.getOperand(1);
  const MachineOperand &Off = A.getOperand(2);
  if (A.getOperand(3).getImm() != M6809Mem::Auto ||
      B.getOperand(3).getImm() != M6809Mem::Auto ||
      !Base.isIdenticalTo(B.getOperand(1)) ||
      !Off.isIdenticalTo(B.getOperand(2)))
    return false;
  // "ldx 2,x" changes the address.
  return !(Base.isReg() && Base.getReg() && TRI.regsOverlap(Base.getReg(), R));
}

// Instructions that only write their destination register (and the flags).
static bool isPlainDef(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case M6809::LD8m:
  case M6809::LD16m:
  case M6809::LD8i:
  case M6809::LD16i:
  case M6809::LD8dr:
  case M6809::LD16dr:
  case M6809::TFR8:
  case M6809::TFR16:
  case M6809::LEA16:
  case M6809::LEA16dr:
    return !MI.hasOrderedMemoryRef();
  default:
    return false;
  }
}

static MachineInstr *nextInstr(MachineInstr &MI) {
  MachineBasicBlock::iterator I = std::next(MI.getIterator());
  MachineBasicBlock::iterator E = MI.getParent()->end();
  while (I != E && I->isDebugInstr())
    ++I;
  return I == E ? nullptr : &*I;
}

// Does an instruction after MI read the flags before they are set again?
// CC is reserved, so liveness does not track it. Flags never live across
// blocks: each block's branch consumes them.
static bool flagsReadAfter(MachineInstr &MI, const TargetRegisterInfo &TRI) {
  for (MachineBasicBlock::iterator J = std::next(MI.getIterator()),
                                   E = MI.getParent()->end();
       J != E; ++J) {
    if (J->readsRegister(M6809::CC, &TRI))
      return true;
    if (J->modifiesRegister(M6809::CC, &TRI))
      return false;
  }
  return false;
}

// pshs r / addd ,s++ / tfr d,r2, where the flags are dead after: r2 = r + D
// with "lear2 d,r", and "tfr r2,d" after it if D is still live.
static bool foldAddIntoLea(MachineBasicBlock &MBB,
                           const TargetRegisterInfo &TRI,
                           const TargetInstrInfo &TII) {
  bool Changed = false;
  for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;
       ++I) {
    if (I->getOpcode() != M6809::PSHS16)
      continue;
    Register R = I->getOperand(0).getReg();
    if (R != M6809::X && R != M6809::Y && R != M6809::U)
      continue;
    MachineInstr *Add = nextInstr(*I);
    if (!Add || Add->getOpcode() != M6809::ADDDpop)
      continue;
    MachineInstr *Tfr = nextInstr(*Add);
    if (!Tfr || Tfr->getOpcode() != M6809::TFR16 ||
        Tfr->getOperand(1).getReg() != M6809::D)
      continue;
    Register R2 = Tfr->getOperand(0).getReg();
    if (R2 != M6809::X && R2 != M6809::Y && R2 != M6809::U)
      continue;
    // Liveness after the transfer.
    LivePhysRegs Live(TRI);
    Live.addLiveOuts(MBB);
    for (MachineBasicBlock::iterator J = MBB.end(),
                                     After = std::next(Tfr->getIterator());
         J != After;)
      Live.stepBackward(*--J);
    const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
    if (flagsReadAfter(*Tfr, TRI))
      continue;
    DebugLoc DL = Add->getDebugLoc();
    MachineInstr *Lea =
        BuildMI(MBB, Tfr->getIterator(), DL, TII.get(M6809::LEA16dr), R2)
            .addReg(R)
            .addReg(M6809::D);
    if (!Live.available(MRI, M6809::D))
      BuildMI(MBB, Tfr->getIterator(), DL, TII.get(M6809::TFR16), M6809::D)
          .addReg(R2);
    Tfr->eraseFromParent();
    Add->eraseFromParent();
    I->eraseFromParent();
    I = Lea->getIterator();
    Changed = true;
  }
  return Changed;
}

// A load that only feeds a transfer loads the destination directly:
// "ldd 4,s; tfr d,x" is "ldx 4,s" when D is dead after. LD sets the flags
// from the value whichever register it writes.
static bool loadIntoTransferDest(MachineBasicBlock &MBB,
                                 const TargetRegisterInfo &TRI) {
  bool Changed = false;
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  for (MachineInstr &MI : make_early_inc_range(MBB)) {
    unsigned Opc = MI.getOpcode();
    if (Opc != M6809::TFR16 && Opc != M6809::TFR8)
      continue;
    Register Dst = MI.getOperand(0).getReg();
    Register Src = MI.getOperand(1).getReg();
    const TargetRegisterClass &RC =
        Opc == M6809::TFR16 ? M6809::GR16RegClass : M6809::ACC8RegClass;
    if (!RC.contains(Dst) || !RC.contains(Src))
      continue;
    MachineBasicBlock::iterator PI = MI.getIterator();
    if (PI == MBB.begin())
      continue;
    MachineInstr &Prev = *std::prev(PI);
    unsigned POpc = Prev.getOpcode();
    if (!(POpc == M6809::LD16m || POpc == M6809::LD16i ||
          POpc == M6809::LD8m || POpc == M6809::LD8i) ||
        Prev.getOperand(0).getReg() != Src || Prev.hasOrderedMemoryRef())
      continue;
    // "ldd ,x++; tfr d,x" must not become "ldx ,x++".
    if ((POpc == M6809::LD16m || POpc == M6809::LD8m) &&
        Prev.getOperand(3).getImm() != M6809Mem::Auto &&
        Prev.getOperand(1).isReg() && Prev.getOperand(1).getReg() &&
        TRI.regsOverlap(Prev.getOperand(1).getReg(), Dst))
      continue;
    LivePhysRegs Live(TRI);
    Live.addLiveOuts(MBB);
    for (MachineBasicBlock::iterator J = MBB.end(), After = std::next(PI);
         J != After;)
      Live.stepBackward(*--J);
    if (!Live.available(MRI, Src))
      continue;
    Prev.getOperand(0).setReg(Dst);
    MI.eraseFromParent();
    Changed = true;
  }
  return Changed;
}

// "tfr d,u; tfr u,d": the second moves nothing (TFR sets no flags).
// "leau 13,s; leau 4,u": one step. LEAX and LEAY set Z from the result,
// which is the same either way, and the shortest offset form of the sum is
// never longer than the two.
static bool foldPairs(MachineBasicBlock &MBB) {
  bool Changed = false;
  for (MachineInstr &MI : MBB) {
    MachineInstr *Next;
    while ((Next = nextInstr(MI))) {
      unsigned Opc = MI.getOpcode();
      if ((Opc == M6809::TFR16 || Opc == M6809::TFR8) &&
          Next->getOpcode() == Opc &&
          Next->getOperand(0).getReg() == MI.getOperand(1).getReg() &&
          Next->getOperand(1).getReg() == MI.getOperand(0).getReg()) {
        Next->eraseFromParent();
        Changed = true;
        continue;
      }
      if (Opc != M6809::LEA16 || Next->getOpcode() != M6809::LEA16)
        break;
      Register R = MI.getOperand(0).getReg();
      const MachineOperand &Base = MI.getOperand(1);
      const MachineOperand &Off = MI.getOperand(2);
      const MachineOperand &NBase = Next->getOperand(1);
      const MachineOperand &NOff = Next->getOperand(2);
      if (Next->getOperand(0).getReg() != R || !NBase.isReg() ||
          NBase.getReg() != R || !Base.isReg() || !Base.getReg() ||
          Base.getReg() == M6809::PC || !Off.isImm() || !NOff.isImm() ||
          MI.getOperand(3).getImm() != M6809Mem::Auto ||
          Next->getOperand(3).getImm() != M6809Mem::Auto)
        break;
      int64_t Sum = Off.getImm() + NOff.getImm();
      if (Sum < -32768 || Sum > 32767)
        break;
      MI.getOperand(2).setImm(Sum);
      Next->eraseFromParent();
      Changed = true;
    }
  }
  return Changed;
}

bool M6809PostRACleanup::runOnMachineFunction(MachineFunction &MF) {
  if (DisableCleanup || skipFunction(MF.getFunction()))
    return false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  for (MachineBasicBlock &MBB : MF) {
    bool BlockChanged = foldAddIntoLea(MBB, TRI, TII);
    BlockChanged |= loadIntoTransferDest(MBB, TRI);
    BlockChanged |= foldPairs(MBB);
    for (MachineInstr &MI : MBB) {
      unsigned W = loadWidth(MI) | storeWidth(MI);
      MachineInstr *Next;
      while (W && (Next = nextInstr(MI))) {
        bool Pair =
            loadWidth(MI) ? storeWidth(*Next) == W : loadWidth(*Next) == W;
        if (!Pair || !sameAccess(MI, *Next, TRI))
          break;
        Next->eraseFromParent();
        BlockChanged = true;
      }
    }

    // Flags never live across blocks: each block's branch consumes them.
    LiveRegUnits Live(TRI);
    Live.addLiveOuts(MBB);
    bool FlagsLive = false;
    for (MachineInstr &MI : make_early_inc_range(reverse(MBB))) {
      if (MI.isDebugInstr())
        continue;
      bool DefsCC = MI.modifiesRegister(M6809::CC, &TRI);
      if (isPlainDef(MI) && !(DefsCC && FlagsLive)) {
        Register Dst = MI.getOperand(0).getReg();
        if (!MRI.isReserved(Dst) && Live.available(Dst)) {
          MI.eraseFromParent();
          BlockChanged = true;
          continue;
        }
      }
      if (DefsCC)
        FlagsLive = MI.readsRegister(M6809::CC, &TRI);
      else if (MI.readsRegister(M6809::CC, &TRI))
        FlagsLive = true;
      Live.stepBackward(MI);
    }
    // A removed reload leaves a kill on the store before it.
    if (BlockChanged)
      recomputeLivenessFlags(MBB);
    Changed |= BlockChanged;
  }
  return Changed;
}

FunctionPass *llvm::createM6809PostRACleanupPass() {
  return new M6809PostRACleanup();
}
