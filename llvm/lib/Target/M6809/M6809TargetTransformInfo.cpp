//===-- M6809TargetTransformInfo.cpp - M6809 specific TTI -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809TargetTransformInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

// The loop's counter, compare and branch, and the test before the first
// iteration of a loop that optsize leaves unrotated, cost more than their
// IR suggests. Over the torture suite at -Oz, 8 shrinks 39 files by 9.7 KB
// and grows 10 by 260 bytes; 16 starts to grow the size benchmarks.
static cl::opt<unsigned> OptSizeUnrollBonus(
    "m6809-optsize-unroll-bonus", cl::Hidden, cl::init(8),
    cl::desc("Cost added to a loop's size for its optsize full-unroll "
             "threshold"));

// There is a post-increment and a pre-decrement, but neither a
// post-decrement nor a pre-increment.  A loop with an access that steps
// down gets pre-indexed pointers.
TTI::AddressingModeKind
M6809TTIImpl::getPreferredAddressingMode(const Loop *L,
                                         ScalarEvolution *SE) const {
  if (!SE)
    return TTI::AMK_PostIndexed;
  for (const BasicBlock *BB : L->blocks())
    for (const Instruction &I : *BB) {
      const Value *Ptr = getLoadStorePointerOperand(&I);
      if (!Ptr || !SE->isSCEVable(Ptr->getType()))
        continue;
      if (auto *AR =
              dyn_cast<SCEVAddRecExpr>(SE->getSCEV(const_cast<Value *>(Ptr))))
        if (AR->getLoop() == L)
          if (auto *Step = dyn_cast<SCEVConstant>(AR->getStepRecurrence(*SE)))
            if (Step->getAPInt().isNegative())
              return TTI::AMK_PreIndexed;
    }
  return TTI::AMK_PostIndexed;
}

// As the default, but a small offset is free: "ldb -1,x" costs what
// "ldb ,x" does, so the immediate cost only breaks ties.
bool M6809TTIImpl::isLSRCostLess(const TTI::LSRCost &C1,
                                 const TTI::LSRCost &C2) const {
  return std::tie(C1.NumRegs, C1.AddRecCost, C1.NumIVMuls, C1.NumBaseAdds,
                  C1.ScaleCost, C1.SetupCost, C1.ImmCost) <
         std::tie(C2.NumRegs, C2.AddRecCost, C2.NumIVMuls, C2.NumBaseAdds,
                  C2.ScaleCost, C2.SetupCost, C2.ImmCost);
}

// At optsize LoopUnroll's threshold is OptSizeThreshold, 0 by default, so no
// loop is ever fully unrolled. But a loop that runs a few times often costs
// more in its counter, compare and branch than its copies would, once
// each copy is simplified for its iteration (the first one of a state
// machine knows the starting state). Use the loop's own size as the
// threshold: LoopUnroll then unrolls only when its simulation of the
// simplified copies comes out smaller than the loop.
void M6809TTIImpl::getUnrollingPreferences(
    Loop *L, ScalarEvolution &SE, TTI::UnrollingPreferences &UP,
    OptimizationRemarkEmitter *ORE) const {
  BaseT::getUnrollingPreferences(L, SE, UP, ORE);
  InstructionCost Size = 0;
  for (const BasicBlock *BB : L->blocks())
    for (const Instruction &I : *BB) {
      SmallVector<const Value *, 4> Ops(I.operand_values());
      Size += getInstructionCost(&I, Ops, TTI::TCK_CodeSize);
    }
  if (Size.isValid())
    UP.OptSizeThreshold = Size.getValue() + OptSizeUnrollBonus;
}
