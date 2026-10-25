//===-- M6800SelectionDAGInfo.h - M6800 SelectionDAG Info -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6800_M6800SELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_M6800_M6800SELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "M6800GenSDNodeInfo.inc"

namespace llvm {

class M6800SelectionDAGInfo : public SelectionDAGGenTargetInfo {
public:
  M6800SelectionDAGInfo();
  ~M6800SelectionDAGInfo() override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6800_M6800SELECTIONDAGINFO_H
