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
// The D-offset accesses ("ldb d,x", "leax d,y") have the opposite problem:
// their offset must be in D, and when that value has other uses its whole
// live range is pinned to D, even across instructions that need A or B.
// Such offsets are read through a fresh ACC16 copy just before the access,
// and the original register is widened to whatever its other uses allow.
//
//===----------------------------------------------------------------------===//

#include "M6809.h"
#include "M6809InstrInfo.h"
#include "M6809Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Target/TargetMachine.h"

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

// D, or a class that the coalescer can narrow to D: DW16 (D or W) is what
// subregister uses leave behind.
static bool isDClass(const TargetRegisterClass *RC) {
  return RC == &M6809::ACC16RegClass || RC == &M6809::DW16RegClass;
}

// True if MI has a use tied to a def that must be in D. At -O0 a GR16 tie
// counts too: the fast allocator may put it in D, and it cannot move it
// again when another operand also needs D.
static bool hasTiedDUse(const MachineInstr &MI, const MachineRegisterInfo &MRI,
                        bool FastRA) {
  for (const MachineOperand &MO : MI.explicit_uses()) {
    if (!MO.isReg() || !MO.isTied() || !MO.getReg().isVirtual())
      continue;
    const TargetRegisterClass *RC = MRI.getRegClass(MO.getReg());
    if (FastRA ? RC->contains(M6809::D) : isDClass(RC))
      return true;
  }
  return false;
}

// At -O0, read a D-only tied source of an instruction whose result may be
// in any GR16 through a GR16 copy. Otherwise the two-address pass narrows
// the result to D as well, and a chain of such operations pins every value
// to D.
static bool widenTiedDSources(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isCopy() || MI.isPHI() || MI.getNumExplicitDefs() != 1)
        continue;
      const TargetRegisterClass *DefRC = TII.getRegClass(MI.getDesc(), 0);
      if (DefRC != &M6809::GR16RegClass)
        continue;
      for (MachineOperand &MO : MI.explicit_uses()) {
        if (!MO.isReg() || !MO.isTied() || !MO.getReg().isVirtual() ||
            !isDClass(MRI.getRegClass(MO.getReg())))
          continue;
        Register Copy = MRI.createVirtualRegister(&M6809::GR16RegClass);
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

static bool splitDOffsets(MachineFunction &MF);

bool M6809SplitDOperands::runOnMachineFunction(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  bool FastRA = MF.getTarget().getOptLevel() == CodeGenOptLevel::None;
  bool Changed = splitDOffsets(MF);
  if (FastRA)
    Changed |= widenTiedDSources(MF);

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isCopy() || MI.isPHI() || !hasTiedDUse(MI, MRI, FastRA))
        continue;
      for (unsigned I = 0, E = MI.getNumExplicitOperands(); I != E; ++I) {
        MachineOperand &MO = MI.getOperand(I);
        if (!MO.isReg() || MO.isDef() || MO.isTied() ||
            !MO.getReg().isVirtual() || !isDClass(MRI.getRegClass(MO.getReg())))
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

static bool isDOffsetAccess(unsigned Opc) {
  switch (Opc) {
  case M6809::LEA16dr:
  case M6809::LD8dr:
  case M6809::LD16dr:
  case M6809::ST16dr:
    return true;
  default:
    return false;
  }
}

// See the second paragraph at the top of the file.
static bool splitDOffsets(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!isDOffsetAccess(MI.getOpcode()))
        continue;
      for (MachineOperand &MO : MI.explicit_uses()) {
        if (!MO.isReg() || !MO.getReg().isVirtual())
          continue;
        Register Reg = MO.getReg();
        if (MRI.getRegClass(Reg) != &M6809::ACC16RegClass ||
            MRI.hasOneNonDBGUse(Reg))
          continue;
        Register Copy = MRI.createVirtualRegister(&M6809::ACC16RegClass);
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(TargetOpcode::COPY), Copy)
            .addReg(Reg);
        MO.setReg(Copy);
        MO.setIsKill(true);
        MRI.recomputeRegClass(Reg);
        Changed = true;
      }
    }
  }
  return Changed;
}

FunctionPass *llvm::createM6809SplitDOperandsPass() {
  return new M6809SplitDOperands();
}
