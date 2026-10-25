//===-- M6800Subtarget.cpp - M6800 Subtarget Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800Subtarget.h"
#include "M6800.h"
#include "M6800SelectionDAGInfo.h"

using namespace llvm;

#define DEBUG_TYPE "m6800-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "M6800GenSubtargetInfo.inc"

void M6800Subtarget::anchor() {}

M6800Subtarget &
M6800Subtarget::initializeSubtargetDependencies(StringRef CPU, StringRef FS) {
  ParseSubtargetFeatures(CPU, /*TuneCPU=*/CPU, FS);
  return *this;
}

M6800Subtarget::M6800Subtarget(const Triple &TT, const std::string &CPU,
                               const std::string &FS, const TargetMachine &TM)
    : M6800GenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), FrameLowering(*this),
      TLInfo(TM, *this) {
  TSInfo = std::make_unique<M6800SelectionDAGInfo>();
}

M6800Subtarget::~M6800Subtarget() = default;

const SelectionDAGTargetInfo *M6800Subtarget::getSelectionDAGInfo() const {
  return TSInfo.get();
}
