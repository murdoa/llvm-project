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

  bool SelectAddr(SDValue Addr, SDValue &Base, SDValue &Offset,
                  SDValue &Mode);

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

// Picks the addressing mode for a load or store:
//  - frame index (+ constant)    -> off,S once the frame is laid out
//  - global/symbol (+ constant)  -> extended "sym+off", no base register
//  - constant address            -> extended "nnnn"
//  - anything else (+ constant)  -> indexed off,X / off,U / off,Y
bool M6809DAGToDAGISel::SelectAddr(SDValue Addr, SDValue &Base,
                                   SDValue &Offset, SDValue &Mode) {
  SDLoc DL(Addr);
  int64_t Off = 0;
  Mode = CurDAG->getTargetConstant(M6809Mem::Auto, DL, MVT::i8);

  if (auto *C = dyn_cast<ConstantSDNode>(Addr)) {
    Base = CurDAG->getRegister(Register(), MVT::i16);
    Offset = CurDAG->getTargetConstant(C->getZExtValue() & 0xFFFF, DL,
                                       MVT::i16);
    return true;
  }

  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    Off = cast<ConstantSDNode>(Addr.getOperand(1))->getSExtValue();
    Addr = Addr.getOperand(0);
  }

  if (Addr.getOpcode() == M6809ISD::WRAPPER) {
    SDValue Sym = Addr.getOperand(0);
    if (auto *G = dyn_cast<GlobalAddressSDNode>(Sym)) {
      Base = CurDAG->getRegister(Register(), MVT::i16);
      Offset = CurDAG->getTargetGlobalAddress(G->getGlobal(), DL, MVT::i16,
                                              G->getOffset() + Off);
      return true;
    }
    if (auto *E = dyn_cast<ExternalSymbolSDNode>(Sym); E && Off == 0) {
      Base = CurDAG->getRegister(Register(), MVT::i16);
      Offset = CurDAG->getTargetExternalSymbol(E->getSymbol(), MVT::i16);
      return true;
    }
  }

  if (auto *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i16);
  } else if (Addr.getOpcode() == ISD::CopyFromReg &&
             cast<RegisterSDNode>(Addr.getOperand(1))->getReg() == M6809::S) {
    // Outgoing call arguments are stored S-relative.
    Base = CurDAG->getRegister(M6809::S, MVT::i16);
  } else {
    Base = Addr;
  }

  Offset = CurDAG->getSignedTargetConstant(Off, DL, MVT::i16);
  return true;
}

void M6809DAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }

  // The address of a stack object: LEAX off,S once the frame is laid out.
  if (auto *FIN = dyn_cast<FrameIndexSDNode>(N)) {
    SDLoc DL(N);
    SDValue TFI = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i16);
    SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i16);
    SDValue Mode = CurDAG->getTargetConstant(M6809Mem::Auto, DL, MVT::i8);
    CurDAG->SelectNodeTo(N, M6809::LEA16, MVT::i16, TFI, Zero, Mode);
    return;
  }

  SelectCode(N);
}
