//===- M6800.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Motorola 6809 and Hitachi HD6309 are big-endian 8-bit processors with
// a 64K address space. Programs are usually ROM images laid out by a linker
// script, so there is no dynamic linking support.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class M6800 final : public TargetInfo {
public:
  M6800(Ctx &);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

M6800::M6800(Ctx &ctx) : TargetInfo(ctx) {
  // Fill gaps in code with SWI.
  trapInstr = {0x3F, 0x3F, 0x3F, 0x3F};
  // Everything fits in 64K: no page alignment between segments, and
  // programs start at 0 unless a linker script says otherwise.
  defaultCommonPageSize = 1;
  defaultMaxPageSize = 1;
  defaultImageBase = 0;
}

RelExpr M6800::getRelExpr(RelType type, const Symbol &s,
                          const uint8_t *loc) const {
  switch (type) {
  case R_M6800_PCREL8:
    return R_PC;
  case R_M6800_NONE:
    return R_NONE;
  default:
    return R_ABS;
  }
}

void M6800::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_M6800_8:
    checkIntUInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_M6800_16:
    checkIntUInt(ctx, loc, val, 16, rel);
    write16be(loc, val);
    break;
  case R_M6800_32:
    write32be(loc, val);
    break;
  case R_M6800_DIR8:
    // Direct addressing reaches the first 256 bytes only.
    checkUInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_M6800_PCREL8: {
    // The offset counts from the end of the instruction, which is the
    // byte after it.
    int64_t off = static_cast<int64_t>(val) - 1;
    checkInt(ctx, loc, off, 8, rel);
    *loc = off;
    break;
  }
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

void elf::setM6800TargetInfo(Ctx &ctx) { ctx.target.reset(new M6800(ctx)); }
