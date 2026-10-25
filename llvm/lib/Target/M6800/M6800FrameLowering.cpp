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
//            | locals / spills |  0 .. N - 1 after the prologue
//   SP + 1 ->+-----------------+
//
// There is no frame pointer: frames with variable-sized objects are not
// supported yet.
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

// Frames up to this size are allocated and freed with PSHX/PULX and
// DES/INS, one byte of code per two (or one) bytes of frame.
static constexpr int64_t SmallFrame = 8;

// The prologue keeps D, which may hold the first argument. X is free.
//
//   small:  pshx ... [des]
//   large:  tsx; xgdx; subd #n; xgdx; txs
//
// In the large form the second XGDX puts the caller's D back.
void M6800FrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (MFI.hasVarSizedObjects())
    MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported(
        MF.getFunction(), "variable-sized stack objects on the 6800"));
  int64_t StackSize = MFI.getStackSize();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  auto Emit = [&](unsigned Opc) {
    return BuildMI(MBB, MBBI, DL, TII.get(Opc))
        .setMIFlag(MachineInstr::FrameSetup);
  };

  if (StackSize <= SmallFrame) {
    for (int64_t I = 0; I + 2 <= StackSize; I += 2)
      Emit(M6800::PSHX).addReg(M6800::X, RegState::Undef);
    if (StackSize & 1)
      Emit(M6800::DES);
    return;
  }
  Emit(M6800::TSX);
  Emit(M6800::XGDX);
  Emit(M6800::SUBDi).addReg(M6800::D, RegState::Define).addReg(M6800::D)
      .addImm(StackSize);
  Emit(M6800::XGDX);
  Emit(M6800::TXS);
}

// The epilogue keeps D, and X too when it holds part of the return value.
//
//   X free, small:  pulx ... [ins]
//   X free, large:  tsx; xgdx; addd #n; xgdx; txs
//   X live, small:  ins ...
//   X live, large:  pshx; pshb; psha; tsx; ldd 0,x; std n,x; ldd 2,x;
//                   std n+2,x; xgdx; addd #n; xgdx; txs; pula; pulb; pulx
//
// The last form copies the saved D and X above the frame, then moves SP up
// to them.
void M6800FrameLowering::emitEpilogue(MachineFunction &MF,
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
  auto AddToX = [&](int64_t N) {
    Emit(M6800::XGDX);
    Emit(M6800::ADDDi).addReg(M6800::D, RegState::Define).addReg(M6800::D)
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
  if (StackSize <= 20 || StackSize + 3 > 255) {
    if (StackSize + 3 > 255)
      MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported(
          MF.getFunction(), "a frame this large with an i32 result"));
    for (int64_t I = 0; I != StackSize; ++I)
      Emit(M6800::INS);
    return;
  }
  Emit(M6800::PSHX).addReg(M6800::X);
  Emit(M6800::PSH8).addReg(M6800::B);
  Emit(M6800::PSH8).addReg(M6800::A);
  Emit(M6800::TSX);
  for (int64_t Off : {0, 2}) {
    Emit(M6800::LD16m).addReg(M6800::D, RegState::Define).addReg(M6800::X)
        .addImm(Off).addImm(M6800Mem::Auto);
    Emit(M6800::ST16m).addReg(M6800::D).addReg(M6800::X)
        .addImm(StackSize + Off).addImm(M6800Mem::Auto);
  }
  AddToX(StackSize);
  Emit(M6800::TXS);
  Emit(M6800::PUL8).addReg(M6800::A, RegState::Define);
  Emit(M6800::PUL8).addReg(M6800::B, RegState::Define);
  Emit(M6800::PULX).addReg(M6800::X, RegState::Define);
}

MachineBasicBlock::iterator M6800FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // The call frame is part of the fixed frame, so these do nothing.
  return MBB.erase(I);
}
