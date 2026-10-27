//===-- M6800MCTargetDesc.cpp - M6800 Target Descriptions -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800MCTargetDesc.h"
#include "M6800InstPrinter.h"
#include "M6800MCAsmInfo.h"
#include "TargetInfo/M6800TargetInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "M6800GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "M6800GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "M6800GenRegisterInfo.inc"

static MCInstrInfo *createM6800MCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitM6800MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createM6800MCRegisterInfo(const Triple &TT) {
  auto *X = new MCRegisterInfo();
  // The return address lives on the stack, not in a register.
  InitM6800MCRegisterInfo(X, M6800::PC);
  return X;
}

static MCSubtargetInfo *
createM6800MCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  if (CPU.empty())
    CPU = "6301";
  return createM6800MCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCAsmInfo *createM6800MCAsmInfo(const MCRegisterInfo &MRI,
                                       const Triple &TT,
                                       const MCTargetOptions &Options) {
  return new M6800MCAsmInfo(TT, Options);
}

static MCInstPrinter *createM6800MCInstPrinter(const Triple &T,
                                               unsigned SyntaxVariant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  return new M6800InstPrinter(MAI, MII, MRI);
}

namespace {
// Code needs no alignment: no filler before each object's .text.
class M6800MCObjectFileInfo : public MCObjectFileInfo {
  unsigned getTextSectionAlignment() const override { return 1; }
};
} // namespace

static MCObjectFileInfo *createM6800MCObjectFileInfo(MCContext &Ctx, bool PIC,
                                                    bool LargeCodeModel) {
  auto *MOFI = new M6800MCObjectFileInfo();
  MOFI->initMCObjectFileInfo(Ctx, PIC, LargeCodeModel);
  return MOFI;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeM6800TargetMC() {
  Target &T = getTheM6800Target();
  RegisterMCAsmInfoFn X(T, createM6800MCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createM6800MCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createM6800MCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createM6800MCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createM6800MCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createM6800MCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createM6800AsmBackend);
  TargetRegistry::RegisterMCObjectFileInfo(T, createM6800MCObjectFileInfo);
}
