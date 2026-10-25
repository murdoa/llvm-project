//===-- M6809ISelDAGToDAG.cpp - A DAG pattern matching inst selector ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809.h"
#include "M6809SelectionDAGInfo.h"
#include "M6809TargetMachine.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"

using namespace llvm;

#define DEBUG_TYPE "m6809-isel"
#define PASS_NAME "M6809 DAG->DAG Pattern Instruction Selection"

namespace {

class M6809DAGToDAGISel : public SelectionDAGISel {
public:
  M6809DAGToDAGISel() = delete;
  M6809DAGToDAGISel(M6809TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  void Select(SDNode *N) override;

  bool SelectAddr(SDValue Addr, SDValue &Base, SDValue &Offset);

#include "M6809GenDAGISel.inc"
};

class M6809DAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  M6809DAGToDAGISelLegacy(M6809TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<M6809DAGToDAGISel>(TM, OptLevel)) {}
};

char M6809DAGToDAGISelLegacy::ID;

} // namespace

INITIALIZE_PASS(M6809DAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createM6809ISelDag(M6809TargetMachine &TM,
                                       CodeGenOptLevel OptLevel) {
  return new M6809DAGToDAGISelLegacy(TM, OptLevel);
}

// Indexed addressing with a constant offset: off,X / off,U / off,Y / off,S.
// Frame indexes become off,S once the frame is laid out.
bool M6809DAGToDAGISel::SelectAddr(SDValue Addr, SDValue &Base,
                                   SDValue &Offset) {
  SDLoc DL(Addr);
  int64_t Off = 0;

  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    Off = cast<ConstantSDNode>(Addr.getOperand(1))->getSExtValue();
    Addr = Addr.getOperand(0);
  }

  if (auto *FIN = dyn_cast<FrameIndexSDNode>(Addr))
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i16);
  else
    Base = Addr;

  Offset = CurDAG->getTargetConstant(Off, DL, MVT::i16);
  return true;
}

void M6809DAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }
  SelectCode(N);
}
