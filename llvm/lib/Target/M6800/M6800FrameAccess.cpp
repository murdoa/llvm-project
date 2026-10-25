//===-- M6800FrameAccess.cpp - Reach stack slots through X ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The 6800 has no stack-relative addressing. Until this pass, a stack slot
// is a memory operand with SP as its base and an offset from SP + 1. This
// pass rewrites each one to use X, which TSX sets to SP + 1:
//
//   ldaa 3,sp        ->  tsx; ldaa 3,x
//
// When X holds a live value it is pushed and pulled around the access, and
// the offset moves on by the two pushed bytes:
//
//   ldaa 3,sp        ->  pshx; tsx; ldaa 5,x; pulx
//
// One TSX serves every access after it while X and SP stay unchanged. Stores
// of X go through D ("xgdx; tsx; std 3,x"), and the address of a stack slot
// (LEAFI) is computed here too. Offsets above 255, which indexed mode cannot
// hold, are added to X with "xgdx; addd #n; xgdx".
//
//===----------------------------------------------------------------------===//

#include "M6800.h"
#include "M6800InstrInfo.h"
#include "M6800Subtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "m6800-frame-access"
#define PASS_NAME "M6800 stack slot access through X"

namespace {

class M6800FrameAccess : public MachineFunctionPass {
public:
  static char ID;
  M6800FrameAccess() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return PASS_NAME; }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

private:
  // Which registers are live just after an instruction.
  struct LiveAfter {
    bool X, D, B, CC;
  };

  const M6800InstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  DenseMap<const MachineInstr *, LiveAfter> Live;
  // X holds SP + 1, from a TSX this pass emitted, and no other value.
  bool XIsSP = false;

  void computeLiveness(MachineBasicBlock &MBB);
  bool rewrite(MachineInstr &MI, unsigned MemIdx);
  void rewriteLEAFI(MachineInstr &MI);
  void rewriteStoreX(MachineInstr &MI, int64_t Off);
  void addToX(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
              const DebugLoc &DL, int64_t N, bool BFree);
};

} // namespace

char M6800FrameAccess::ID = 0;

INITIALIZE_PASS(M6800FrameAccess, DEBUG_TYPE, PASS_NAME, false, false)

void M6800FrameAccess::computeLiveness(MachineBasicBlock &MBB) {
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  LivePhysRegs LPR(*TRI);
  LPR.addLiveOuts(MBB);
  for (MachineInstr &MI : reverse(MBB)) {
    Live[&MI] = {!LPR.available(MRI, M6800::X), !LPR.available(MRI, M6800::D),
                 !LPR.available(MRI, M6800::B),
                 !LPR.available(MRI, M6800::CC)};
    LPR.stepBackward(MI);
  }
}

// The explicit operand that starts a memory operand based on SP, or -1.
static int findStackOperand(const MachineInstr &MI) {
  for (unsigned I = 0, N = MI.getNumExplicitOperands(); I + 2 < N; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (MO.isReg() && !MO.isDef() && MO.getReg() == M6800::SP &&
        MI.getOperand(I + 1).isImm() && MI.getOperand(I + 2).isImm())
      return I;
  }
  return -1;
}

// X += N. "inx" for small N; "ldab #n; abx" when B is free; else "xgdx;
// addd #n; xgdx", which leaves D as it was. All but INX change the flags.
void M6800FrameAccess::addToX(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I,
                              const DebugLoc &DL, int64_t N, bool BFree) {
  if (N >= 0 && N <= 3) {
    for (int64_t K = 0; K != N; ++K)
      BuildMI(MBB, I, DL, TII->get(M6800::INX));
    return;
  }
  if (BFree && N > 0 && N < 256) {
    BuildMI(MBB, I, DL, TII->get(M6800::LD8i), M6800::B).addImm(N);
    BuildMI(MBB, I, DL, TII->get(M6800::ABX));
    return;
  }
  BuildMI(MBB, I, DL, TII->get(M6800::XGDX));
  BuildMI(MBB, I, DL, TII->get(M6800::ADDDi), M6800::D)
      .addReg(M6800::D)
      .addImm(N);
  BuildMI(MBB, I, DL, TII->get(M6800::XGDX));
}

// The address of a stack slot, into X or D.
void M6800FrameAccess::rewriteLEAFI(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  Register Dst = MI.getOperand(0).getReg();
  int64_t Off = MI.getOperand(2).getImm();
  LiveAfter LA = Live.lookup(&MI);

  if (Dst == M6800::X) {
    if (!XIsSP)
      BuildMI(MBB, MI, DL, TII->get(M6800::TSX));
    addToX(MBB, MI, DL, Off, !LA.B && !LA.D);
    XIsSP = Off == 0;
  } else {
    // tsx; xgdx; addd #n, with X kept around it when live.
    bool SaveX = LA.X;
    if (SaveX)
      BuildMI(MBB, MI, DL, TII->get(M6800::PSHX)).addReg(M6800::X);
    if (SaveX || !XIsSP)
      BuildMI(MBB, MI, DL, TII->get(M6800::TSX));
    BuildMI(MBB, MI, DL, TII->get(M6800::XGDX));
    Off += SaveX ? 2 : 0;
    if (Off)
      BuildMI(MBB, MI, DL, TII->get(M6800::ADDDi), M6800::D)
          .addReg(M6800::D)
          .addImm(Off);
    if (SaveX)
      BuildMI(MBB, MI, DL, TII->get(M6800::PULX), M6800::X);
    XIsSP = false;
  }
  MI.eraseFromParent();
}

// "stx off,sp": X goes through D. "[pshb; psha] xgdx; tsx; std off,x;
// [xgdx] [pula; pulb]", with D kept when live and X put back when live.
void M6800FrameAccess::rewriteStoreX(MachineInstr &MI, int64_t Off) {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  LiveAfter LA = Live.lookup(&MI);
  if (LA.D) {
    BuildMI(MBB, MI, DL, TII->get(M6800::PSH8)).addReg(M6800::B);
    BuildMI(MBB, MI, DL, TII->get(M6800::PSH8)).addReg(M6800::A);
    Off += 2;
  }
  BuildMI(MBB, MI, DL, TII->get(M6800::XGDX));
  BuildMI(MBB, MI, DL, TII->get(M6800::TSX));
  if (Off > 255) {
    if (LA.CC)
      report_fatal_error("M6800: stack frame too large for this access");
    // addToX keeps the value in D.
    addToX(MBB, MI, DL, Off, false);
    Off = 0;
  }
  BuildMI(MBB, MI, DL, TII->get(M6800::ST16m))
      .addReg(M6800::D)
      .addReg(M6800::X)
      .addImm(Off)
      .add(MI.getOperand(3))
      .cloneMemRefs(MI);
  if (LA.X)
    BuildMI(MBB, MI, DL, TII->get(M6800::XGDX));
  if (LA.D) {
    BuildMI(MBB, MI, DL, TII->get(M6800::PUL8), M6800::A);
    BuildMI(MBB, MI, DL, TII->get(M6800::PUL8), M6800::B);
  }
  XIsSP = false;
  MI.eraseFromParent();
}

// Rewrites the stack slot operand at MemIdx. Returns false if MI was erased.
bool M6800FrameAccess::rewrite(MachineInstr &MI, unsigned MemIdx) {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  int64_t Off = MI.getOperand(MemIdx + 1).getImm();
  LiveAfter LA = Live.lookup(&MI);

  if (MI.getOpcode() == M6800::ST16m && MI.getOperand(0).getReg() == M6800::X) {
    rewriteStoreX(MI, Off);
    return false;
  }
  if (MI.readsRegister(M6800::X, TRI))
    report_fatal_error("M6800: stack slot access that also reads X");

  bool DefsX = MI.modifiesRegister(M6800::X, TRI);
  bool SaveX = LA.X && !DefsX;
  bool NeedsCC = MI.readsRegister(M6800::CC, TRI) ||
                 (LA.CC && !MI.modifiesRegister(M6800::CC, TRI));
  int64_t Pushed = SaveX ? 2 : 0;
  int64_t XOff = Off + Pushed;

  if (SaveX)
    BuildMI(MBB, MI, DL, TII->get(M6800::PSHX)).addReg(M6800::X);
  if (SaveX || !XIsSP)
    BuildMI(MBB, MI, DL, TII->get(M6800::TSX));
  bool Moved = false;
  if (XOff > 255) {
    if (NeedsCC)
      report_fatal_error("M6800: stack frame too large for this access");
    addToX(MBB, MI, DL, XOff, false);
    XOff = 0;
    Moved = true;
  }
  MI.getOperand(MemIdx).setReg(M6800::X);
  MI.getOperand(MemIdx + 1).setImm(XOff);
  if (SaveX)
    BuildMI(MBB, std::next(MI.getIterator()), DL, TII->get(M6800::PULX),
            M6800::X);
  XIsSP = !SaveX && !DefsX && !Moved;
  return true;
}

bool M6800FrameAccess::runOnMachineFunction(MachineFunction &MF) {
  TII = MF.getSubtarget<M6800Subtarget>().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    Live.clear();
    computeLiveness(MBB);
    XIsSP = false;
    for (MachineInstr &MI : make_early_inc_range(MBB)) {
      if (MI.isDebugInstr())
        continue;
      if (MI.getOpcode() == M6800::LEAFI) {
        rewriteLEAFI(MI);
        Changed = true;
        continue;
      }
      int MemIdx = findStackOperand(MI);
      if (MemIdx >= 0 && !MI.isInlineAsm()) {
        Changed = true;
        if (!rewrite(MI, MemIdx))
          continue;
        // rewrite() has set XIsSP for what MI leaves in X.
        if (MI.modifiesRegister(M6800::SP, TRI))
          XIsSP = false;
        continue;
      }
      if (MI.modifiesRegister(M6800::X, TRI) ||
          MI.modifiesRegister(M6800::SP, TRI))
        XIsSP = false;
    }
  }
  return Changed;
}

FunctionPass *llvm::createM6800FrameAccessPass() {
  return new M6800FrameAccess();
}
