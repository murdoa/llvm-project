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

// Without a frame pointer the frame is one LEAS. With one, U is saved first
// and then points at its save slot, two bytes below S on entry:
//
//   pshs u; leau ,s; leas -n,s   ...   leas ,u; puls u
void M6809FrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  int64_t StackSize = MF.getFrameInfo().getStackSize();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  if (hasFP(MF)) {
    BuildMI(MBB, MBBI, DL, TII.get(M6809::PSHS16))
        .addReg(M6809::U, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, DL, TII.get(M6809::LEA16), M6809::U)
        .addReg(M6809::S)
        .addImm(0)
        .addImm(M6809Mem::Auto)
        .setMIFlag(MachineInstr::FrameSetup);
    StackSize -= FPSaveSize;
  }
  if (StackSize)
    BuildMI(MBB, MBBI, DL, TII.get(M6809::LEASi))
        .addImm(-StackSize)
        .setMIFlag(MachineInstr::FrameSetup);
}

void M6809FrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  int64_t StackSize = MF.getFrameInfo().getStackSize();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  if (hasFP(MF)) {
    BuildMI(MBB, MBBI, DL, TII.get(M6809::LEASm))
        .addReg(M6809::U)
        .addImm(0)
        .addImm(M6809Mem::Auto)
        .setMIFlag(MachineInstr::FrameDestroy);
    BuildMI(MBB, MBBI, DL, TII.get(M6809::PULS16), M6809::U)
        .setMIFlag(MachineInstr::FrameDestroy);
    return;
  }
  if (StackSize)
    BuildMI(MBB, MBBI, DL, TII.get(M6809::LEASi))
        .addImm(StackSize)
        .setMIFlag(MachineInstr::FrameDestroy);
}

void M6809FrameLowering::determineCalleeSaves(MachineFunction &MF,
                                              BitVector &SavedRegs,
                                              RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  if (!hasFP(MF))
    return;
  // The prologue saves U itself, in a slot the frame layout must skip.
  SavedRegs.reset(M6809::U);
  MF.getFrameInfo().CreateFixedSpillStackObject(FPSaveSize, -FPSaveSize);
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
