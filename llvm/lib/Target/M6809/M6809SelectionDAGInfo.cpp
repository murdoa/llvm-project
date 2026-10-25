//===-- M6809SelectionDAGInfo.cpp - M6809 SelectionDAG Info ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809SelectionDAGInfo.h"

#define GET_SDNODE_DESC
#include "M6809GenSDNodeInfo.inc"

using namespace llvm;

M6809SelectionDAGInfo::M6809SelectionDAGInfo()
    : SelectionDAGGenTargetInfo(M6809GenSDNodeInfo) {}

M6809SelectionDAGInfo::~M6809SelectionDAGInfo() = default;
