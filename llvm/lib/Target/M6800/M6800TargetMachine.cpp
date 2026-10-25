//===-- M6800TargetMachine.cpp - Define TargetMachine for M6800 -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800TargetMachine.h"
#include "M6800.h"
#include "M6800MachineFunctionInfo.h"
#include "TargetInfo/M6800TargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeM6800Target() {
  RegisterTargetMachine<M6800TargetMachine> X(getTheM6800Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeM6800AsmPrinterPass(PR);
  initializeM6800DAGToDAGISelLegacyPass(PR);
  initializeM6800SplitDOperandsPass(PR);
  initializeM6800PostRACleanupPass(PR);
  initializeM6800FrameAccessPass(PR);
}

static StringRef defaultCPU(StringRef CPU) {
  return CPU.empty() ? StringRef("6301") : CPU;
}

M6800TargetMachine::M6800TargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT, defaultCPU(CPU), FS, Options,
                               RM.value_or(Reloc::Static),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(defaultCPU(CPU)), std::string(FS), *this) {
  initAsmInfo();
  // With two 8-bit and one 16-bit accumulator, a tied operand that stays
  // live often has no register to be copied into. TwoAddressInstruction
  // inserts the copy instead, which the fast allocator can spill around.
  setEnableTiedFastRegAlloc(false);
}

M6800TargetMachine::~M6800TargetMachine() = default;

namespace {

class M6800PassConfig : public TargetPassConfig {
public:
  M6800PassConfig(M6800TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  M6800TargetMachine &getM6800TargetMachine() const {
    return getTM<M6800TargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreRegAlloc() override;
  void addFastRegAlloc() override;
  FunctionPass *createTargetRegisterAllocator(bool Optimized) override;
  void addPreSched2() override;
};

} // namespace

TargetPassConfig *M6800TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new M6800PassConfig(*this, PM);
}

void M6800PassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool M6800PassConfig::addInstSelector() {
  addPass(createM6800ISelDag(getM6800TargetMachine(), getOptLevel()));
  return false;
}

// Every register class but GR16 has a single register, which the fast
// allocator cannot work with: an operand that needs X while X still holds a
// value about to be copied out runs it out of registers. -O0 uses the
// optimizing pipeline's allocator too.
void M6800PassConfig::addFastRegAlloc() { addOptimizedRegAlloc(); }

FunctionPass *M6800PassConfig::createTargetRegisterAllocator(bool) {
  return createGreedyRegisterAllocator();
}

void M6800PassConfig::addPreRegAlloc() {
  addPass(createM6800SplitDOperandsPass());
}

void M6800PassConfig::addPreSched2() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createM6800PostRACleanupPass());
  // Stack slots become "tsx" and an offset from X. After the cleanup, which
  // compares stack slot operands.
  addPass(createM6800FrameAccessPass());
}

MachineFunctionInfo *M6800TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return M6800FunctionInfo::create<M6800FunctionInfo>(Allocator, F, STI);
}
