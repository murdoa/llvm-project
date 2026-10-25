//===-- M6809ISelLowering.cpp - M6809 DAG Lowering Implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809ISelLowering.h"
#include "M6809.h"
#include "M6809SelectionDAGInfo.h"
#include "M6809Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "m6809-lower"

#define GET_CALLING_CONV_IMPL
#include "M6809GenCallingConv.inc"

// Return address pushed by JSR/BSR, between the incoming S and the first
// stack argument.
static constexpr int ReturnAddressSize = 2;

M6809TargetLowering::M6809TargetLowering(const TargetMachine &TM,
                                         const M6809Subtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  addRegisterClass(MVT::i8, &M6809::ACC8RegClass);
  addRegisterClass(MVT::i16, &M6809::GR16RegClass);
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(M6809::S);
  setBooleanContents(ZeroOrOneBooleanContent);
  setSchedulingPreference(Sched::RegPressure);
  setMinFunctionAlignment(Align(1));
  setPrefFunctionAlignment(Align(1));

  // Loads only produce whole registers. Extending loads become a load plus
  // SEX/CLRA, and truncating stores a STB of the low byte.
  for (MVT VT : {MVT::i8, MVT::i16}) {
    for (auto Ext : {ISD::EXTLOAD, ISD::ZEXTLOAD, ISD::SEXTLOAD}) {
      setLoadExtAction(Ext, VT, MVT::i1, Promote);
      if (VT == MVT::i16)
        setLoadExtAction(Ext, VT, MVT::i8, Expand);
    }
  }
  setTruncStoreAction(MVT::i16, MVT::i8, Expand);

  // SEX covers i8-in-i16; smaller widths become shifts.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  setOperationAction(ISD::GlobalAddress, MVT::i16, Custom);
  setOperationAction(ISD::ExternalSymbol, MVT::i16, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i16, Custom);

  // Every compare ends up as BR_CC or SELECT_CC, which keep the compare and
  // its flag user together.
  for (MVT VT : {MVT::i8, MVT::i16}) {
    setOperationAction(ISD::BR_CC, VT, Custom);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
    setOperationAction(ISD::SETCC, VT, Expand);
    setOperationAction(ISD::SELECT, VT, Expand);
  }
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  // TODO: everything not listed here is left Legal and fails instruction
  // selection until it is added. Next up: shifts, multiply, divide and
  // calls.
}

//===----------------------------------------------------------------------===//
// Custom lowering
//===----------------------------------------------------------------------===//

SDValue M6809TargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return lowerGlobalAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return lowerExternalSymbol(Op, DAG);
  case ISD::BlockAddress:
    return lowerBlockAddress(Op, DAG);
  case ISD::BR_CC:
    return lowerBR_CC(Op, DAG);
  case ISD::SELECT_CC:
    return lowerSELECT_CC(Op, DAG);
  default:
    llvm_unreachable("M6809: unexpected operation to lower");
  }
}

SDValue M6809TargetLowering::lowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  auto *N = cast<GlobalAddressSDNode>(Op);
  SDLoc DL(Op);
  SDValue GA = DAG.getTargetGlobalAddress(N->getGlobal(), DL, MVT::i16,
                                          N->getOffset());
  return DAG.getNode(M6809ISD::WRAPPER, DL, MVT::i16, GA);
}

SDValue M6809TargetLowering::lowerExternalSymbol(SDValue Op,
                                                 SelectionDAG &DAG) const {
  auto *N = cast<ExternalSymbolSDNode>(Op);
  SDLoc DL(Op);
  SDValue Sym = DAG.getTargetExternalSymbol(N->getSymbol(), MVT::i16);
  return DAG.getNode(M6809ISD::WRAPPER, DL, MVT::i16, Sym);
}

SDValue M6809TargetLowering::lowerBlockAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  auto *N = cast<BlockAddressSDNode>(Op);
  SDLoc DL(Op);
  SDValue BA = DAG.getTargetBlockAddress(N->getBlockAddress(), MVT::i16,
                                         N->getOffset());
  return DAG.getNode(M6809ISD::WRAPPER, DL, MVT::i16, BA);
}

// Maps an integer condition to a 6809 branch condition. A constant LHS is
// swapped to the right first, since only the right side can be an immediate.
static M6809CC::CondCode prepareCompare(SDValue &LHS, SDValue &RHS,
                                        ISD::CondCode CC) {
  if (isa<ConstantSDNode>(LHS) && !isa<ConstantSDNode>(RHS)) {
    std::swap(LHS, RHS);
    CC = ISD::getSetCCSwappedOperands(CC);
  }
  switch (CC) {
  case ISD::SETEQ: return M6809CC::EQ;
  case ISD::SETNE: return M6809CC::NE;
  case ISD::SETUGT: return M6809CC::HI;
  case ISD::SETUGE: return M6809CC::HS;
  case ISD::SETULT: return M6809CC::LO;
  case ISD::SETULE: return M6809CC::LS;
  case ISD::SETGT: return M6809CC::GT;
  case ISD::SETGE: return M6809CC::GE;
  case ISD::SETLT: return M6809CC::LT;
  case ISD::SETLE: return M6809CC::LE;
  default:
    llvm_unreachable("M6809: unsupported integer condition");
  }
}

SDValue M6809TargetLowering::lowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  M6809CC::CondCode MCC = prepareCompare(LHS, RHS, CC);
  return DAG.getNode(M6809ISD::CMPBR, DL, MVT::Other, Chain, LHS, RHS,
                     DAG.getTargetConstant(MCC, DL, MVT::i8), Dest);
}

SDValue M6809TargetLowering::lowerSELECT_CC(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TVal = Op.getOperand(2);
  SDValue FVal = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);

  M6809CC::CondCode MCC = prepareCompare(LHS, RHS, CC);
  return DAG.getNode(M6809ISD::SELECT_CC, DL, Op.getValueType(), TVal, FVal,
                     LHS, RHS, DAG.getTargetConstant(MCC, DL, MVT::i8));
}

// SELECT* pseudos become:
//
//   ThisMBB:  CB*rr lhs, rhs, cc, SinkMBB    ; taken: result = tval
//   FalseMBB: (falls through)                ; result = fval
//   SinkMBB:  dst = PHI [tval, ThisMBB], [fval, FalseMBB]
MachineBasicBlock *
M6809TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                 MachineBasicBlock *MBB) const {
  unsigned CBOpc;
  switch (MI.getOpcode()) {
  case M6809::SELECT8_c8:
  case M6809::SELECT16_c8:
    CBOpc = M6809::CB8rr;
    break;
  case M6809::SELECT8_c16:
  case M6809::SELECT16_c16:
    CBOpc = M6809::CB16rr;
    break;
  default:
    llvm_unreachable("M6809: unexpected instruction for custom inserter");
  }

  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  const BasicBlock *BB = MBB->getBasicBlock();
  MachineFunction::iterator It = std::next(MBB->getIterator());

  MachineBasicBlock *FalseMBB = MF->CreateMachineBasicBlock(BB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(BB);
  MF->insert(It, FalseMBB);
  MF->insert(It, SinkMBB);

  // Everything after the select moves to SinkMBB.
  SinkMBB->splice(SinkMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(FalseMBB);
  MBB->addSuccessor(SinkMBB);
  FalseMBB->addSuccessor(SinkMBB);

  BuildMI(MBB, DL, TII.get(CBOpc))
      .addReg(MI.getOperand(3).getReg())
      .addReg(MI.getOperand(4).getReg())
      .addImm(MI.getOperand(5).getImm())
      .addMBB(SinkMBB);

  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(TargetOpcode::PHI),
          MI.getOperand(0).getReg())
      .addReg(MI.getOperand(1).getReg())
      .addMBB(MBB)
      .addReg(MI.getOperand(2).getReg())
      .addMBB(FalseMBB);

  MI.eraseFromParent();
  return SinkMBB;
}

//===----------------------------------------------------------------------===//
// Calling convention
//===----------------------------------------------------------------------===//

SDValue M6809TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (IsVarArg)
    report_fatal_error("M6809: varargs are not supported yet");

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_M6809);

  for (const CCValAssign &VA : ArgLocs) {
    MVT VT = VA.getLocVT();
    if (VA.isRegLoc()) {
      const TargetRegisterClass *RC =
          VT == MVT::i8 ? &M6809::ACC8RegClass : &M6809::GR16RegClass;
      Register VReg = MRI.createVirtualRegister(RC);
      MRI.addLiveIn(VA.getLocReg(), VReg);
      InVals.push_back(DAG.getCopyFromReg(Chain, DL, VReg, VT));
      continue;
    }

    // Stack argument. Offsets are relative to S on entry, which points at
    // the return address.
    int FI = MFI.CreateFixedObject(VT.getStoreSize(),
                                   VA.getLocMemOffset() + ReturnAddressSize,
                                   /*IsImmutable=*/true);
    SDValue FIN = DAG.getFrameIndex(FI, MVT::i16);
    InVals.push_back(DAG.getLoad(VT, DL, Chain, FIN,
                                 MachinePointerInfo::getFixedStack(MF, FI)));
  }
  return Chain;
}

bool M6809TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_M6809);
}

SDValue
M6809TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                 bool IsVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 const SDLoc &DL, SelectionDAG &DAG) const {
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_M6809);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);
  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    const CCValAssign &VA = RVLocs[I];
    assert(VA.isRegLoc() && "M6809 returns values in registers only");
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[I], Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);
  return DAG.getNode(M6809ISD::RET_GLUE, DL, MVT::Other, RetOps);
}
