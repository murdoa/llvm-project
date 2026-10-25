//===-- M6800PostRACleanup.cpp - Remove leftovers of register allocation --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// With two 16-bit registers, values move between registers and spill slots
// all the time, and register allocation leaves some moves that do nothing:
//
//   ldd 4,sp / std 4,sp   the store writes back the value just loaded
//   std 4,sp / ldd 4,sp   the load reads back the value just stored
//   ldd 4,sp / ldd #0     the first load is overwritten before any use
//
// It runs before M6800FrameAccess, while stack slots are still SP-based.
//
// This pass removes them. ST and LD set the flags the same way (N and Z from
// the value, V clear), so dropping the second of a pair changes no flag.
// A dead load or move goes only if no later instruction reads the flags it
// set; CC is reserved, so MachineInstr::isDead never deletes these.
//
//===----------------------------------------------------------------------===//

#include "M6800.h"
#include "M6800InstrInfo.h"
#include "M6800Subtarget.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "m6800-post-ra-cleanup"
#define PASS_NAME "M6800 post-RA cleanup"

static cl::opt<bool> DisableCleanup("m6800-disable-post-ra-cleanup",
                                    cl::Hidden,
                                    cl::desc("Keep the moves that register "
                                             "allocation leaves behind"));

namespace {

class M6800PostRACleanup : public MachineFunctionPass {
public:
  static char ID;
  M6800PostRACleanup() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return PASS_NAME; }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace

char M6800PostRACleanup::ID = 0;

INITIALIZE_PASS(M6800PostRACleanup, DEBUG_TYPE, PASS_NAME, false, false)

// Width of a plain load or store: 1 or 2, or 0 for anything else.
static unsigned loadWidth(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case M6800::LD8m: return 1;
  case M6800::LD16m: return 2;
  default: return 0;
  }
}

static unsigned storeWidth(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case M6800::ST8m: return 1;
  case M6800::ST16m: return 2;
  default: return 0;
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
  if (A.getOperand(3).getImm() != M6800Mem::Auto ||
      B.getOperand(3).getImm() != M6800Mem::Auto ||
      !Base.isIdenticalTo(B.getOperand(1)) ||
      !Off.isIdenticalTo(B.getOperand(2)))
    return false;
  // "ldx 2,x" changes the address.
  return !(Base.isReg() && Base.getReg() && TRI.regsOverlap(Base.getReg(), R));
}

// Instructions that only write their destination register (and the flags).
static bool isPlainDef(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case M6800::LD8m:
  case M6800::LD16m:
  case M6800::LD8i:
  case M6800::LD16i:
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

bool M6800PostRACleanup::runOnMachineFunction(MachineFunction &MF) {
  if (DisableCleanup || skipFunction(MF.getFunction()))
    return false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      unsigned W = loadWidth(MI) | storeWidth(MI);
      MachineInstr *Next;
      while (W && (Next = nextInstr(MI))) {
        bool Pair = loadWidth(MI) ? storeWidth(*Next) == W
                                  : loadWidth(*Next) == W;
        if (!Pair || !sameAccess(MI, *Next, TRI))
          break;
        Next->eraseFromParent();
        Changed = true;
      }
    }

    // Flags never live across blocks: each block's branch consumes them.
    LiveRegUnits Live(TRI);
    Live.addLiveOuts(MBB);
    bool FlagsLive = false;
    for (MachineInstr &MI : make_early_inc_range(reverse(MBB))) {
      if (MI.isDebugInstr())
        continue;
      bool DefsCC = MI.modifiesRegister(M6800::CC, &TRI);
      if (isPlainDef(MI) && !(DefsCC && FlagsLive)) {
        Register Dst = MI.getOperand(0).getReg();
        if (!MRI.isReserved(Dst) && Live.available(Dst)) {
          MI.eraseFromParent();
          Changed = true;
          continue;
        }
      }
      if (DefsCC)
        FlagsLive = MI.readsRegister(M6800::CC, &TRI);
      else if (MI.readsRegister(M6800::CC, &TRI))
        FlagsLive = true;
      Live.stepBackward(MI);
    }
  }
  return Changed;
}

FunctionPass *llvm::createM6800PostRACleanupPass() {
  return new M6800PostRACleanup();
}
