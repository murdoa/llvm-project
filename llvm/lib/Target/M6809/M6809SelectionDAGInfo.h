//===-- M6809SelectionDAGInfo.h - M6809 SelectionDAG Info -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6809_M6809SELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_M6809_M6809SELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "M6809GenSDNodeInfo.inc"

namespace llvm {

class M6809SelectionDAGInfo : public SelectionDAGGenTargetInfo {
public:
  M6809SelectionDAGInfo();
  ~M6809SelectionDAGInfo() override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_M6809SELECTIONDAGINFO_H
