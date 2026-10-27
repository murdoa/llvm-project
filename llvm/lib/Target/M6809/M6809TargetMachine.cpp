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
#include "M6809TargetTransformInfo.h"
#include "TargetInfo/M6809TargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeM6809Target() {
  RegisterTargetMachine<M6809TargetMachine> X(getTheM6809Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeM6809AsmPrinterPass(PR);
  initializeM6809DAGToDAGISelLegacyPass(PR);
  initializeM6809SplitDOperandsPass(PR);
  initializeM6809PostRACleanupPass(PR);
  initializeM6809DirectPagePass(PR);
}

namespace {
// A zero-initialised variable in .dp.bss takes no space in the object file,
// as one in .bss does: an explicit section otherwise makes it data.
class M6809TargetObjectFile : public TargetLoweringObjectFileELF {
  // Code needs no alignment: no filler before each object's .text.
  unsigned getTextSectionAlignment() const override { return 1; }
  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override {
    StringRef Name = GO->getSection();
    if (auto *GV = dyn_cast<GlobalVariable>(GO))
      if ((Name == ".dp.bss" || Name.starts_with(".dp.bss.")) &&
          GV->hasInitializer() && GV->getInitializer()->isNullValue() &&
          !GV->isConstant())
        Kind = SectionKind::getBSS();
    return TargetLoweringObjectFileELF::getExplicitSectionGlobal(GO, Kind, TM);
  }
};
} // namespace

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
      TLOF(std::make_unique<M6809TargetObjectFile>()) {
  initAsmInfo();
  // With two 8-bit and one 16-bit accumulator, a tied operand that stays
  // live often has no register to be copied into. TwoAddressInstruction
  // inserts the copy instead, which the fast allocator can spill around.
  setEnableTiedFastRegAlloc(false);
  // Outline repeated sequences in minsize functions; a JSR costs three
  // bytes and the outlined RTS one.
  setMachineOutliner(true);
  setSupportsDefaultOutlining(true);
}

M6809TargetMachine::~M6809TargetMachine() = default;

const M6809Subtarget *
M6809TargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");
  std::string CPU = std::string(
      defaultCPU(CPUAttr.isValid() ? CPUAttr.getValueAsString() : TargetCPU));
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;
  std::unique_ptr<M6809Subtarget> &ST = SubtargetMap[CPU + FS];
  if (!ST)
    ST = std::make_unique<M6809Subtarget>(TargetTriple, CPU, FS, *this);
  return ST.get();
}

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
  void addPreRegAlloc() override;
  void addFastRegAlloc() override;
  void addPreSched2() override;
};

} // namespace

TargetTransformInfo
M6809TargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<M6809TTIImpl>(this, F));
}

TargetPassConfig *M6809TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new M6809PassConfig(*this, PM);
}

void M6809PassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  addPass(createM6809DirectPagePass());
  TargetPassConfig::addIRPasses();
}

bool M6809PassConfig::addInstSelector() {
  addPass(createM6809ISelDag(getM6809TargetMachine(), getOptLevel()));
  return false;
}

// Zero and sign extension insert B into an IMPLICIT_DEF of D. The fast
// allocator can only give B to the inserted value if the rest of D is known
// to be undefined, which ProcessImplicitDefs records, as the optimizing
// pipeline does.
void M6809PassConfig::addFastRegAlloc() {
  addPass(&ProcessImplicitDefsID);
  TargetPassConfig::addFastRegAlloc();
}

void M6809PassConfig::addPreRegAlloc() {
  addPass(createM6809SplitDOperandsPass());
}

void M6809PassConfig::addPreSched2() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createM6809PostRACleanupPass());
}

MachineFunctionInfo *M6809TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return M6809FunctionInfo::create<M6809FunctionInfo>(Allocator, F, STI);
}
