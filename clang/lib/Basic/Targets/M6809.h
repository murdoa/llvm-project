//===--- M6809.h - Declare M6809 target feature support ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Motorola 6809 / Hitachi HD6309 TargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_M6809_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_M6809_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY M6809TargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];
  std::string CPU = "6809";

public:
  M6809TargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    TLSSupported = false;
    BigEndian = true;
    // 16-bit int and pointers. Nothing needs more than byte alignment.
    PointerWidth = 16;
    PointerAlign = 8;
    ShortAlign = 8;
    IntWidth = 16;
    IntAlign = 8;
    LongWidth = 32;
    LongAlign = 8;
    LongLongWidth = 64;
    LongLongAlign = 8;
    SuitableAlign = 8;
    DefaultAlignForAttributeAligned = 8;
    HalfWidth = 16;
    HalfAlign = 8;
    FloatWidth = 32;
    FloatAlign = 8;
    DoubleWidth = 64;
    DoubleAlign = 8;
    DoubleFormat = &llvm::APFloat::IEEEdouble();
    LongDoubleWidth = 64;
    LongDoubleAlign = 8;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    SizeType = UnsignedInt;
    PtrDiffType = SignedInt;
    IntPtrType = SignedInt;
    IntMaxType = SignedLongLong;
    Char16Type = UnsignedInt;
    Char32Type = UnsignedLong;
    WCharType = SignedInt;
    WIntType = SignedInt;
    SigAtomicType = SignedInt;
    // One CPU: 8- and 16-bit atomics are lock-free.  The backend makes
    // loads and stores plain ones and calls __sync_* for the rest.
    MaxAtomicPromoteWidth = 16;
    MaxAtomicInlineWidth = 16;
    resetDataLayout();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  bool allowsLargerPreferedTypeAlignment() const override { return false; }

  bool hasFeature(StringRef Feature) const override {
    return Feature == "m6809" || (Feature == "6309" && CPU == "6309");
  }

  bool isValidCPUName(StringRef Name) const override {
    return Name == "6809" || Name == "6309" || Name == "generic";
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override {
    Values.append({"6809", "6309", "generic"});
  }

  bool setCPU(StringRef Name) override {
    if (!isValidCPUName(Name))
      return false;
    CPU = Name == "generic" ? "6809" : Name.str();
    return true;
  }

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return {};
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    // As in the backend.  One register of any other kind is a register
    // variable: register char c __asm__("a").
    switch (*Name) {
    case 'a': // an index register: X, U or Y
    case 'q': // an 8-bit accumulator: A or B
    case 'd': // D, or B for an 8-bit value
      Info.setAllowsRegister();
      return true;
    }
    return false;
  }

  std::string_view getClobbers() const override { return ""; }

  // __builtin_setjmp and __builtin_longjmp.
  bool hasSjLjLowering() const override { return true; }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_M6809_H
