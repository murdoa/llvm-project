//===-- M6809SelectionDAGInfo.cpp - M6809 SelectionDAG Info ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809SelectionDAGInfo.h"
#include "M6809Subtarget.h"
#include "llvm/CodeGen/SelectionDAG.h"

#define GET_SDNODE_DESC
#include "M6809GenSDNodeInfo.inc"

using namespace llvm;

M6809SelectionDAGInfo::M6809SelectionDAGInfo()
    : SelectionDAGGenTargetInfo(M6809GenSDNodeInfo) {}

M6809SelectionDAGInfo::~M6809SelectionDAGInfo() = default;

// On the HD6309, TFM copies or fills W bytes in a few bytes of code, for any
// length: it is used for every copy or fill not inlined as loads and stores.
SDValue M6809SelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align, Align, bool, bool, MachinePointerInfo,
    MachinePointerInfo) const {
  if (!DAG.getSubtarget<M6809Subtarget>().has6309()) {
    // A copy of a known length short enough for B to count.
    auto *C = dyn_cast<ConstantSDNode>(Size);
    if (!C || C->getZExtValue() == 0 || C->getZExtValue() > 255)
      return SDValue();
    return DAG.getNode(M6809ISD::COPYLOOP, DL, MVT::Other, Chain, Dst, Src,
                       DAG.getTargetConstant(C->getZExtValue(), DL, MVT::i8));
  }
  Size = DAG.getZExtOrTrunc(Size, DL, MVT::i16);
  return DAG.getNode(M6809ISD::TFMCPY, DL, MVT::Other, Chain, Dst, Src, Size);
}

SDValue M6809SelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Val,
    SDValue Size, Align, bool, bool, MachinePointerInfo) const {
  if (!DAG.getSubtarget<M6809Subtarget>().has6309()) {
    // A fill of a known value and length short enough for B to count.
    auto *C = dyn_cast<ConstantSDNode>(Size);
    auto *V = dyn_cast<ConstantSDNode>(Val);
    if (!C || !V || C->getZExtValue() == 0 || C->getZExtValue() > 255)
      return SDValue();
    return DAG.getNode(
        M6809ISD::SETLOOP, DL, MVT::Other, Chain, Dst,
        DAG.getTargetConstant(V->getZExtValue() & 0xFF, DL, MVT::i8),
        DAG.getTargetConstant(C->getZExtValue(), DL, MVT::i8));
  }
  Size = DAG.getZExtOrTrunc(Size, DL, MVT::i16);
  Val = DAG.getZExtOrTrunc(Val, DL, MVT::i8);
  return DAG.getNode(M6809ISD::TFMSET, DL, MVT::Other, Chain, Dst, Val, Size);
}
