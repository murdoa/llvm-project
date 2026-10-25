//===-- M6809SplitDOperands.cpp - Copy second D operands out of D ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Many 6809 operations work only on D: the register-register pseudos such as
// ADDDrr and OR16rr take their left operand in D and their right operand in
// any 16-bit register. If the right operand was itself computed by a D-only
// instruction, its virtual register is in the one-register class ACC16.
// When its live range is short the allocator cannot split it, both operands
// need D, and allocation fails.
//
// This pass reads such operands through a COPY into a fresh GR16 register,
// so there is always a range the allocator can place in X, Y or U. It runs
// after the SSA optimizations, which would fold the copy away, and
// M6809RegisterInfo::shouldCoalesce stops the coalescer from undoing it.
//
//===----------------------------------------------------------------------===//

#include "M6809.h"
#include "M6809InstrInfo.h"
#include "M6809Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "m6809-split-d-operands"
#define PASS_NAME "M6809 copy second D operands out of D"

namespace {

class M6809SplitDOperands : public MachineFunctionPass {
public:
  static char ID;
  M6809SplitDOperands() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return PASS_NAME; }
};

} // namespace

char M6809SplitDOperands::ID = 0;

INITIALIZE_PASS(M6809SplitDOperands, DEBUG_TYPE, PASS_NAME, false, false)

// True if MI has a use tied to a def that must be in D.
static bool hasTiedDUse(const MachineInstr &MI,
                        const MachineRegisterInfo &MRI) {
  for (const MachineOperand &MO : MI.explicit_uses())
    if (MO.isReg() && MO.isTied() && MO.getReg().isVirtual() &&
        MRI.getRegClass(MO.getReg()) == &M6809::ACC16RegClass)
      return true;
  return false;
}

bool M6809SplitDOperands::runOnMachineFunction(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isCopy() || MI.isPHI() || !hasTiedDUse(MI, MRI))
        continue;
      for (unsigned I = 0, E = MI.getNumExplicitOperands(); I != E; ++I) {
        MachineOperand &MO = MI.getOperand(I);
        if (!MO.isReg() || MO.isDef() || MO.isTied() ||
            !MO.getReg().isVirtual() ||
            MRI.getRegClass(MO.getReg()) != &M6809::ACC16RegClass)
          continue;
        // Only if the operand itself accepts more than D.
        const TargetRegisterClass *OpRC = TII.getRegClass(MI.getDesc(), I);
        if (!OpRC || OpRC == &M6809::ACC16RegClass)
          continue;
        Register Copy = MRI.createVirtualRegister(&M6809::GR16RegClass);
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(TargetOpcode::COPY), Copy)
            .addReg(MO.getReg());
        MO.setReg(Copy);
        MO.setIsKill(false);
        Changed = true;
      }
    }
  }
  return Changed;
}

FunctionPass *llvm::createM6809SplitDOperandsPass() {
  return new M6809SplitDOperands();
}
