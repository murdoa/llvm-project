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
// In a function with a frame pointer, stack slots are based on FP instead,
// and "ldx __m6800_fp" takes the place of TSX. FP does not move when X is
// pushed, so those offsets stay as they are.
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
    bool X, D, B, C; // C: the carry flag.
  };

  const M6800InstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  DenseMap<const MachineInstr *, LiveAfter> Live;
  // The base register whose value X holds, from a TSX or "ldx __m6800_fp"
  // this pass emitted, or 0.
  Register XBase;

  void loadBase(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                const DebugLoc &DL, Register Base);

  void computeLiveness(MachineBasicBlock &MBB);
  bool rewrite(MachineInstr &MI, unsigned MemIdx);
  void rewriteInlineAsm(MachineInstr &MI);
  void rewriteLEAFI(MachineInstr &MI);
  void rewriteStoreX(MachineInstr &MI, Register Base, int64_t Off);
  void addToX(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
              const DebugLoc &DL, int64_t N, bool BFree);
  void addToXKeepFlags(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                       const DebugLoc &DL, Register Base, int64_t N);
};

} // namespace

char M6800FrameAccess::ID = 0;

INITIALIZE_PASS(M6800FrameAccess, DEBUG_TYPE, PASS_NAME, false, false)

// Whether MI changes C. Loads, stores and register transfers set N and Z
// and clear V but leave C alone, so a spill or reload may sit between an
// instruction that sets C and one that reads it ("asld; std 4,sp; rolb").
static bool clobbersC(const MachineInstr &MI, const TargetRegisterInfo *TRI) {
  if (!MI.modifiesRegister(M6800::CC, TRI))
    return false;
  switch (MI.getOpcode()) {
  case M6800::LD8m:
  case M6800::LD16m:
  case M6800::ST8m:
  case M6800::ST16m:
  case M6800::LD8i:
  case M6800::LD16i:
  case M6800::TAB:
  case M6800::TBA:
  case M6800::INX:
  case M6800::DEX:
    return false;
  default:
    return true;
  }
}

// Whether C is read at the start of MBB before anything sets it.
static bool cLiveIn(const MachineBasicBlock &MBB,
                    const TargetRegisterInfo *TRI) {
  for (const MachineInstr &MI : MBB) {
    if (MI.readsRegister(M6800::CC, TRI))
      return true;
    if (clobbersC(MI, TRI))
      return false;
  }
  return false;
}

// CC is reserved, so LivePhysRegs does not track it: C is followed by hand.
void M6800FrameAccess::computeLiveness(MachineBasicBlock &MBB) {
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  LivePhysRegs LPR(*TRI);
  LPR.addLiveOuts(MBB);
  bool CLive = any_of(MBB.successors(), [&](const MachineBasicBlock *Succ) {
    return cLiveIn(*Succ, TRI);
  });
  for (MachineInstr &MI : reverse(MBB)) {
    Live[&MI] = {!LPR.available(MRI, M6800::X), !LPR.available(MRI, M6800::D),
                 !LPR.available(MRI, M6800::B), CLive};
    LPR.stepBackward(MI);
    CLive = (CLive && !clobbersC(MI, TRI)) || MI.readsRegister(M6800::CC, TRI);
  }
}

// The explicit operand that starts a memory operand based on SP or FP, or -1.
static int findStackOperand(const MachineInstr &MI) {
  for (unsigned I = 0, N = MI.getNumExplicitOperands(); I + 2 < N; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (MO.isReg() && !MO.isDef() &&
        (MO.getReg() == M6800::SP || MO.getReg() == M6800::FP) &&
        MI.getOperand(I + 1).isImm() && MI.getOperand(I + 2).isImm())
      return I;
  }
  return -1;
}

// X = SP + 1 ("tsx") or X = __m6800_fp ("ldx __m6800_fp", which leaves C
// alone).
void M6800FrameAccess::loadBase(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I,
                                const DebugLoc &DL, Register Base) {
  if (Base == M6800::SP) {
    BuildMI(MBB, I, DL, TII->get(M6800::TSX));
    return;
  }
  BuildMI(MBB, I, DL, TII->get(M6800::LD16m), M6800::X)
      .addReg(0)
      .addExternalSymbol("__m6800_fp")
      .addImm(M6800Mem::Extended);
}

// X += N. "inx" for small N; "ldab #n; abx" when B is free; else "xgdx;
// addd #n; xgdx", which leaves D as it was. All but INX change the flags.
void M6800FrameAccess::addToX(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I, const DebugLoc &DL,
                              int64_t N, bool BFree) {
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

// X = Base + N, leaving A, B and the flags as they were: "pshb; psha;
// tpa; psha; tsx; xgdx; addd #n; xgdx; pula; tap; pula; pulb". For SP, N
// counts from SP + 1 as it is before this sequence.
void M6800FrameAccess::addToXKeepFlags(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator I,
                                       const DebugLoc &DL, Register Base,
                                       int64_t N) {
  BuildMI(MBB, I, DL, TII->get(M6800::PSH8)).addReg(M6800::B);
  BuildMI(MBB, I, DL, TII->get(M6800::PSH8)).addReg(M6800::A);
  BuildMI(MBB, I, DL, TII->get(M6800::TPA));
  BuildMI(MBB, I, DL, TII->get(M6800::PSH8)).addReg(M6800::A);
  loadBase(MBB, I, DL, Base);
  BuildMI(MBB, I, DL, TII->get(M6800::XGDX));
  BuildMI(MBB, I, DL, TII->get(M6800::ADDDi), M6800::D)
      .addReg(M6800::D)
      .addImm(Base == M6800::SP ? N + 3 : N);
  BuildMI(MBB, I, DL, TII->get(M6800::XGDX));
  BuildMI(MBB, I, DL, TII->get(M6800::PUL8), M6800::A);
  BuildMI(MBB, I, DL, TII->get(M6800::TAP));
  BuildMI(MBB, I, DL, TII->get(M6800::PUL8), M6800::A);
  BuildMI(MBB, I, DL, TII->get(M6800::PUL8), M6800::B);
}

// The address of a stack slot, into X or D.
void M6800FrameAccess::rewriteLEAFI(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  Register Dst = MI.getOperand(0).getReg();
  Register Base = MI.getOperand(1).getReg();
  int64_t Off = MI.getOperand(2).getImm();
  LiveAfter LA = Live.lookup(&MI);

  if (Dst == M6800::X) {
    if (XBase != Base)
      loadBase(MBB, MI, DL, Base);
    addToX(MBB, MI, DL, Off, !LA.B && !LA.D);
    XBase = Off == 0 ? Base : Register();
  } else {
    // tsx; xgdx; addd #n, with X kept around it when live.
    bool SaveX = LA.X;
    if (SaveX)
      BuildMI(MBB, MI, DL, TII->get(M6800::PSHX)).addReg(M6800::X);
    if (XBase != Base || (SaveX && Base == M6800::SP))
      loadBase(MBB, MI, DL, Base);
    BuildMI(MBB, MI, DL, TII->get(M6800::XGDX));
    Off += SaveX && Base == M6800::SP ? 2 : 0;
    if (Off)
      BuildMI(MBB, MI, DL, TII->get(M6800::ADDDi), M6800::D)
          .addReg(M6800::D)
          .addImm(Off);
    if (SaveX)
      BuildMI(MBB, MI, DL, TII->get(M6800::PULX), M6800::X);
    XBase = Register();
  }
  MI.eraseFromParent();
}

// "stx off,sp": X goes through D. "[pshb; psha] xgdx; tsx; std off,x;
// [xgdx] [pula; pulb]", with D kept when live and X put back when live.
void M6800FrameAccess::rewriteStoreX(MachineInstr &MI, Register Base,
                                     int64_t Off) {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  LiveAfter LA = Live.lookup(&MI);
  if (LA.D) {
    BuildMI(MBB, MI, DL, TII->get(M6800::PSH8)).addReg(M6800::B);
    BuildMI(MBB, MI, DL, TII->get(M6800::PSH8)).addReg(M6800::A);
    if (Base == M6800::SP)
      Off += 2;
  }
  BuildMI(MBB, MI, DL, TII->get(M6800::XGDX));
  if (Off > 255 && LA.C) {
    // The store leaves C alone, so C must survive the add.
    addToXKeepFlags(MBB, MI, DL, Base, Off);
    Off = 0;
  } else {
    loadBase(MBB, MI, DL, Base);
    if (Off > 255) {
      // addToX keeps the value in D. The store sets N, Z and V again.
      addToX(MBB, MI, DL, Off, false);
      Off = 0;
    }
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
  XBase = Register();
  MI.eraseFromParent();
}

// Rewrites the stack slot operand at MemIdx. Returns false if MI was erased.
bool M6800FrameAccess::rewrite(MachineInstr &MI, unsigned MemIdx) {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  Register Base = MI.getOperand(MemIdx).getReg();
  int64_t Off = MI.getOperand(MemIdx + 1).getImm();
  LiveAfter LA = Live.lookup(&MI);

  if (MI.getOpcode() == M6800::ST16m && MI.getOperand(0).getReg() == M6800::X) {
    rewriteStoreX(MI, Base, Off);
    return false;
  }
  if (MI.readsRegister(M6800::X, TRI))
    report_fatal_error("M6800: stack slot access that also reads X");

  bool DefsX = MI.modifiesRegister(M6800::X, TRI);
  bool SaveX = LA.X && !DefsX;
  bool NeedsCC =
      MI.readsRegister(M6800::CC, TRI) || (LA.C && !clobbersC(MI, TRI));
  int64_t Pushed = SaveX && Base == M6800::SP ? 2 : 0;
  int64_t XOff = Off + Pushed;

  if (SaveX)
    BuildMI(MBB, MI, DL, TII->get(M6800::PSHX)).addReg(M6800::X);
  bool Moved = false;
  if (XOff > 255 && NeedsCC) {
    addToXKeepFlags(MBB, MI, DL, Base, XOff);
    XOff = 0;
    Moved = true;
  } else {
    if (SaveX || XBase != Base)
      loadBase(MBB, MI, DL, Base);
    if (XOff > 255) {
      addToX(MBB, MI, DL, XOff, false);
      XOff = 0;
      Moved = true;
    }
  }
  MI.getOperand(MemIdx).setReg(M6800::X);
  MI.getOperand(MemIdx + 1).setImm(XOff);
  if (SaveX)
    BuildMI(MBB, std::next(MI.getIterator()), DL, TII->get(M6800::PULX),
            M6800::X);
  XBase = !SaveX && !DefsX && !Moved ? Base : Register();
  return true;
}

// Stack slot operands of inline asm become "off,x" after a TSX (or "ldx
// __m6800_fp"), with X pushed and pulled around the asm when it is live.
// The asm must leave X alone, and each offset must fit the indexed mode.
void M6800FrameAccess::rewriteInlineAsm(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  LiveAfter LA = Live.lookup(&MI);
  if (MI.readsRegister(M6800::X, TRI) || MI.modifiesRegister(M6800::X, TRI)) {
    MI.emitInlineAsmError(
        "inline asm with a stack slot operand cannot also use X");
    return;
  }
  Register Base;
  for (int I = findStackOperand(MI); I >= 0; I = findStackOperand(MI)) {
    Register B = MI.getOperand(I).getReg();
    if (Base && B != Base) {
      MI.emitInlineAsmError(
          "inline asm operands on both SP and the frame pointer");
      return;
    }
    Base = B;
    int64_t Off = MI.getOperand(I + 1).getImm();
    if (LA.X && Base == M6800::SP)
      Off += 2;
    if (Off < 0 || Off > 255) {
      MI.emitInlineAsmError(
          "inline asm stack slot operand out of indexed range");
      return;
    }
    MI.getOperand(I).setReg(M6800::X);
    MI.getOperand(I + 1).setImm(Off);
  }
  if (LA.X)
    BuildMI(MBB, MI, DL, TII->get(M6800::PSHX)).addReg(M6800::X);
  if (LA.X || XBase != Base)
    loadBase(MBB, MI, DL, Base);
  if (LA.X)
    BuildMI(MBB, std::next(MI.getIterator()), DL, TII->get(M6800::PULX),
            M6800::X);
  XBase = LA.X ? Register() : Base;
}

bool M6800FrameAccess::runOnMachineFunction(MachineFunction &MF) {
  TII = MF.getSubtarget<M6800Subtarget>().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    Live.clear();
    computeLiveness(MBB);
    XBase = Register();
    for (MachineInstr &MI : make_early_inc_range(MBB)) {
      if (MI.isDebugInstr())
        continue;
      if (MI.getOpcode() == M6800::LEAFI) {
        rewriteLEAFI(MI);
        Changed = true;
        continue;
      }
      int MemIdx = findStackOperand(MI);
      if (MemIdx >= 0 && MI.isInlineAsm()) {
        rewriteInlineAsm(MI);
        Changed = true;
        if (MI.modifiesRegister(M6800::SP, TRI))
          XBase = Register();
        continue;
      }
      if (MemIdx >= 0) {
        Changed = true;
        if (!rewrite(MI, MemIdx))
          continue;
        // rewrite() has set XBase for what MI leaves in X.
        if (XBase == M6800::SP && MI.modifiesRegister(M6800::SP, TRI))
          XBase = Register();
        continue;
      }
      if (MI.modifiesRegister(M6800::X, TRI) ||
          (XBase == M6800::SP && MI.modifiesRegister(M6800::SP, TRI)))
        XBase = Register();
    }
    M6800InstrInfo::markUndefSaves(MBB, TRI);
  }
  return Changed;
}

FunctionPass *llvm::createM6800FrameAccessPass() {
  return new M6800FrameAccess();
}
