//===-- M6809FrameLowering.cpp - Frame info for M6809 ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Stack layout, S-relative, with no frame pointer yet:
//
//            +-----------------+
//   higher   | stack args      |  S_entry + 2 ...
//            | return address  |  S_entry      (pushed by JSR/BSR)
//            | locals / spills |  S_entry - N .. S_entry - 1
//   S  --->  +-----------------+  S_entry - N  (after "leas -N,s")
//
//===----------------------------------------------------------------------===//

#include "M6809FrameLowering.h"
#include "M6809.h"
#include "M6809InstrInfo.h"
#include "M6809Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

void M6809FrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  uint64_t StackSize = MF.getFrameInfo().getStackSize();
  if (StackSize == 0)
    return;

  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  BuildMI(MBB, MBBI, DL, TII.get(M6809::LEASi))
      .addImm(-static_cast<int64_t>(StackSize))
      .setMIFlag(MachineInstr::FrameSetup);
}

void M6809FrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  uint64_t StackSize = MF.getFrameInfo().getStackSize();
  if (StackSize == 0)
    return;

  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  BuildMI(MBB, MBBI, DL, TII.get(M6809::LEASi))
      .addImm(static_cast<int64_t>(StackSize))
      .setMIFlag(MachineInstr::FrameDestroy);
}

MachineBasicBlock::iterator M6809FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // Normally the call frame is part of the fixed frame and these pseudos do
  // nothing. With variable-sized objects S moves around the call instead.
  if (!hasReservedCallFrame(MF)) {
    int64_t Amount = I->getOperand(0).getImm();
    if (Amount) {
      bool Down = I->getOpcode() == M6809::ADJCALLSTACKDOWN;
      BuildMI(MBB, I, I->getDebugLoc(),
              STI.getInstrInfo()->get(M6809::LEASi))
          .addImm(Down ? -Amount : Amount);
    }
  }
  return MBB.erase(I);
}
