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

  // TODO: everything not listed here is left Legal and fails instruction
  // selection until it is added. Next up: shifts, compares and branches,
  // multiply, divide, globals and calls.
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
