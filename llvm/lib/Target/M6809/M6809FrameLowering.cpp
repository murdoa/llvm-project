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
#include "MCTargetDesc/M6809MCTargetDesc.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

// The stack-pointer adjustment of a frame can ride on a push or pull that
// is there anyway: "pshs u,x" for "pshs u; leas -2,s", and "puls x,u,pc"
// for "leas 2,s; puls u; rts". A pull may only take registers that are dead,
// and never CC or DP. Returns the mask of registers that take Bytes bytes, or
// 0 if they cannot.
static unsigned getFillMask(int64_t Bytes, bool Pull,
                            const LivePhysRegs *Live,
                            const MachineRegisterInfo *MRI) {
  if (Bytes <= 0)
    return 0;
  struct Cand {
    MCPhysReg R;
    unsigned Size;
  };
  // Two-byte registers first, so that few bytes are moved.
  static const Cand PushCands[] = {{M6809::X, 2}, {M6809::Y, 2}, {M6809::B, 1},
                              {M6809::A, 1}, {M6809::DP, 1}, {M6809::CC, 1}};
  static const Cand PullCands[] = {
      {M6809::X, 2}, {M6809::Y, 2}, {M6809::B, 1}, {M6809::A, 1}};
  ArrayRef<Cand> Cands =
      Pull ? ArrayRef<Cand>(PullCands) : ArrayRef<Cand>(PushCands);
  // The smallest set of registers that covers Bytes exactly.
  unsigned Mask = 0;
  for (const Cand &C : Cands) {
    if (C.Size > Bytes)
      continue;
    if (Live && !Live->available(*MRI, C.R))
      continue;
    Mask |= M6809::getPushPullMask(C.R, false);
    Bytes -= C.Size;
    if (!Bytes)
      return Mask;
  }
  return 0;
}

static void addFillOperands(MachineInstrBuilder &MIB, unsigned Mask,
                            bool Pull) {
  for (MCPhysReg R : {M6809::X, M6809::Y, M6809::A, M6809::B})
    if (Mask & M6809::getPushPullMask(R, false))
      MIB.addReg(R, Pull ? RegState::ImplicitDefine
                         : RegState::Implicit | RegState::Undef);
}

// Without a frame pointer the frame is one LEAS, after the pushes of the
// callee-saved registers. With one, U is saved first and then points at
// its save slot, two bytes below S on entry:
//
//   pshs u; leau ,s; leas -n,s   ...   leas ,u; puls u
void M6809FrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t StackSize = MFI.getStackSize();
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
  // Past the callee-saved pushes, which spillCalleeSavedRegisters put first.
  MachineInstr *Push = nullptr;
  for (const CalleeSavedInfo &I : MFI.getCalleeSavedInfo()) {
    (void)I;
    assert(MBBI != MBB.end() && MBBI->getOpcode() == M6809::PSHS16 &&
           "M6809: expected a callee-saved push");
    Push = &*MBBI++;
    StackSize -= 2;
  }
  if (!StackSize)
    return;
  // "pshs u; leas -2,s" is "pshs u,x".
  if (Push && Push->getOperand(0).getReg() == M6809::U && !hasFP(MF) &&
      MF.getFunction().hasOptSize()) {
    if (unsigned Fill = getFillMask(StackSize, false, nullptr, nullptr)) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, Push, Push->getDebugLoc(), TII.get(M6809::PSHSr))
              .addImm(Fill | M6809::getPushPullMask(M6809::U, false))
              .addReg(M6809::U, RegState::Implicit | RegState::Kill)
              .setMIFlag(MachineInstr::FrameSetup);
      addFillOperands(MIB, Fill, false);
      Push->eraseFromParent();
      return;
    }
  }
  BuildMI(MBB, MBBI, DL, TII.get(M6809::LEASi))
      .addImm(-StackSize)
      .setMIFlag(MachineInstr::FrameSetup);
}

void M6809FrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t StackSize = MFI.getStackSize();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator Term = MBB.getFirstTerminator();
  DebugLoc DL = Term != MBB.end() ? Term->getDebugLoc() : DebugLoc();

  // Back over the callee-saved pulls that restoreCalleeSavedRegisters put
  // before the terminator.
  MachineBasicBlock::iterator MBBI = Term;
  unsigned CSRSize = 0;
  for (const CalleeSavedInfo &I : MFI.getCalleeSavedInfo()) {
    (void)I;
    --MBBI;
    assert(MBBI->getOpcode() == M6809::PULS16 &&
           "M6809: expected a callee-saved pull");
    CSRSize += 2;
  }

  if (hasFP(MF)) {
    BuildMI(MBB, MBBI, DL, TII.get(M6809::LEASm))
        .addReg(M6809::U)
        .addImm(-int64_t(CSRSize))
        .addImm(M6809Mem::Auto)
        .setMIFlag(MachineInstr::FrameDestroy);
    MachineInstr *Pull = BuildMI(MBB, Term, DL, TII.get(M6809::PULS16), M6809::U)
                             .setMIFlag(MachineInstr::FrameDestroy);
    if (Term != MBB.end() && Term->getOpcode() == M6809::RTS) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, Term, DL, TII.get(M6809::PULSret))
              .addImm(M6809::getPushPullMask(M6809::U, false) |
                      M6809::getPushPullMask(M6809::PC, false))
              .addReg(M6809::U, RegState::ImplicitDefine)
              .setMIFlag(MachineInstr::FrameDestroy);
      for (const MachineOperand &MO : Term->implicit_operands())
        if (MO.isUse())
          MIB.add(MO);
      Pull->eraseFromParent();
      Term->eraseFromParent();
    }
    return;
  }

  int64_t Adjust = StackSize - CSRSize;
  MachineInstr *Pull = nullptr;
  if (MBBI != Term && std::prev(Term)->getOperand(0).getReg() == M6809::U)
    Pull = &*std::prev(Term);
  bool Ret = Term != MBB.end() && Term->getOpcode() == M6809::RTS;
  // Registers dead at the terminator can take the bytes LEAS would drop.
  unsigned Fill = 0;
  if (Adjust && MF.getFunction().hasOptSize() && Term != MBB.end() &&
      (Pull || Ret || Term->isReturn())) {
    LivePhysRegs Live(*STI.getRegisterInfo());
    Live.addLiveOuts(MBB);
    for (MachineBasicBlock::iterator I = MBB.end(); I != Term;)
      Live.stepBackward(*--I);
    Fill = getFillMask(Adjust, true, &Live, &MF.getRegInfo());
    // A lone "leas n,s" before a jump or RTI is as small as "puls x".
    if (!Pull && !Ret && Adjust <= 2)
      Fill = 0;
  }
  if (Adjust && !Fill)
    BuildMI(MBB, MBBI, DL, TII.get(M6809::LEASi))
        .addImm(Adjust)
        .setMIFlag(MachineInstr::FrameDestroy);
  if (!Fill && !(Pull && Ret))
    return;

  unsigned Mask = Fill;
  if (Pull)
    Mask |= M6809::getPushPullMask(M6809::U, false);
  if (Ret)
    Mask |= M6809::getPushPullMask(M6809::PC, false);
  MachineInstrBuilder MIB =
      BuildMI(MBB, Term, DL, TII.get(Ret ? M6809::PULSret : M6809::PULSr))
          .addImm(Mask)
          .setMIFlag(MachineInstr::FrameDestroy);
  if (Pull)
    MIB.addReg(M6809::U, RegState::ImplicitDefine);
  addFillOperands(MIB, Fill, true);
  if (Ret) {
    for (const MachineOperand &MO : Term->implicit_operands())
      if (MO.isUse())
        MIB.add(MO);
    Term->eraseFromParent();
  }
  if (Pull)
    Pull->eraseFromParent();
}

// Callee-saved registers are pushed in the prologue, just below the return
// address (and the saved frame pointer), and pulled in the epilogue.
bool M6809FrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t Offset = hasFP(MF) ? -FPSaveSize : 0;
  for (CalleeSavedInfo &I : CSI) {
    Offset -= 2;
    I.setFrameIdx(MFI.CreateFixedSpillStackObject(2, Offset));
  }
  return true;
}

bool M6809FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  for (const CalleeSavedInfo &I : CSI) {
    Register R = I.getReg();
    if (!MBB.isLiveIn(R))
      MBB.addLiveIn(R);
    BuildMI(MBB, MI, DL, TII.get(M6809::PSHS16))
        .addReg(R, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);
  }
  return true;
}

bool M6809FrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  for (const CalleeSavedInfo &I : reverse(CSI))
    BuildMI(MBB, MI, DL, TII.get(M6809::PULS16), I.getReg())
        .setMIFlag(MachineInstr::FrameDestroy);
  return true;
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
