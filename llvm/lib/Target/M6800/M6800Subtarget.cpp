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
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/SelectionDAGISel.h"

using namespace llvm;

#define DEBUG_TYPE "m6800-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "M6800GenSubtargetInfo.inc"

void M6800Subtarget::anchor() {}

M6800Subtarget &M6800Subtarget::initializeSubtargetDependencies(StringRef CPU,
                                                                StringRef FS) {
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

// Size-optimized functions are scheduled in source order. With so few
// registers the register-pressure scheduler often moves a use away from its
// def and pays for that with a spill, where the IR's own order is usually
// tight already. Together with no region splitting for these functions
// (M6800RegisterInfo::shouldRegionSplitForVirtReg) the c-torture suite is
// 6% smaller at -Oz. At -O2 source order is larger, so the
// register-pressure scheduler stays.
static ScheduleDAGSDNodes *createM6800DAGScheduler(SelectionDAGISel *IS,
                                                   CodeGenOptLevel OptLevel) {
  if (IS->MF->getFunction().hasOptSize())
    return createSourceListDAGScheduler(IS, OptLevel);
  return createBURRListDAGScheduler(IS, OptLevel);
}

RegisterScheduler::FunctionPassCtor
M6800Subtarget::getDAGScheduler(CodeGenOptLevel OptLevel) const {
  // At -O0 the default is already source order.
  return OptLevel == CodeGenOptLevel::None ? nullptr : createM6800DAGScheduler;
}
