//===-- M6800SelectionDAGInfo.cpp - M6800 SelectionDAG Info ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800SelectionDAGInfo.h"

#define GET_SDNODE_DESC
#include "M6800GenSDNodeInfo.inc"

using namespace llvm;

M6800SelectionDAGInfo::M6800SelectionDAGInfo()
    : SelectionDAGGenTargetInfo(M6800GenSDNodeInfo) {}

M6800SelectionDAGInfo::~M6800SelectionDAGInfo() = default;
