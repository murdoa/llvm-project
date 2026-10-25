//===-- M6800ISelDAGToDAG.cpp - A DAG pattern matching inst selector ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800.h"
#include "M6800SelectionDAGInfo.h"
#include "M6800Subtarget.h"
#include "M6800TargetMachine.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"

using namespace llvm;

#define DEBUG_TYPE "m6800-isel"
#define PASS_NAME "M6800 DAG->DAG Pattern Instruction Selection"

namespace {

class M6800DAGToDAGISel : public SelectionDAGISel {
  const M6800Subtarget *Subtarget = nullptr;

public:
  M6800DAGToDAGISel() = delete;
  M6800DAGToDAGISel(M6800TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    Subtarget = &MF.getSubtarget<M6800Subtarget>();
    return SelectionDAGISel::runOnMachineFunction(MF);
  }

  void Select(SDNode *N) override;

  SDValue getTargetSymbol(SDValue Sym, int64_t Off);
  bool SelectAddr(SDValue Addr, SDValue &Base, SDValue &Offset, SDValue &Mode);
  SDValue findNearBase(SDValue X, int64_t Off);
  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintID,
                                    std::vector<SDValue> &OutOps) override;

#include "M6800GenDAGISel.inc"
};

class M6800DAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  M6800DAGToDAGISelLegacy(M6800TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<M6800DAGToDAGISel>(TM, OptLevel)) {}
};

char M6800DAGToDAGISelLegacy::ID;

} // namespace

INITIALIZE_PASS(M6800DAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createM6800ISelDag(M6800TargetMachine &TM,
                                       CodeGenOptLevel OptLevel) {
  return new M6800DAGToDAGISelLegacy(TM, OptLevel);
}

// The target form of a wrapped address plus Off, if the address operand can
// hold it.
SDValue M6800DAGToDAGISel::getTargetSymbol(SDValue Sym, int64_t Off) {
  SDLoc DL(Sym);
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Sym))
    return CurDAG->getTargetGlobalAddress(G->getGlobal(), DL, MVT::i16,
                                          G->getOffset() + Off);
  if (auto *B = dyn_cast<BlockAddressSDNode>(Sym))
    return CurDAG->getTargetBlockAddress(B->getBlockAddress(), MVT::i16,
                                         B->getOffset() + Off);
  if (Off != 0)
    return SDValue();
  if (auto *E = dyn_cast<ExternalSymbolSDNode>(Sym))
    return CurDAG->getTargetExternalSymbol(E->getSymbol(), MVT::i16);
  if (auto *J = dyn_cast<JumpTableSDNode>(Sym))
    return CurDAG->getTargetJumpTable(J->getIndex(), MVT::i16);
  return SDValue();
}

// Indexed offsets are unsigned bytes, so x + Off with Off outside 0..255
// needs its own sum. Another sum x + C with C <= Off < C + 256 can be the
// base instead: the lowest such C, so that one sum serves every access.
SDValue M6800DAGToDAGISel::findNearBase(SDValue X, int64_t Off) {
  SDValue Best;
  int64_t BestC = 0;
  for (SDNode *U : X->users()) {
    if (U->getOpcode() != ISD::ADD || U->getOperand(0) != X ||
        !isa<ConstantSDNode>(U->getOperand(1)))
      continue;
    int64_t C = cast<ConstantSDNode>(U->getOperand(1))->getSExtValue();
    if (C <= Off && Off - C <= 255 && (!Best || C < BestC)) {
      Best = SDValue(U, 0);
      BestC = C;
    }
  }
  return Best;
}

// Picks the addressing mode for a load or store:
//  - frame index (+ constant)    -> a stack slot, "off,x" after TSX
//  - global/symbol (+ constant)  -> extended "sym+off", no base register
//  - constant address            -> extended "nnnn"
//  - reg + 0..255                -> indexed "off,x"
//  - anything else               -> indexed "0,x" from the computed address
//
// A stack slot has SP as its base and an offset from SP + 1.
bool M6800DAGToDAGISel::SelectAddr(SDValue Addr, SDValue &Base, SDValue &Offset,
                                   SDValue &Mode) {
  SDLoc DL(Addr);
  int64_t Off = 0;
  Mode = CurDAG->getTargetConstant(M6800Mem::Auto, DL, MVT::i8);

  if (auto *C = dyn_cast<ConstantSDNode>(Addr)) {
    Base = CurDAG->getRegister(Register(), MVT::i16);
    Offset =
        CurDAG->getTargetConstant(C->getZExtValue() & 0xFFFF, DL, MVT::i16);
    return true;
  }

  SDValue Whole = Addr;
  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    Off = cast<ConstantSDNode>(Addr.getOperand(1))->getSExtValue();
    Addr = Addr.getOperand(0);
  }

  if (Addr.getOpcode() == M6800ISD::WRAPPER) {
    if (SDValue Sym = getTargetSymbol(Addr.getOperand(0), Off)) {
      Base = CurDAG->getRegister(Register(), MVT::i16);
      Offset = Sym;
      return true;
    }
  }

  if (auto *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i16);
  } else if (Addr.getOpcode() == ISD::CopyFromReg &&
             cast<RegisterSDNode>(Addr.getOperand(1))->getReg() == M6800::SP) {
    // Outgoing call arguments, at SP + 1 and up.
    Base = CurDAG->getRegister(M6800::SP, MVT::i16);
    --Off;
  } else if (Off >= 0 && Off <= 255) {
    // The indexed offset is an unsigned byte.
    Base = Addr;
  } else if (SDValue Near = findNearBase(Addr, Off)) {
    // x - 1 next to x - 2: "1,x" from the other address rather than a
    // second sum.
    Off -= cast<ConstantSDNode>(Near.getOperand(1))->getSExtValue();
    Base = Near;
  } else {
    Base = Whole;
    Off = 0;
  }

  Offset = CurDAG->getSignedTargetConstant(Off, DL, MVT::i16);
  return true;
}

// An inline asm memory operand is the base, offset and mode of an indexed
// operand, like an instruction's.
bool M6800DAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, InlineAsm::ConstraintCode ConstraintID,
    std::vector<SDValue> &OutOps) {
  switch (ConstraintID) {
  case InlineAsm::ConstraintCode::m:
  case InlineAsm::ConstraintCode::o:
  case InlineAsm::ConstraintCode::Q:
    break;
  default:
    return true;
  }
  SDValue Base, Offset, Mode;
  if (!SelectAddr(Op, Base, Offset, Mode))
    return true;
  // A computed base must be in a register indexed mode can use.
  if (!isa<RegisterSDNode>(Base) && !isa<FrameIndexSDNode>(Base)) {
    SDLoc DL(Op);
    SDValue RC =
        CurDAG->getTargetConstant(M6800::PTR16RegClassID, DL, MVT::i32);
    Base = SDValue(CurDAG->getMachineNode(TargetOpcode::COPY_TO_REGCLASS, DL,
                                          MVT::i16, Base, RC),
                   0);
  }
  OutOps.push_back(Base);
  OutOps.push_back(Offset);
  OutOps.push_back(Mode);
  return false;
}

void M6800DAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }

  // The address of a stack object, expanded by M6800FrameAccess.
  // TargetFrameIndex operands (of lifetime markers, for example) stay as
  // they are.
  if (auto *FIN = dyn_cast<FrameIndexSDNode>(N);
      FIN && N->getOpcode() == ISD::FrameIndex) {
    SDLoc DL(N);
    SDValue TFI = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i16);
    SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i16);
    SDValue Mode = CurDAG->getTargetConstant(M6800Mem::Auto, DL, MVT::i8);
    CurDAG->SelectNodeTo(N, M6800::LEAFI, MVT::i16, TFI, Zero, Mode);
    return;
  }

  SelectCode(N);
}
