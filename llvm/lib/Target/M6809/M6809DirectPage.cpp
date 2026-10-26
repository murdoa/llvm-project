//===-- M6809DirectPage.cpp - Move hot variables to the direct page -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A variable in a .dp or .dp.* section is reached with a one-byte direct
// address ("ldb <var"), a byte shorter than the extended ">var" (see
// M6809MCInstLower). With -m6809-direct-page-bytes=N this pass picks
// zero-initialised variables for the section .dp.bss itself, up to N bytes
// less what the module already puts in .dp sections, choosing those that
// save the most bytes: one for each load or store of a fixed address.
//
// The choice is per module, so the option is meant for a whole program in
// one module (LTO); the linker script places .dp.bss in the page DP points
// at, keeps it there, and the startup code clears it.
//
//===----------------------------------------------------------------------===//

#include "M6809.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "m6809-direct-page"

static cl::opt<unsigned> DirectPageBytes(
    "m6809-direct-page-bytes", cl::init(0), cl::Hidden,
    cl::desc("Bytes of the direct page to fill with the variables that "
             "are loaded and stored most (0: none)"));

namespace {

class M6809DirectPage : public ModulePass {
public:
  static char ID;
  M6809DirectPage() : ModulePass(ID) {}
  bool runOnModule(Module &M) override;
  StringRef getPassName() const override { return "M6809 direct page"; }
};

} // namespace

char M6809DirectPage::ID = 0;

INITIALIZE_PASS(M6809DirectPage, DEBUG_TYPE, "M6809 direct page", false, false)

static bool inDirectPage(const GlobalObject &GO) {
  if (!GO.hasSection())
    return false;
  StringRef Name = GO.getSection();
  return Name == ".dp" || Name.starts_with(".dp.");
}

// Bytes saved by direct addressing: one per load or store at an address
// fixed at link time, counted in the 16-bit pieces wider values take.
static unsigned savings(const Value *V, const DataLayout &DL) {
  unsigned N = 0;
  for (const User *U : V->users()) {
    Type *Ty = nullptr;
    if (auto *L = dyn_cast<LoadInst>(U))
      Ty = L->getType();
    else if (auto *S = dyn_cast<StoreInst>(U)) {
      if (S->getPointerOperand() == V)
        Ty = S->getValueOperand()->getType();
    } else if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      if (CE->getOpcode() == Instruction::GetElementPtr)
        N += savings(CE, DL);
      continue;
    }
    if (Ty && Ty->isSized())
      N += std::max<uint64_t>(1, (DL.getTypeStoreSize(Ty) + 1) / 2);
  }
  return N;
}

bool M6809DirectPage::runOnModule(Module &M) {
  if (!DirectPageBytes || skipModule(M))
    return false;
  const DataLayout &DL = M.getDataLayout();
  uint64_t Used = 0;
  struct Candidate {
    GlobalVariable *GV;
    unsigned Size, Gain;
  };
  SmallVector<Candidate, 64> Cands;
  for (GlobalVariable &GV : M.globals()) {
    if (GV.isDeclaration() || !GV.getValueType()->isSized())
      continue;
    uint64_t Size = DL.getTypeAllocSize(GV.getValueType());
    if (inDirectPage(GV)) {
      Used += Size;
      continue;
    }
    if (GV.hasSection() || GV.isConstant() || GV.isThreadLocal() ||
        GV.hasComdat() || !GV.hasInitializer() ||
        !GV.getInitializer()->isNullValue() || GV.isInterposable() ||
        GV.getAddressSpace() != 0 || Size == 0 || Size > DirectPageBytes)
      continue;
    unsigned Gain = savings(&GV, DL);
    if (Gain)
      Cands.push_back({&GV, unsigned(Size), Gain});
  }
  if (Used >= DirectPageBytes || Cands.empty())
    return false;

  // A 0/1 knapsack on the bytes left.
  unsigned Cap = DirectPageBytes - Used;
  std::vector<unsigned> Best(Cap + 1, 0);
  std::vector<std::vector<bool>> Take(Cands.size(),
                                      std::vector<bool>(Cap + 1, false));
  for (unsigned I = 0; I != Cands.size(); ++I)
    for (unsigned W = Cap; W >= Cands[I].Size; --W)
      if (Best[W - Cands[I].Size] + Cands[I].Gain > Best[W]) {
        Best[W] = Best[W - Cands[I].Size] + Cands[I].Gain;
        Take[I][W] = true;
      }
  bool Changed = false;
  for (unsigned I = Cands.size(), W = Cap; I-- > 0;)
    if (Take[I][W]) {
      Cands[I].GV->setSection(".dp.bss");
      W -= Cands[I].Size;
      Changed = true;
    }
  return Changed;
}

ModulePass *llvm::createM6809DirectPagePass() { return new M6809DirectPage(); }
