//===-- M6800SplitDOperands.cpp - Copy second D operands out of D ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Many 6800 operations work only on D. If an instruction whose result may be
// in D or X reads a D-only value as its tied operand, the two-address pass
// narrows the result to D as well. At -O0 a chain of such operations pins
// every value to D, and the allocator runs out of registers. This pass
// reads those operands through a COPY into a fresh GR16 register, at -O0.
//
// Values narrowed to X alone (by MachineCSE's copy propagation, say) are
// read through a GR16 COPY at every level. Otherwise the two-address pass
// pins the result to X too, and storing it through an address in X has no
// assignment.
//
//===----------------------------------------------------------------------===//

#include "M6800.h"
#include "M6800InstrInfo.h"
#include "M6800Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "m6800-split-d-operands"
#define PASS_NAME "M6800 copy D-only tied operands out of D"

namespace {

class M6800SplitDOperands : public MachineFunctionPass {
public:
  static char ID;
  M6800SplitDOperands() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return PASS_NAME; }
};

} // namespace

char M6800SplitDOperands::ID = 0;

INITIALIZE_PASS(M6800SplitDOperands, DEBUG_TYPE, PASS_NAME, false, false)

static bool isDClass(const TargetRegisterClass *RC) {
  return RC == &M6800::ACC16RegClass;
}

static bool isXClass(const TargetRegisterClass *RC) {
  return RC == &M6800::IDX16RegClass;
}

// Reads a tied source in a class IsNarrow accepts, of an instruction whose
// result may be in any GR16, through a GR16 copy. Otherwise the two-address
// pass narrows the result to the same class.
static bool widenTiedSources(MachineFunction &MF,
                             bool (*IsNarrow)(const TargetRegisterClass *)) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isCopy() || MI.isPHI() || MI.getNumExplicitDefs() != 1)
        continue;
      const TargetRegisterClass *DefRC = TII.getRegClass(MI.getDesc(), 0);
      if (DefRC != &M6800::GR16RegClass)
        continue;
      for (MachineOperand &MO : MI.explicit_uses()) {
        if (!MO.isReg() || !MO.isTied() || !MO.getReg().isVirtual() ||
            !IsNarrow(MRI.getRegClass(MO.getReg())))
          continue;
        Register Copy = MRI.createVirtualRegister(&M6800::GR16RegClass);
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(TargetOpcode::COPY), Copy)
            .addReg(MO.getReg());
        MO.setReg(Copy);
        MO.setIsKill(true);
        Changed = true;
      }
    }
  }
  return Changed;
}

bool M6800SplitDOperands::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = widenTiedSources(MF, isXClass);
  // At -O0 a chain of D-only operations would pin every value to D.
  if (MF.getTarget().getOptLevel() == CodeGenOptLevel::None)
    Changed |= widenTiedSources(MF, isDClass);
  return Changed;
}

FunctionPass *llvm::createM6800SplitDOperandsPass() {
  return new M6800SplitDOperands();
}
