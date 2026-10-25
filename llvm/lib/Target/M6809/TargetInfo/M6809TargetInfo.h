//===-- M6809TargetInfo.h - M6809 Target Implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6809_TARGETINFO_M6809TARGETINFO_H
#define LLVM_LIB_TARGET_M6809_TARGETINFO_M6809TARGETINFO_H

namespace llvm {

class Target;

Target &getTheM6809Target();

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_TARGETINFO_M6809TARGETINFO_H
