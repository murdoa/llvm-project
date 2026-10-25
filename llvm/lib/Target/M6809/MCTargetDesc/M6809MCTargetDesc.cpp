//===-- M6809MCTargetDesc.cpp - M6809 Target Descriptions -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809MCTargetDesc.h"
#include "M6809InstPrinter.h"
#include "M6809MCAsmInfo.h"
#include "TargetInfo/M6809TargetInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "M6809GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "M6809GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "M6809GenRegisterInfo.inc"

static MCInstrInfo *createM6809MCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitM6809MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createM6809MCRegisterInfo(const Triple &TT) {
  auto *X = new MCRegisterInfo();
  // The return address lives on the stack, not in a register.
  InitM6809MCRegisterInfo(X, M6809::PC);
  return X;
}

static MCSubtargetInfo *createM6809MCSubtargetInfo(const Triple &TT,
                                                   StringRef CPU,
                                                   StringRef FS) {
  if (CPU.empty())
    CPU = "6809";
  return createM6809MCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCAsmInfo *createM6809MCAsmInfo(const MCRegisterInfo &MRI,
                                       const Triple &TT,
                                       const MCTargetOptions &Options) {
  return new M6809MCAsmInfo(TT, Options);
}

static MCInstPrinter *createM6809MCInstPrinter(const Triple &T,
                                               unsigned SyntaxVariant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  return new M6809InstPrinter(MAI, MII, MRI);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeM6809TargetMC() {
  Target &T = getTheM6809Target();
  RegisterMCAsmInfoFn X(T, createM6809MCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createM6809MCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createM6809MCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createM6809MCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createM6809MCInstPrinter);
}
