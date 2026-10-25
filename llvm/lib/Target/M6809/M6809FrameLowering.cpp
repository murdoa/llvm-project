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
#include "M6809MachineFunctionInfo.h"
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

// On the 6809 an operation on two registers stores one of them and reads
// it back as a memory operand. Pushing it costs 12 cycles for a byte
// ("pshs a; addb ,s+") and 16 for a word ("pshs x; addd ,s++"); a store to
// a slot in the frame and an indexed read cost 8-10 and 12-14, in the same
// bytes (one more for STY). Give the function such a slot when it has a
// frame anyway, so the slot costs nothing to set up, unless optimizing for
// size: the slot moves other objects further from S. The operations use it
// when they are expanded, after tail duplication has seen them as one
// instruction.
void M6809FrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  // The HD6309 has register-register forms of its own.
  if (STI.has6309() || MF.getFunction().hasOptSize())
    return;
  // A frame means a LEAS in the prologue: some local or spill slot, not
  // just fixed objects such as the arguments or the saved U.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  bool HasFrame = MFI.adjustsStack() && MFI.getMaxCallFrameSize();
  for (int I = 0, E = MFI.getObjectIndexEnd(); I != E && !HasFrame; ++I)
    HasFrame = !MFI.isDeadObjectIndex(I) && MFI.getObjectSize(I);
  if (!HasFrame)
    return;
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      if (M6809InstrInfo::usesScratchSlot(MI.getOpcode())) {
        MF.getInfo<M6809FunctionInfo>()->setScratchFrameIndex(
            MFI.CreateStackObject(2, Align(1), /*isSpillSlot=*/false));
        return;
      }
}

// An S-relative access takes no extra byte with an offset in -16..15, one
// more up to 127 and two beyond. Objects are placed in order away from the
// outgoing argument area at S, so put those with the most uses per byte
// last: they land nearest S, and a large array does not push small spill
// slots out of range.
void M6809FrameLowering::orderFrameObjects(
    const MachineFunction &MF, SmallVectorImpl<int> &Objects) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  SmallVector<unsigned, 16> Uses(MFI.getObjectIndexEnd());
  int Scratch = MF.getInfo<M6809FunctionInfo>()->getScratchFrameIndex();
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB) {
      if (Scratch >= 0 && M6809InstrInfo::usesScratchSlot(MI.getOpcode()))
        Uses[Scratch] += 2; // a store and a read
      for (const MachineOperand &MO : MI.operands())
        if (MO.isFI() && MO.getIndex() >= 0)
          ++Uses[MO.getIndex()];
    }
  // Uses[A] / Size(A) < Uses[B] / Size(B), without division.
  llvm::stable_sort(Objects, [&](int A, int B) {
    return uint64_t(Uses[A]) * MFI.getObjectSize(B) <
           uint64_t(Uses[B]) * MFI.getObjectSize(A);
  });
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
      BuildMI(MBB, I, I->getDebugLoc(), STI.getInstrInfo()->get(M6809::LEASi))
          .addImm(Down ? -Amount : Amount);
    }
  }
  return MBB.erase(I);
}
