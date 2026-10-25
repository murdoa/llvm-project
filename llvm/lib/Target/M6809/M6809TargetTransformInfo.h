//===-- M6809TargetTransformInfo.h - M6809 specific TTI ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Cost information for the IR passes.  The indexed modes ,R+ and ,R++ make
// a pointer that steps through memory cheaper than a counter plus an
// offset, so loops are given post-incremented pointers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6809_M6809TARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_M6809_M6809TARGETTRANSFORMINFO_H

#include "M6809Subtarget.h"
#include "M6809TargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/IR/Function.h"

namespace llvm {

class M6809TTIImpl final : public BasicTTIImplBase<M6809TTIImpl> {
  using BaseT = BasicTTIImplBase<M6809TTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const M6809Subtarget *ST;
  const M6809TargetLowering *TLI;

  const M6809Subtarget *getST() const { return ST; }
  const M6809TargetLowering *getTLI() const { return TLI; }

public:
  explicit M6809TTIImpl(const M6809TargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  TTI::AddressingModeKind
  getPreferredAddressingMode(const Loop *L, ScalarEvolution *SE) const override {
    return TTI::AMK_PostIndexed;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_M6809_M6809TARGETTRANSFORMINFO_H
