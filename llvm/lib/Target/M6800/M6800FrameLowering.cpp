//===-- M6800FrameLowering.cpp - Frame info for M6800 ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Stack layout. SP points at the first free byte, so the lowest byte in use
// is SP + 1, which is what TSX puts in X. Offsets count from there:
//
//            +-----------------+
//   higher   | stack args      |  2 ... on entry
//            | return address  |  0, 1 on entry (pushed by JSR/BSR)
//            | [caller's FP]   |  with a frame pointer
//            | locals / spills |  0 .. N - 1 after the prologue
//   SP + 1 ->+-----------------+
//            | [dynamic allocas, call arguments]
//
// A function with variable-sized objects has a frame pointer. No register
// is free for it, so it is the word __m6800_fp in memory, which holds SP + 1
// as it is after the prologue: stack slots are "ldx __m6800_fp" and "off,x"
// with the same offsets as from SP. The prologue saves the caller's value
// and the epilogue puts it back. Calls then move SP themselves, since the
// space below the fixed frame belongs to the allocas.
//
//===----------------------------------------------------------------------===//

#include "M6800FrameLowering.h"
#include "M6800.h"
#include "M6800InstrInfo.h"
#include "M6800Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"

using namespace llvm;

static const char *const FPSymbol = "__m6800_fp";

// Frames up to this size are allocated and freed with PSHX/PULX and
// DES/INS, one byte of code per two (or one) bytes of frame.
static constexpr int64_t SmallFrame = 8;

bool M6800FrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return MF.getFrameInfo().hasVarSizedObjects();
}

// The caller's frame pointer is saved just below the return address.
void M6800FrameLowering::determineCalleeSaves(MachineFunction &MF,
                                              BitVector &SavedRegs,
                                              RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  if (hasFP(MF))
    MF.getFrameInfo().CreateFixedSpillStackObject(2, -2);
}

// Loads or stores the frame pointer word: "ldx", "stx" or "lds".
static MachineInstrBuilder buildFPAccess(const TargetInstrInfo &TII,
                                         MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator I,
                                         const DebugLoc &DL, unsigned Opc) {
  MachineInstrBuilder MIB = BuildMI(MBB, I, DL, TII.get(Opc));
  if (Opc == M6800::LD16m)
    MIB.addReg(M6800::X, RegState::Define);
  else if (Opc == M6800::ST16m)
    MIB.addReg(M6800::X);
  return MIB.addReg(0).addExternalSymbol(FPSymbol).addImm(M6800Mem::Extended);
}

// The prologue keeps D, which may hold the first argument. X is free.
//
//   small:  pshx ... [des]
//   large:  tsx; xgdx; subd #n; xgdx; txs
//
// In the large form the second XGDX puts the caller's D back. With a frame
// pointer, "ldx __m6800_fp; pshx" comes first and "tsx; stx __m6800_fp"
// last.
void M6800FrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  emitPrologueCode(MF, MBB);
  M6800InstrInfo::markUndefSaves(MBB, STI.getRegisterInfo());
}

void M6800FrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  emitEpilogueCode(MF, MBB);
  M6800InstrInfo::markUndefSaves(MBB, STI.getRegisterInfo());
}

void M6800FrameLowering::emitPrologueCode(MachineFunction &MF,
                                          MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t StackSize = MFI.getStackSize();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  auto Emit = [&](unsigned Opc) {
    return BuildMI(MBB, MBBI, DL, TII.get(Opc))
        .setMIFlag(MachineInstr::FrameSetup);
  };
  bool FP = hasFP(MF);

  if (FP) {
    buildFPAccess(TII, MBB, MBBI, DL, M6800::LD16m)
        .setMIFlag(MachineInstr::FrameSetup);
    Emit(M6800::PSHX).addReg(M6800::X);
    StackSize -= 2;
  }
  if (StackSize <= SmallFrame) {
    for (int64_t I = 0; I + 2 <= StackSize; I += 2)
      Emit(M6800::PSHX).addReg(M6800::X, RegState::Undef);
    if (StackSize & 1)
      Emit(M6800::DES);
  } else {
    Emit(M6800::TSX);
    Emit(M6800::XGDX);
    Emit(M6800::SUBDi)
        .addReg(M6800::D, RegState::Define)
        .addReg(M6800::D)
        .addImm(StackSize);
    Emit(M6800::XGDX);
    Emit(M6800::TXS);
  }
  if (FP) {
    Emit(M6800::TSX);
    buildFPAccess(TII, MBB, MBBI, DL, M6800::ST16m)
        .setMIFlag(MachineInstr::FrameSetup);
  }
}

// The epilogue keeps D, and X too when it holds part of the return value.
// It frees N bytes:
//
//   X free, small:  pulx ... [ins]
//   X free, large:  tsx; xgdx; addd #n; xgdx; txs
//   X live, small:  ins ...
//   X live, large:  pshx; pshb; psha; tsx; ldd 0,x; std n,x; ldd 2,x;
//                   std n+2,x; xgdx; addd #n; xgdx; txs; pula; pulb; pulx
//
// The last form copies the saved D and X above the frame, then moves SP up
// to them.
//
// With a frame pointer, "lds __m6800_fp" first frees the allocas and all but
// one byte of the fixed frame below the saved FP; the saved FP is then put
// back with "pulx; stx __m6800_fp", or with X live:
//
//   pshx; pshb; psha; tsx; ldx 4,x; stx __m6800_fp; pula; pulb; pulx; ins; ins
void M6800FrameLowering::emitEpilogueCode(MachineFunction &MF,
                                          MachineBasicBlock &MBB) const {
  int64_t StackSize = MF.getFrameInfo().getStackSize();
  if (!StackSize)
    return;
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  bool XLive = MBBI != MBB.end() && MBBI->readsRegister(M6800::X, nullptr);
  auto Emit = [&](unsigned Opc) {
    return BuildMI(MBB, MBBI, DL, TII.get(Opc))
        .setMIFlag(MachineInstr::FrameDestroy);
  };

  if (!hasFP(MF)) {
    emitFree(MF, MBB, MBBI, DL, StackSize, XLive);
    return;
  }
  // SP + 1 = __m6800_fp: the bottom of the fixed frame.
  buildFPAccess(TII, MBB, MBBI, DL, M6800::LDSm)
      .setMIFlag(MachineInstr::FrameDestroy);
  if (StackSize == 2)
    Emit(M6800::DES);
  else
    emitFree(MF, MBB, MBBI, DL, StackSize - 3, XLive);
  if (!XLive) {
    Emit(M6800::PULX).addReg(M6800::X, RegState::Define);
    buildFPAccess(TII, MBB, MBBI, DL, M6800::ST16m)
        .setMIFlag(MachineInstr::FrameDestroy);
    return;
  }
  Emit(M6800::PSHX).addReg(M6800::X);
  Emit(M6800::PSH8).addReg(M6800::B);
  Emit(M6800::PSH8).addReg(M6800::A);
  Emit(M6800::TSX);
  Emit(M6800::LD16m)
      .addReg(M6800::X, RegState::Define)
      .addReg(M6800::X)
      .addImm(4)
      .addImm(M6800Mem::Auto);
  buildFPAccess(TII, MBB, MBBI, DL, M6800::ST16m)
      .setMIFlag(MachineInstr::FrameDestroy);
  Emit(M6800::PUL8).addReg(M6800::A, RegState::Define);
  Emit(M6800::PUL8).addReg(M6800::B, RegState::Define);
  Emit(M6800::PULX).addReg(M6800::X, RegState::Define);
  Emit(M6800::INS);
  Emit(M6800::INS);
}

void M6800FrameLowering::emitFree(MachineFunction &MF, MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MBBI,
                                  const DebugLoc &DL, int64_t StackSize,
                                  bool XLive) const {
  if (!StackSize)
    return;
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  auto Emit = [&](unsigned Opc) {
    return BuildMI(MBB, MBBI, DL, TII.get(Opc))
        .setMIFlag(MachineInstr::FrameDestroy);
  };
  auto AddToX = [&](int64_t N) {
    Emit(M6800::XGDX);
    Emit(M6800::ADDDi)
        .addReg(M6800::D, RegState::Define)
        .addReg(M6800::D)
        .addImm(N);
    Emit(M6800::XGDX);
  };

  if (!XLive && StackSize <= SmallFrame) {
    for (int64_t I = 0; I + 2 <= StackSize; I += 2)
      Emit(M6800::PULX).addReg(M6800::X, RegState::Define | RegState::Dead);
    if (StackSize & 1)
      Emit(M6800::INS);
    return;
  }
  if (!XLive) {
    Emit(M6800::TSX);
    AddToX(StackSize);
    Emit(M6800::TXS);
    return;
  }
  if (StackSize <= 20) {
    for (int64_t I = 0; I != StackSize; ++I)
      Emit(M6800::INS);
    return;
  }
  // Push D and X, copy the four bytes up over the frame, move SP up to
  // them and pull them. The copy reaches at most 255 bytes up, so a larger
  // frame takes several steps of the same size, each at least 21 bytes.
  Emit(M6800::PSHX).addReg(M6800::X);
  Emit(M6800::PSH8).addReg(M6800::B);
  Emit(M6800::PSH8).addReg(M6800::A);
  constexpr int64_t MaxStep = 252;
  int64_t Steps = (StackSize + MaxStep - 1) / MaxStep;
  for (int64_t Left = StackSize; Steps; --Steps) {
    int64_t Step = (Left + Steps - 1) / Steps;
    Left -= Step;
    Emit(M6800::TSX);
    for (int64_t Off : {0, 2}) {
      Emit(M6800::LD16m)
          .addReg(M6800::D, RegState::Define)
          .addReg(M6800::X)
          .addImm(Off)
          .addImm(M6800Mem::Auto);
      Emit(M6800::ST16m)
          .addReg(M6800::D)
          .addReg(M6800::X)
          .addImm(Step + Off)
          .addImm(M6800Mem::Auto);
    }
    AddToX(Step);
    Emit(M6800::TXS);
  }
  Emit(M6800::PUL8).addReg(M6800::A, RegState::Define);
  Emit(M6800::PUL8).addReg(M6800::B, RegState::Define);
  Emit(M6800::PULX).addReg(M6800::X, RegState::Define);
}

MachineBasicBlock::iterator M6800FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // Without a frame pointer the call frame is part of the fixed frame, so
  // these do nothing. With one, "des" or "ins" for each byte: they change
  // no register or flag.
  if (!hasReservedCallFrame(MF)) {
    const TargetInstrInfo &TII = *STI.getInstrInfo();
    bool Setup = I->getOpcode() == TII.getCallFrameSetupOpcode();
    int64_t Amount = I->getOperand(0).getImm();
    if (!Setup)
      Amount -= I->getOperand(1).getImm();
    for (int64_t K = 0; K < Amount; ++K)
      BuildMI(MBB, I, I->getDebugLoc(),
              TII.get(Setup ? M6800::DES : M6800::INS));
  }
  return MBB.erase(I);
}
