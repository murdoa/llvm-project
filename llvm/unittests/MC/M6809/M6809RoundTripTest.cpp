//===- M6809RoundTripTest.cpp - Decode then encode every 6809 opcode ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Every byte sequence the disassembler accepts must encode back to the same
// bytes, for every opcode on every page and every indexed postbyte.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/M6809FixupKinds.h"
#include "MCTargetDesc/M6809MCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "gtest/gtest.h"

using namespace llvm;

extern "C" void LLVMInitializeM6809TargetInfo();
extern "C" void LLVMInitializeM6809TargetMC();
extern "C" void LLVMInitializeM6809Disassembler();

namespace {

struct Context {
  Triple TT{"m6809"};
  MCTargetOptions Options;
  std::unique_ptr<MCRegisterInfo> MRI;
  std::unique_ptr<MCAsmInfo> MAI;
  std::unique_ptr<MCInstrInfo> MII;
  std::unique_ptr<MCSubtargetInfo> STI;
  std::unique_ptr<MCContext> Ctx;
  std::unique_ptr<MCDisassembler> DisAsm;
  std::unique_ptr<MCCodeEmitter> Emitter;

  Context() {
    LLVMInitializeM6809TargetInfo();
    LLVMInitializeM6809TargetMC();
    LLVMInitializeM6809Disassembler();
    std::string Error;
    const Target *T = TargetRegistry::lookupTarget(TT, Error);
    if (!T)
      return;
    MRI.reset(T->createMCRegInfo(TT));
    MAI.reset(T->createMCAsmInfo(*MRI, TT, Options));
    MII.reset(T->createMCInstrInfo());
    STI.reset(T->createMCSubtargetInfo(TT, "6809", ""));
    Ctx = std::make_unique<MCContext>(TT, *MAI, *MRI, *STI);
    DisAsm.reset(T->createMCDisassembler(*STI, *Ctx));
    Emitter.reset(T->createMCCodeEmitter(*MII, *Ctx));
  }
};

Context &getContext() {
  static Context C;
  return C;
}

// Encodes MI as it would be at Address, resolving its PC-relative fixups.
SmallVector<uint8_t, 8> encodeAt(const MCInst &MI, uint64_t Address) {
  Context &C = getContext();
  SmallVector<char, 8> Bytes;
  SmallVector<MCFixup, 2> Fixups;
  C.Emitter->encodeInstruction(MI, Bytes, Fixups, *C.STI);
  for (const MCFixup &F : Fixups) {
    int64_t Target;
    EXPECT_TRUE(F.getValue()->evaluateAsAbsolute(Target));
    unsigned Size = F.getKind() == M6809::fixup_pcrel8 ? 1 : 2;
    EXPECT_TRUE(F.getKind() == M6809::fixup_pcrel8 ||
                F.getKind() == M6809::fixup_pcrel16);
    // Offsets count from the end of the field, which ends the instruction.
    int64_t Off = Target - int64_t(Address + F.getOffset() + Size);
    for (unsigned I = 0; I != Size; ++I)
      Bytes[F.getOffset() + I] = char(Off >> (8 * (Size - 1 - I)));
  }
  return SmallVector<uint8_t, 8>(Bytes.begin(), Bytes.end());
}

TEST(M6809RoundTrip, EveryEncoding) {
  Context &C = getContext();
  if (!C.DisAsm)
    GTEST_SKIP();

  const uint64_t Address = 0x4000;
  unsigned Decoded = 0;
  for (int Prefix : {-1, 0x10, 0x11}) {
    for (unsigned Op = 0; Op != 256; ++Op) {
      for (unsigned Post = 0; Post != 256; ++Post) {
        SmallVector<uint8_t, 8> In;
        if (Prefix >= 0)
          In.push_back(Prefix);
        In.append({uint8_t(Op), uint8_t(Post), 0xA5, 0x5A, 0x3C});
        MCInst MI;
        uint64_t Size;
        if (C.DisAsm->getInstruction(MI, Size, In, Address, nulls()) !=
            MCDisassembler::Success)
          continue;
        ++Decoded;
        SmallVector<uint8_t, 8> Out = encodeAt(MI, Address);
        // The register bits of the PC-relative postbytes are ignored by the
        // processor; the encoder writes them as zero.
        unsigned N = MI.getNumOperands();
        bool PCBase = N >= 3 && MI.getOperand(N - 3).isReg() &&
                      MI.getOperand(N - 3).getReg() == M6809::PC;
        if (PCBase) {
          unsigned PostPos = Prefix >= 0 ? 2 : 1;
          In[PostPos] &= ~0x60;
        }
        ASSERT_EQ(Out.size(), Size)
            << "prefix " << Prefix << " opcode " << Op << " postbyte " << Post;
        for (unsigned I = 0; I != Size; ++I)
          ASSERT_EQ(Out[I], In[I]) << "prefix " << Prefix << " opcode " << Op
                                   << " postbyte " << Post << " byte " << I;
      }
    }
  }
  EXPECT_GT(Decoded, 0u);
}

} // namespace
