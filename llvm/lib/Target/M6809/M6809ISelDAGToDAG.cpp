//===-- M6809ISelDAGToDAG.cpp - A DAG pattern matching inst selector ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809.h"
#include "M6809SelectionDAGInfo.h"
#include "M6809Subtarget.h"
#include "M6809TargetMachine.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"

using namespace llvm;

#define DEBUG_TYPE "m6809-isel"
#define PASS_NAME "M6809 DAG->DAG Pattern Instruction Selection"

namespace {

class M6809DAGToDAGISel : public SelectionDAGISel {
  const M6809Subtarget *Subtarget = nullptr;

public:
  M6809DAGToDAGISel() = delete;
  M6809DAGToDAGISel(M6809TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    Subtarget = &MF.getSubtarget<M6809Subtarget>();
    return SelectionDAGISel::runOnMachineFunction(MF);
  }

  void Select(SDNode *N) override;

  SDValue getTargetSymbol(SDValue Sym, int64_t Off);
  bool SelectAddr(SDValue Addr, SDValue &Base, SDValue &Offset, SDValue &Mode);
  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintID,
                                    std::vector<SDValue> &OutOps) override;

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

// The target form of a wrapped address plus Off, if the address operand can
// hold it.
SDValue M6809DAGToDAGISel::getTargetSymbol(SDValue Sym, int64_t Off) {
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

// Picks the addressing mode for a load or store:
//  - frame index (+ constant)    -> off,S once the frame is laid out
//  - global/symbol (+ constant)  -> extended "sym+off", no base register
//  - reg + global/symbol         -> indexed "sym,X"
//  - constant address            -> extended "nnnn"
//  - anything else (+ constant)  -> indexed off,X / off,U / off,Y
bool M6809DAGToDAGISel::SelectAddr(SDValue Addr, SDValue &Base, SDValue &Offset,
                                   SDValue &Mode) {
  SDLoc DL(Addr);
  int64_t Off = 0;
  Mode = CurDAG->getTargetConstant(M6809Mem::Auto, DL, MVT::i8);

  if (auto *C = dyn_cast<ConstantSDNode>(Addr)) {
    Base = CurDAG->getRegister(Register(), MVT::i16);
    Offset =
        CurDAG->getTargetConstant(C->getZExtValue() & 0xFFFF, DL, MVT::i16);
    return true;
  }

  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    Off = cast<ConstantSDNode>(Addr.getOperand(1))->getSExtValue();
    Addr = Addr.getOperand(0);
  }

  if (Addr.getOpcode() == M6809ISD::WRAPPER) {
    if (SDValue Sym = getTargetSymbol(Addr.getOperand(0), Off)) {
      Base = CurDAG->getRegister(Register(), MVT::i16);
      Offset = Sym;
      return true;
    }
  }

  // reg + symbol: indexed with the symbol as a 16-bit offset ("ldb tab,x").
  if (Addr.getOpcode() == ISD::ADD) {
    for (unsigned I = 0; I != 2; ++I) {
      SDValue W = Addr.getOperand(I), R = Addr.getOperand(1 - I);
      if (W.getOpcode() != M6809ISD::WRAPPER || isa<ConstantSDNode>(R))
        continue;
      if (SDValue Sym = getTargetSymbol(W.getOperand(0), Off)) {
        Base = R;
        Offset = Sym;
        return true;
      }
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

// An inline asm memory operand is the base, offset and mode of an indexed
// operand, like an instruction's.
bool M6809DAGToDAGISel::SelectInlineAsmMemoryOperand(
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
        CurDAG->getTargetConstant(M6809::PTR16RegClassID, DL, MVT::i32);
    Base = SDValue(CurDAG->getMachineNode(TargetOpcode::COPY_TO_REGCLASS, DL,
                                          MVT::i16, Base, RC),
                   0);
  }
  OutOps.push_back(Base);
  OutOps.push_back(Offset);
  OutOps.push_back(Mode);
  return false;
}

void M6809DAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }

  // The address of a stack object: LEAX off,S once the frame is laid out.
  // TargetFrameIndex operands (of lifetime markers, for example) stay as
  // they are.
  if (auto *FIN = dyn_cast<FrameIndexSDNode>(N);
      FIN && N->getOpcode() == ISD::FrameIndex) {
    SDLoc DL(N);
    SDValue TFI = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i16);
    SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i16);
    SDValue Mode = CurDAG->getTargetConstant(M6809Mem::Auto, DL, MVT::i8);
    CurDAG->SelectNodeTo(N, M6809::LEA16, MVT::i16, TFI, Zero, Mode);
    return;
  }

  // Post-incremented and pre-decremented loads and stores (see
  // getPre/PostIndexedAddressParts).
  if (auto *LD = dyn_cast<LoadSDNode>(N);
      LD && LD->getAddressingMode() != ISD::UNINDEXED) {
    bool Byte = LD->getMemoryVT() == MVT::i8;
    bool Post = LD->getAddressingMode() == ISD::POST_INC;
    unsigned Opc = Post ? (Byte ? M6809::LD8pi : M6809::LD16pi)
                        : (Byte ? M6809::LD8pd : M6809::LD16pd);
    MachineSDNode *MN = CurDAG->getMachineNode(
        Opc, SDLoc(N), LD->getValueType(0),
        MVT::i16, MVT::Other, {LD->getBasePtr(), LD->getChain()});
    CurDAG->setNodeMemRefs(MN, {LD->getMemOperand()});
    ReplaceNode(N, MN);
    return;
  }
  if (auto *ST = dyn_cast<StoreSDNode>(N);
      ST && ST->getAddressingMode() != ISD::UNINDEXED) {
    bool Byte = ST->getMemoryVT() == MVT::i8;
    bool Post = ST->getAddressingMode() == ISD::POST_INC;
    unsigned Opc = Post ? (Byte ? M6809::ST8pi : M6809::ST16pi)
                        : (Byte ? M6809::ST8pd : M6809::ST16pd);
    MachineSDNode *MN = CurDAG->getMachineNode(
        Opc, SDLoc(N), MVT::i16, MVT::Other,
        {ST->getValue(), ST->getBasePtr(), ST->getChain()});
    CurDAG->setNodeMemRefs(MN, {ST->getMemOperand()});
    ReplaceNode(N, MN);
    return;
  }

  // HD6309 block copy and fill: the pseudos return the advanced pointers,
  // which nothing uses.
  if (N->getOpcode() == M6809ISD::TFMCPY ||
      N->getOpcode() == M6809ISD::TFMSET) {
    SDLoc DL(N);
    bool Cpy = N->getOpcode() == M6809ISD::TFMCPY;
    SDValue Len = N->getOperand(3);
    auto *C = dyn_cast<ConstantSDNode>(Len);
    if (C)
      Len = CurDAG->getTargetConstant(C->getZExtValue(), DL, MVT::i16);
    SDValue Ops[] = {N->getOperand(1), N->getOperand(2), Len, N->getOperand(0)};
    MachineSDNode *MN =
        Cpy ? CurDAG->getMachineNode(C ? M6809::MEMCPY16i : M6809::MEMCPY16, DL,
                                     MVT::i16, MVT::i16, MVT::Other, Ops)
            : CurDAG->getMachineNode(C ? M6809::MEMSET16i : M6809::MEMSET16, DL,
                                     MVT::i16, MVT::Other, Ops);
    ReplaceUses(SDValue(N, 0), SDValue(MN, Cpy ? 2 : 1));
    CurDAG->RemoveDeadNode(N);
    return;
  }

  SelectCode(N);
}
