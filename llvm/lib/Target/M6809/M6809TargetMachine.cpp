//===-- M6809TargetMachine.cpp - Define TargetMachine for M6809 -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809TargetMachine.h"
#include "M6809.h"
#include "M6809MachineFunctionInfo.h"
#include "TargetInfo/M6809TargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeM6809Target() {
  RegisterTargetMachine<M6809TargetMachine> X(getTheM6809Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeM6809AsmPrinterPass(PR);
  initializeM6809DAGToDAGISelLegacyPass(PR);
}

static StringRef defaultCPU(StringRef CPU) {
  return CPU.empty() ? StringRef("6809") : CPU;
}

M6809TargetMachine::M6809TargetMachine(const Target &T, const Triple &TT,
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
}

M6809TargetMachine::~M6809TargetMachine() = default;

namespace {

class M6809PassConfig : public TargetPassConfig {
public:
  M6809PassConfig(M6809TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  M6809TargetMachine &getM6809TargetMachine() const {
    return getTM<M6809TargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
};

} // namespace

TargetPassConfig *M6809TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new M6809PassConfig(*this, PM);
}

void M6809PassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool M6809PassConfig::addInstSelector() {
  addPass(createM6809ISelDag(getM6809TargetMachine(), getOptLevel()));
  return false;
}

MachineFunctionInfo *M6809TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return M6809FunctionInfo::create<M6809FunctionInfo>(Allocator, F, STI);
}
