//===-- M6809TailCalls.cpp - Calls just before a return become jumps ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The machine outliner leaves "jsr sym / rts" behind: a call to an outlined
// sequence just before a return, where the other copies of the sequence did
// not end in one, and a call that ended an outlined sequence's caller. This
// pass, after the outliner, makes each of them "jmp sym", a byte shorter.
//
// A block that returns right after the call has no frame left, so S is as
// it was at entry and the callee can have no arguments on the stack (2,s
// for it would be this function's return address). Two calls stay:
// those in a function that calls setjmp and the like, and those to an
// outlined sequence that reaches into its caller's frame, whose offsets
// count the return address the JSR pushes (M6809::CallerFrameAttr).
//
//===----------------------------------------------------------------------===//

#include "M6809.h"
#include "M6809InstrInfo.h"
#include "M6809Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "m6809-tail-calls"
#define PASS_NAME "M6809 calls before a return into jumps"

namespace {

class M6809TailCalls : public MachineFunctionPass {
public:
  static char ID;
  M6809TailCalls() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return PASS_NAME; }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace

char M6809TailCalls::ID = 0;

INITIALIZE_PASS(M6809TailCalls, DEBUG_TYPE, PASS_NAME, false, false)

bool M6809TailCalls::runOnMachineFunction(MachineFunction &MF) {
  // setjmp keeps the return address: it must stay this function's.
  if (skipFunction(MF.getFunction()) || MF.exposesReturnsTwice())
    return false;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::iterator Ret = MBB.getLastNonDebugInstr();
    if (Ret == MBB.end() || Ret->getOpcode() != M6809::RTS ||
        Ret == MBB.begin())
      continue;
    MachineBasicBlock::iterator Call = prev_nodbg(Ret, MBB.begin());
    if (Call->getOpcode() != M6809::JSRe)
      continue;
    const MachineOperand &Callee = Call->getOperand(0);
    if (Callee.isGlobal())
      if (const auto *F = dyn_cast<Function>(Callee.getGlobal()))
        if (F->hasFnAttribute(M6809::CallerFrameAttr))
          continue;
    BuildMI(MBB, Ret, Call->getDebugLoc(), TII.get(M6809::TAILJMPe))
        .add(Call->getOperand(0));
    Call->eraseFromParent();
    Ret->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

FunctionPass *llvm::createM6809TailCallsPass() { return new M6809TailCalls(); }
