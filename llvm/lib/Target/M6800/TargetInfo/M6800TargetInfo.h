//===-- M6800TargetInfo.h - M6800 Target Implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6800_TARGETINFO_M6800TARGETINFO_H
#define LLVM_LIB_TARGET_M6800_TARGETINFO_M6800TARGETINFO_H

namespace llvm {

class Target;

Target &getTheM6800Target();

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6800_TARGETINFO_M6800TARGETINFO_H
