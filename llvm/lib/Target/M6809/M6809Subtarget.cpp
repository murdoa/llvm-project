//===-- M6809Subtarget.cpp - M6809 Subtarget Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809Subtarget.h"
#include "M6809.h"
#include "M6809SelectionDAGInfo.h"

using namespace llvm;

#define DEBUG_TYPE "m6809-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "M6809GenSubtargetInfo.inc"

void M6809Subtarget::anchor() {}

M6809Subtarget &M6809Subtarget::initializeSubtargetDependencies(StringRef CPU,
                                                                StringRef FS) {
  ParseSubtargetFeatures(CPU, /*TuneCPU=*/CPU, FS);
  return *this;
}

M6809Subtarget::M6809Subtarget(const Triple &TT, const std::string &CPU,
                               const std::string &FS, const TargetMachine &TM)
    : M6809GenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), FrameLowering(*this),
      TLInfo(TM, *this) {
  TSInfo = std::make_unique<M6809SelectionDAGInfo>();
}

M6809Subtarget::~M6809Subtarget() = default;

const SelectionDAGTargetInfo *M6809Subtarget::getSelectionDAGInfo() const {
  return TSInfo.get();
}
