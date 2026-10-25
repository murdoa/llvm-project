//===-- M6800ISelLowering.cpp - M6800 DAG Lowering Implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800ISelLowering.h"
#include "M6800.h"
#include "M6800MachineFunctionInfo.h"
#include "M6800SelectionDAGInfo.h"
#include "M6800Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "m6800-lower"

// A struct passed by value is copied into the packed argument area. Unlike
// CCPassByVal this ignores the byval alignment: the stack is never realigned,
// so the copy has byte alignment wherever it goes.
static bool CC_M6800_ByVal(unsigned ValNo, MVT ValVT, MVT LocVT,
                           CCValAssign::LocInfo LocInfo,
                           ISD::ArgFlagsTy ArgFlags, CCState &State) {
  unsigned Size = std::max(1u, ArgFlags.getByValSize());
  int64_t Offset = State.AllocateStack(Size, Align(1));
  State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
  return true;
}

#define GET_CALLING_CONV_IMPL
#include "M6800GenCallingConv.inc"

// Return address pushed by JSR/BSR, between the incoming SP + 1 and the
// first stack argument.
static constexpr int ReturnAddressSize = 2;

M6800TargetLowering::M6800TargetLowering(const TargetMachine &TM,
                                         const M6800Subtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  addRegisterClass(MVT::i8, &M6800::ACC8RegClass);
  addRegisterClass(MVT::i16, &M6800::GR16RegClass);
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(M6800::SP);
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

  // One CPU, and an interrupt never splits an instruction: 8- and 16-bit
  // atomic loads and stores are plain ones and a fence only stops the
  // compiler, whatever the alignment.  Read-modify-write operations
  // become __sync_* calls.
  setMaxAtomicSizeInBitsSupported(16);
  setSupportsUnalignedAtomics(true);
  for (MVT VT : {MVT::i8, MVT::i16}) {
    for (unsigned Op :
         {ISD::ATOMIC_SWAP, ISD::ATOMIC_CMP_SWAP, ISD::ATOMIC_LOAD_ADD,
          ISD::ATOMIC_LOAD_SUB, ISD::ATOMIC_LOAD_AND, ISD::ATOMIC_LOAD_OR,
          ISD::ATOMIC_LOAD_XOR, ISD::ATOMIC_LOAD_NAND, ISD::ATOMIC_LOAD_MIN,
          ISD::ATOMIC_LOAD_MAX, ISD::ATOMIC_LOAD_UMIN, ISD::ATOMIC_LOAD_UMAX})
      setOperationAction(Op, VT, Expand);
    setOperationAction(ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS, VT, Expand);
  }

  // SEX covers i8-in-i16; smaller widths become shifts.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  setOperationAction(ISD::GlobalAddress, MVT::i16, Custom);
  setOperationAction(ISD::ExternalSymbol, MVT::i16, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i16, Custom);
  setOperationAction(ISD::JumpTable, MVT::i16, Custom);

  // Every compare ends up as BR_CC or SELECT_CC, which keep the compare and
  // its flag user together.
  for (MVT VT : {MVT::i8, MVT::i16}) {
    setOperationAction(ISD::BR_CC, VT, Custom);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
    setOperationAction(ISD::SETCC, VT, Expand);
    setOperationAction(ISD::SELECT, VT, Expand);
  }
  // Ordered i32 compares become a subtract with carry; see lowerBR_CC32.
  // The generic combine only makes BR_CC for legal types, so
  // PerformDAGCombine makes the i32 ones.
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  setTargetDAGCombine({ISD::BRCOND, ISD::SELECT, ISD::MUL});
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  // Multiply: i8 uses the 8x8 MUL instruction, and i16 the runtime library.
  // Divide and remainder call the runtime library.
  setOperationAction(ISD::MUL, MVT::i16, LibCall);
  for (MVT VT : {MVT::i8, MVT::i16}) {
    for (unsigned Op : {ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM})
      setOperationAction(Op, VT, VT == MVT::i8 ? Promote : LibCall);
    for (unsigned Op : {ISD::MULHS, ISD::MULHU, ISD::SMUL_LOHI, ISD::UMUL_LOHI,
                        ISD::SDIVREM, ISD::UDIVREM})
      setOperationAction(Op, VT, Expand);
  }
  // MUL gives the whole 16-bit product of two bytes.
  setOperationAction({ISD::UMUL_LOHI, ISD::MULHU}, MVT::i8, Custom);

  // Varargs: va_list is a plain pointer into the caller's argument area.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);

  setOperationAction(ISD::STACKSAVE, MVT::Other, Legal);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Legal);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i16, Custom);
  setOperationAction(ISD::RETURNADDR, MVT::i16, Custom);
  setOperationAction(ISD::FRAMEADDR, MVT::i16, Custom);
  setOperationAction(ISD::EH_SJLJ_SETJMP, MVT::i32, Custom);
  setOperationAction(ISD::EH_SJLJ_LONGJMP, MVT::Other, Custom);

  // Multi-word add and subtract use glued carry chains: ADDD/SUBD for the
  // low word, then adcb/adca or sbcb/sbca for each higher one.
  for (unsigned Op : {ISD::ADDC, ISD::ADDE, ISD::SUBC, ISD::SUBE}) {
    setOperationAction(Op, MVT::i16, Legal);
    // There are no i8 patterns. The i16 MULHU expansion would otherwise
    // build an i8 carry chain that fails selection.
    setOperationAction(Op, MVT::i8, Expand);
  }

  // i32 shifts by a small constant are carry chains; see ReplaceNodeResults.
  for (unsigned Op : {ISD::SHL, ISD::SRL, ISD::SRA})
    setOperationAction(Op, MVT::i32, Custom);

  // Shifts are one bit at a time; see lowerShift. Everything built from
  // shifts falls back to the generic expansion.
  for (MVT VT : {MVT::i8, MVT::i16}) {
    for (unsigned Op : {ISD::SHL, ISD::SRL, ISD::SRA})
      setOperationAction(Op, VT, Custom);
    for (unsigned Op :
         {ISD::ROTL, ISD::ROTR, ISD::FSHL, ISD::FSHR, ISD::SHL_PARTS,
          ISD::SRL_PARTS, ISD::SRA_PARTS, ISD::BSWAP, ISD::CTPOP, ISD::CTLZ,
          ISD::CTTZ, ISD::ABS, ISD::SMIN, ISD::SMAX, ISD::UMIN, ISD::UMAX})
      setOperationAction(Op, VT, Expand);
  }

  // TODO: everything not listed here is left Legal and fails instruction
  // selection until it is added.
}

MVT M6800TargetLowering::getScalarShiftAmountTy(const DataLayout &, EVT) const {
  return MVT::i8;
}

//===----------------------------------------------------------------------===//
// Custom lowering
//===----------------------------------------------------------------------===//

SDValue M6800TargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::UMUL_LOHI:
  case ISD::MULHU: {
    SDLoc DL(Op);
    SDValue P = DAG.getNode(M6800ISD::UMUL8, DL, MVT::i16, Op.getOperand(0),
                            Op.getOperand(1));
    SDValue Hi =
        DAG.getNode(ISD::TRUNCATE, DL, MVT::i8,
                    DAG.getNode(ISD::SRL, DL, MVT::i16, P,
                                DAG.getShiftAmountConstant(8, MVT::i16, DL)));
    if (Op.getOpcode() == ISD::MULHU)
      return Hi;
    SDValue Lo = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, P);
    return DAG.getMergeValues({Lo, Hi}, DL);
  }
  case ISD::GlobalAddress:
    return lowerGlobalAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return lowerExternalSymbol(Op, DAG);
  case ISD::BlockAddress:
    return lowerBlockAddress(Op, DAG);
  case ISD::JumpTable:
    return DAG.getNode(M6800ISD::WRAPPER, SDLoc(Op), MVT::i16,
                       DAG.getTargetJumpTable(
                           cast<JumpTableSDNode>(Op)->getIndex(), MVT::i16));
  case ISD::BR_CC:
    return lowerBR_CC(Op, DAG);
  case ISD::SELECT_CC:
    return lowerSELECT_CC(Op, DAG);
  case ISD::VASTART:
    return lowerVASTART(Op, DAG);
  case ISD::RETURNADDR:
    return lowerRETURNADDR(Op, DAG);
  case ISD::FRAMEADDR:
    return lowerFRAMEADDR(Op, DAG);
  case ISD::EH_SJLJ_LONGJMP:
    return DAG.getNode(M6800ISD::EH_SJLJ_LONGJMP, SDLoc(Op), MVT::Other,
                       Op.getOperand(0), Op.getOperand(1));
  case ISD::DYNAMIC_STACKALLOC: {
    SDLoc DL(Op);
    uint64_t Align = std::max<uint64_t>(Op.getConstantOperandVal(2), 1);
    SDValue Res =
        DAG.getNode(M6800ISD::DYNALLOC, DL, DAG.getVTList(MVT::i16, MVT::Other),
                    Op.getOperand(0), Op.getOperand(1),
                    DAG.getTargetConstant(Align, DL, MVT::i16));
    return DAG.getMergeValues({Res, Res.getValue(1)}, DL);
  }
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
    return lowerShift(Op, DAG);
  default:
    llvm_unreachable("M6800: unexpected operation to lower");
  }
}

// The return address JSR pushed, just above the incoming arguments.
SDValue
M6800TargetLowering::getReturnAddressFrameIndex(SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  M6800FunctionInfo *FuncInfo = MF.getInfo<M6800FunctionInfo>();
  int FI = FuncInfo->getReturnAddressFrameIndex();
  if (!FI) {
    FI = MF.getFrameInfo().CreateFixedObject(ReturnAddressSize, 0,
                                             /*IsImmutable=*/true);
    FuncInfo->setReturnAddressFrameIndex(FI);
  }
  return DAG.getFrameIndex(FI, MVT::i16);
}

// There is no frame chain to walk, so only the current function's return
// address is known. Outer frames give 0.
SDValue M6800TargetLowering::lowerRETURNADDR(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc DL(Op);
  if (Op.getConstantOperandVal(0) != 0)
    return DAG.getConstant(0, DL, MVT::i16);
  return DAG.getLoad(MVT::i16, DL, DAG.getEntryNode(),
                     getReturnAddressFrameIndex(DAG), MachinePointerInfo());
}

// The frame address is the caller's SP + 1 just before the call: the
// address of the return address. It is above every local of the function.
SDValue M6800TargetLowering::lowerFRAMEADDR(SDValue Op,
                                            SelectionDAG &DAG) const {
  DAG.getMachineFunction().getFrameInfo().setFrameAddressIsTaken(true);
  if (Op.getConstantOperandVal(0) != 0)
    return DAG.getConstant(0, SDLoc(Op), MVT::i16);
  return getReturnAddressFrameIndex(DAG);
}

SDValue M6800TargetLowering::lowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  auto *N = cast<GlobalAddressSDNode>(Op);
  SDLoc DL(Op);
  SDValue GA =
      DAG.getTargetGlobalAddress(N->getGlobal(), DL, MVT::i16, N->getOffset());
  return DAG.getNode(M6800ISD::WRAPPER, DL, MVT::i16, GA);
}

SDValue M6800TargetLowering::lowerExternalSymbol(SDValue Op,
                                                 SelectionDAG &DAG) const {
  auto *N = cast<ExternalSymbolSDNode>(Op);
  SDLoc DL(Op);
  SDValue Sym = DAG.getTargetExternalSymbol(N->getSymbol(), MVT::i16);
  return DAG.getNode(M6800ISD::WRAPPER, DL, MVT::i16, Sym);
}

SDValue M6800TargetLowering::lowerBlockAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  auto *N = cast<BlockAddressSDNode>(Op);
  SDLoc DL(Op);
  SDValue BA =
      DAG.getTargetBlockAddress(N->getBlockAddress(), MVT::i16, N->getOffset());
  return DAG.getNode(M6800ISD::WRAPPER, DL, MVT::i16, BA);
}

// Shifts. A constant amount becomes a chain of one-bit shifts. On i16,
// amounts of 8 or more move a byte first (see the patterns in
// M6800InstrInfo.td) and finish with 8-bit shifts. A variable amount becomes
// a loop.
SDValue M6800TargetLowering::lowerShift(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  unsigned Opc = Op.getOpcode();
  SDValue Val = Op.getOperand(0);
  SDValue Amt = Op.getOperand(1);

  auto *C = dyn_cast<ConstantSDNode>(Amt);
  if (!C) {
    unsigned LoopOpc = Opc == ISD::SHL   ? M6800ISD::LSL_LOOP
                       : Opc == ISD::SRL ? M6800ISD::LSR_LOOP
                                         : M6800ISD::ASR_LOOP;
    SDValue Cnt = DAG.getZExtOrTrunc(Amt, DL, MVT::i16);
    return DAG.getNode(LoopOpc, DL, VT, Val, Cnt);
  }

  uint64_t N = C->getZExtValue();
  if (N >= VT.getSizeInBits())
    return DAG.getUNDEF(VT);
  if (VT == MVT::i16 && N == 8)
    return Op; // Matched by a pattern.

  unsigned OneOpc = Opc == ISD::SHL   ? M6800ISD::LSL
                    : Opc == ISD::SRL ? M6800ISD::LSR
                                      : M6800ISD::ASR;
  auto Repeat = [&](SDValue V, uint64_t Times) {
    for (uint64_t I = 0; I != Times; ++I)
      V = DAG.getNode(OneOpc, DL, V.getValueType(), V);
    return V;
  };

  if (VT == MVT::i16 && N > 8) {
    SDValue Eight = DAG.getConstant(8, DL, MVT::i8);
    if (Opc == ISD::SHL) {
      SDValue Lo = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, Val);
      Lo = Repeat(Lo, N - 8);
      return DAG.getNode(ISD::SHL, DL, VT,
                         DAG.getNode(ISD::ANY_EXTEND, DL, VT, Lo), Eight);
    }
    SDValue Hi = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8,
                             DAG.getNode(Opc, DL, VT, Val, Eight));
    Hi = Repeat(Hi, N - 8);
    return DAG.getNode(Opc == ISD::SRA ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND,
                       DL, VT, Hi);
  }
  return Repeat(Val, N);
}

// Longest i32 constant shift done as a carry chain. Each bit costs about a
// dozen bytes, because both halves pass through D in turn.
static constexpr uint64_t MaxShiftChain32 = 3;

void M6800TargetLowering::ReplaceNodeResults(SDNode *N,
                                             SmallVectorImpl<SDValue> &Results,
                                             SelectionDAG &DAG) const {
  unsigned Opc = N->getOpcode();
  if (Opc == ISD::EH_SJLJ_SETJMP) {
    SDLoc DL(N);
    SDValue R = DAG.getNode(M6800ISD::EH_SJLJ_SETJMP, DL,
                            DAG.getVTList(MVT::i16, MVT::Other),
                            N->getOperand(0), N->getOperand(1));
    Results.push_back(DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, R));
    Results.push_back(R.getValue(1));
    return;
  }
  if ((Opc != ISD::SHL && Opc != ISD::SRL && Opc != ISD::SRA) ||
      N->getValueType(0) != MVT::i32)
    return;
  auto *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!C || C->isZero() || C->getZExtValue() > MaxShiftChain32)
    return; // Leave it to the generic expansion.

  SDLoc DL(N);
  auto [Lo, Hi] = DAG.SplitScalar(N->getOperand(0), DL, MVT::i16, MVT::i16);
  SDVTList VTs = DAG.getVTList(MVT::i16, MVT::Glue);
  for (uint64_t I = 0, E = C->getZExtValue(); I != E; ++I) {
    if (Opc == ISD::SHL) {
      Lo = DAG.getNode(M6800ISD::LSL_C, DL, VTs, Lo);
      Hi = DAG.getNode(M6800ISD::ROL_C, DL, VTs, Hi, Lo.getValue(1));
    } else {
      Hi = DAG.getNode(Opc == ISD::SRL ? M6800ISD::LSR_C : M6800ISD::ASR_C, DL,
                       VTs, Hi);
      Lo = DAG.getNode(M6800ISD::ROR_C, DL, VTs, Lo, Hi.getValue(1));
    }
  }
  Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, Lo, Hi));
}

// Maps an integer condition to a 6800 branch condition. A constant LHS is
// swapped to the right first, since only the right side can be an immediate.
static M6800CC::CondCode prepareCompare(SDValue &LHS, SDValue &RHS,
                                        ISD::CondCode CC, SelectionDAG &DAG,
                                        const SDLoc &DL) {
  if (isa<ConstantSDNode>(LHS) && !isa<ConstantSDNode>(RHS)) {
    std::swap(LHS, RHS);
    CC = ISD::getSetCCSwappedOperands(CC);
  }
  // x <= -1 and x > -1 are sign tests: against zero, they are a TST.
  if (isAllOnesConstant(RHS) && (CC == ISD::SETLE || CC == ISD::SETGT)) {
    RHS = DAG.getConstant(0, DL, RHS.getValueType());
    CC = CC == ISD::SETLE ? ISD::SETLT : ISD::SETGE;
  }
  switch (CC) {
  case ISD::SETEQ:
    return M6800CC::EQ;
  case ISD::SETNE:
    return M6800CC::NE;
  case ISD::SETUGT:
    return M6800CC::HI;
  case ISD::SETUGE:
    return M6800CC::HS;
  case ISD::SETULT:
    return M6800CC::LO;
  case ISD::SETULE:
    return M6800CC::LS;
  case ISD::SETGT:
    return M6800CC::GT;
  case ISD::SETGE:
    return M6800CC::GE;
  case ISD::SETLT:
    return M6800CC::LT;
  case ISD::SETLE:
    return M6800CC::LE;
  default:
    llvm_unreachable("M6800: unsupported integer condition");
  }
}

SDValue M6800TargetLowering::lowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  if (Op.getOperand(2).getValueType() == MVT::i32)
    return lowerBR_CC32(Op, DAG);
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  M6800CC::CondCode MCC = prepareCompare(LHS, RHS, CC, DAG, DL);
  return DAG.getNode(M6800ISD::CMPBR, DL, MVT::Other, Chain, LHS, RHS,
                     DAG.getTargetConstant(MCC, DL, MVT::i8), Dest);
}

static bool isOrderedCompare32(SDValue Cond);
static SDValue combineSelect32(SDNode *N, SelectionDAG &DAG);

// mul a, b -> UMUL8 (trunc a), (trunc b), for i16 a and b that fit in a
// byte, or two UMUL8s when only one of them does.
static SDValue combineMul8(SDNode *N, SelectionDAG &DAG) {
  if (N->getValueType(0) != MVT::i16)
    return SDValue();
  SDValue A = N->getOperand(0), B = N->getOperand(1);
  APInt High = APInt::getHighBitsSet(16, 8);
  bool ByteA = DAG.MaskedValueIsZero(A, High);
  bool ByteB = DAG.MaskedValueIsZero(B, High);
  if (!ByteA && !ByteB)
    return SDValue();
  if (!ByteB)
    std::swap(A, B);
  SDLoc DL(N);
  SDValue B8 = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, B);
  SDValue Lo = DAG.getNode(M6800ISD::UMUL8, DL, MVT::i16,
                           DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, A), B8);
  if (ByteA && ByteB)
    return Lo;
  // a * b = lo(a) * b + (hi(a) * b << 8): two MULs instead of a call to
  // __mulhi3, unless optimizing for size.
  if (DAG.getMachineFunction().getFunction().hasMinSize())
    return SDValue();
  SDValue HiA = DAG.getNode(ISD::SRL, DL, MVT::i16, A,
                            DAG.getShiftAmountConstant(8, MVT::i16, DL));
  SDValue Hi = DAG.getNode(M6800ISD::UMUL8, DL, MVT::i16,
                           DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, HiA), B8);
  Hi = DAG.getNode(ISD::SHL, DL, MVT::i16, Hi,
                   DAG.getShiftAmountConstant(8, MVT::i16, DL));
  return DAG.getNode(ISD::ADD, DL, MVT::i16, Lo, Hi);
}

SDValue M6800TargetLowering::PerformDAGCombine(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  if (!DCI.isBeforeLegalize())
    return SDValue();
  if (N->getOpcode() == ISD::MUL)
    return combineMul8(N, DCI.DAG);
  if (N->getOpcode() == ISD::SELECT)
    return combineSelect32(N, DCI.DAG);
  // brcond (setcc a, b, cc), dest -> br_cc cc, a, b, dest, for i32 a and b.
  if (N->getOpcode() != ISD::BRCOND)
    return SDValue();
  SDValue Cond = N->getOperand(1);
  if (!isOrderedCompare32(Cond) || !Cond.hasOneUse())
    return SDValue();
  return DCI.DAG.getNode(ISD::BR_CC, SDLoc(N), MVT::Other, N->getOperand(0),
                         Cond.getOperand(2), Cond.getOperand(0),
                         Cond.getOperand(1), N->getOperand(2));
}

// An ordered i32 compare is a 32-bit subtraction: subd on the low halves,
// then sbcb/sbca on the high halves leave C, N and V as for the whole value.
// Z is only the high half's, so GT, LE, HI and LS become the other four,
// with the operands swapped or a constant moved by one. Equality compares
// are left to the generic expansion.
static M6800CC::CondCode prepareCompare32(SDValue &LHS, SDValue &RHS,
                                          ISD::CondCode CC, SelectionDAG &DAG,
                                          const SDLoc &DL) {
  if (isa<ConstantSDNode>(LHS) && !isa<ConstantSDNode>(RHS)) {
    std::swap(LHS, RHS);
    CC = ISD::getSetCCSwappedOperands(CC);
  }
  if (auto *C = dyn_cast<ConstantSDNode>(RHS)) {
    const APInt &V = C->getAPIntValue();
    ISD::CondCode NewCC = ISD::SETCC_INVALID;
    switch (CC) {
    case ISD::SETGT:
    case ISD::SETLE:
      if (!V.isMaxSignedValue())
        NewCC = CC == ISD::SETGT ? ISD::SETGE : ISD::SETLT;
      break;
    case ISD::SETUGT:
    case ISD::SETULE:
      if (!V.isMaxValue())
        NewCC = CC == ISD::SETUGT ? ISD::SETUGE : ISD::SETULT;
      break;
    default:
      break;
    }
    if (NewCC != ISD::SETCC_INVALID) {
      CC = NewCC;
      RHS = DAG.getConstant(V + 1, DL, MVT::i32);
    }
  }
  switch (CC) {
  case ISD::SETGT:
  case ISD::SETLE:
  case ISD::SETUGT:
  case ISD::SETULE:
    std::swap(LHS, RHS);
    CC = ISD::getSetCCSwappedOperands(CC);
    break;
  default:
    break;
  }
  switch (CC) {
  case ISD::SETLT:
    return M6800CC::LT;
  case ISD::SETGE:
    return M6800CC::GE;
  case ISD::SETULT:
    return M6800CC::LO;
  case ISD::SETUGE:
    return M6800CC::HS;
  default:
    llvm_unreachable("M6800: unexpected i32 condition");
  }
}

static bool isOrderedCompare32(SDValue Cond) {
  if (Cond.getOpcode() != ISD::SETCC ||
      Cond.getOperand(0).getValueType() != MVT::i32)
    return false;
  ISD::CondCode CC = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
  return CC != ISD::SETEQ && CC != ISD::SETNE;
}

SDValue M6800TargetLowering::lowerBR_CC32(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);
  if (CC == ISD::SETEQ || CC == ISD::SETNE)
    return SDValue();

  M6800CC::CondCode MCC = prepareCompare32(LHS, RHS, CC, DAG, DL);
  auto [LHSLo, LHSHi] = DAG.SplitScalar(LHS, DL, MVT::i16, MVT::i16);
  auto [RHSLo, RHSHi] = DAG.SplitScalar(RHS, DL, MVT::i16, MVT::i16);
  // Subtracting zero from the low half never borrows: compare the high
  // halves. This covers the sign tests (x < 0) too.
  if (isNullConstant(RHSLo))
    return DAG.getNode(M6800ISD::CMPBR, DL, MVT::Other, Chain, LHSHi, RHSHi,
                       DAG.getTargetConstant(MCC, DL, MVT::i8), Dest);
  SDValue Lo = DAG.getNode(M6800ISD::SUBC, DL,
                           DAG.getVTList(MVT::i16, MVT::Glue), LHSLo, RHSLo);
  return DAG.getNode(M6800ISD::SBCBR, DL, MVT::Other,
                     {Chain, LHSHi, RHSHi,
                      DAG.getTargetConstant(MCC, DL, MVT::i8), Dest,
                      Lo.getValue(1)});
}

// select (setcc a, b, cc), t, f with i32 a and b, and i16 or i32 t and f,
// becomes SELECT32, which the custom inserter makes a branch diamond.
static SDValue combineSelect32(SDNode *N, SelectionDAG &DAG) {
  SDValue Cond = N->getOperand(0);
  EVT VT = N->getValueType(0);
  if (!isOrderedCompare32(Cond) || (VT != MVT::i16 && VT != MVT::i32))
    return SDValue();
  SDLoc DL(N);
  SDValue LHS = Cond.getOperand(0), RHS = Cond.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
  M6800CC::CondCode MCC = prepareCompare32(LHS, RHS, CC, DAG, DL);
  auto [LHSLo, LHSHi] = DAG.SplitScalar(LHS, DL, MVT::i16, MVT::i16);
  auto [RHSLo, RHSHi] = DAG.SplitScalar(RHS, DL, MVT::i16, MVT::i16);
  SDValue T = N->getOperand(1), F = N->getOperand(2);
  SDValue TLo = T, THi = T, FLo = F, FHi = F;
  if (VT == MVT::i32) {
    std::tie(TLo, THi) = DAG.SplitScalar(T, DL, MVT::i16, MVT::i16);
    std::tie(FLo, FHi) = DAG.SplitScalar(F, DL, MVT::i16, MVT::i16);
  }
  SDValue R =
      DAG.getNode(M6800ISD::SELECT32, DL, DAG.getVTList(MVT::i16, MVT::i16),
                  {TLo, THi, FLo, FHi, LHSLo, LHSHi, RHSLo, RHSHi,
                   DAG.getTargetConstant(MCC, DL, MVT::i8)});
  if (VT == MVT::i16)
    return R;
  return DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, R, R.getValue(1));
}

// va_start stores the address of the first variadic argument into the
// va_list.
SDValue M6800TargetLowering::lowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  auto *FuncInfo = MF.getInfo<M6800FunctionInfo>();
  SDLoc DL(Op);
  SDValue FI = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), MVT::i16);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, FI, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue M6800TargetLowering::lowerSELECT_CC(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TVal = Op.getOperand(2);
  SDValue FVal = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);

  M6800CC::CondCode MCC = prepareCompare(LHS, RHS, CC, DAG, DL);
  return DAG.getNode(M6800ISD::SELECT_CC, DL, Op.getValueType(), TVal, FVal,
                     LHS, RHS, DAG.getTargetConstant(MCC, DL, MVT::i8));
}

// SELECT* pseudos become:
//
//   ThisMBB:  CB*rr lhs, rhs, cc, SinkMBB    ; taken: result = tval
//   FalseMBB: (falls through)                ; result = fval
//   SinkMBB:  dst = PHI [tval, ThisMBB], [fval, FalseMBB]
MachineBasicBlock *
M6800TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                 MachineBasicBlock *MBB) const {
  switch (MI.getOpcode()) {
  case M6800::MUL8:
  case M6800::UMUL8:
    return emitMul8(MI, MBB);
  case M6800::LSLV_8:
  case M6800::LSLV_16:
  case M6800::LSRV_8:
  case M6800::LSRV_16:
  case M6800::ASRV_8:
  case M6800::ASRV_16:
    return emitShiftLoop(MI, MBB);
  case M6800::SELECT32rr:
  case M6800::SELECT32ri:
    return emitSelect32(MI, MBB);
  case M6800::SJLJ_SETJMP:
    return emitSjLjSetjmp(MI, MBB);
  case M6800::SJLJ_LONGJMP:
    return emitSjLjLongjmp(MI, MBB);
  default:
    return emitSelect(MI, MBB);
  }
}

// SJLJ_SETJMP stores the resume address and __m6800_fp, then gives 0 on the
// way in and 1 when SJLJ_LONGJMP jumps to RestoreMBB:
//
//   ThisMBB:    ldd #RestoreMBB; std 2,buf; ldd __m6800_fp; std 6,buf
//               SJLJ_SETUP RestoreMBB   ; clobbers every register
//   MainMBB:    v0 = 0
//   SinkMBB:    dst = PHI [v0, MainMBB], [v1, RestoreMBB]
//   ...
//   RestoreMBB: v1 = 1; bra SinkMBB
MachineBasicBlock *
M6800TargetLowering::emitSjLjSetjmp(MachineInstr &MI,
                                    MachineBasicBlock *MBB) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const BasicBlock *BB = MBB->getBasicBlock();
  MachineFunction::iterator It = std::next(MBB->getIterator());

  MachineBasicBlock *MainMBB = MF->CreateMachineBasicBlock(BB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(BB);
  MachineBasicBlock *RestoreMBB = MF->CreateMachineBasicBlock(BB);
  MF->insert(It, MainMBB);
  MF->insert(It, SinkMBB);
  MF->push_back(RestoreMBB);
  RestoreMBB->setMachineBlockAddressTaken();

  SinkMBB->splice(SinkMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(MainMBB);
  MBB->addSuccessor(RestoreMBB);
  MainMBB->addSuccessor(SinkMBB);
  RestoreMBB->addSuccessor(SinkMBB);

  Register Dst = MI.getOperand(0).getReg();
  Register Buf = MI.getOperand(1).getReg();
  Register Addr = MRI.createVirtualRegister(&M6800::ACC16RegClass);
  Register FP = MRI.createVirtualRegister(&M6800::ACC16RegClass);
  Register V0 = MRI.createVirtualRegister(&M6800::GR16RegClass);
  Register V1 = MRI.createVirtualRegister(&M6800::GR16RegClass);

  BuildMI(MBB, DL, TII.get(M6800::LD16i), Addr).addMBB(RestoreMBB);
  BuildMI(MBB, DL, TII.get(M6800::ST16m))
      .addReg(Addr)
      .addReg(Buf)
      .addImm(2)
      .addImm(M6800Mem::Auto);
  BuildMI(MBB, DL, TII.get(M6800::LD16m), FP)
      .addReg(0)
      .addExternalSymbol("__m6800_fp")
      .addImm(M6800Mem::Extended);
  BuildMI(MBB, DL, TII.get(M6800::ST16m))
      .addReg(FP)
      .addReg(Buf)
      .addImm(6)
      .addImm(M6800Mem::Auto);
  BuildMI(MBB, DL, TII.get(M6800::SJLJ_SETUP))
      .addMBB(RestoreMBB)
      .addRegMask(Subtarget.getRegisterInfo()->getNoPreservedMask());

  BuildMI(MainMBB, DL, TII.get(M6800::LD16i), V0).addImm(0);
  BuildMI(RestoreMBB, DL, TII.get(M6800::LD16i), V1).addImm(1);
  BuildMI(RestoreMBB, DL, TII.get(M6800::BRA)).addMBB(SinkMBB);
  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(TargetOpcode::PHI), Dst)
      .addReg(V0)
      .addMBB(MainMBB)
      .addReg(V1)
      .addMBB(RestoreMBB);

  MI.eraseFromParent();
  return SinkMBB;
}

// SJLJ_LONGJMP restores __m6800_fp and SP from the buffer and jumps to the
// resume address: "ldd 6,x; std __m6800_fp; lds 4,x; des; ldx 2,x; jmp 0,x".
// The buffer holds SP + 1, hence the DES.
MachineBasicBlock *
M6800TargetLowering::emitSjLjLongjmp(MachineInstr &MI,
                                     MachineBasicBlock *MBB) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  auto Mem = [](MachineInstrBuilder MIB, int64_t Off) {
    MIB.addReg(M6800::X).addImm(Off).addImm(M6800Mem::Auto);
  };
  BuildMI(*MBB, MI, DL, TII.get(TargetOpcode::COPY), M6800::X)
      .addReg(MI.getOperand(0).getReg());
  Mem(BuildMI(*MBB, MI, DL, TII.get(M6800::LD16m), M6800::D), 6);
  BuildMI(*MBB, MI, DL, TII.get(M6800::ST16m))
      .addReg(M6800::D, RegState::Kill)
      .addReg(0)
      .addExternalSymbol("__m6800_fp")
      .addImm(M6800Mem::Extended);
  Mem(BuildMI(*MBB, MI, DL, TII.get(M6800::LDSm)), 4);
  BuildMI(*MBB, MI, DL, TII.get(M6800::DES));
  Mem(BuildMI(*MBB, MI, DL, TII.get(M6800::LD16m), M6800::X), 2);
  Mem(BuildMI(*MBB, MI, DL, TII.get(M6800::JMPm)), 0);
  MI.eraseFromParent();
  return MBB;
}

// Variable shifts become a count-down loop:
//
//   ThisMBB: CB16ri cnt, 0, EQ, DoneMBB
//   LoopMBB: v  = PHI [src, ThisMBB], [v2, LoopMBB]
//            c  = PHI [cnt, ThisMBB], [c2, LoopMBB]
//            v2 = one-bit shift of v
//            c2 = c - 1                         ; dex
//            CB16ri c2, 0, NE, LoopMBB
//   DoneMBB: dst = PHI [src, ThisMBB], [v2, LoopMBB]
MachineBasicBlock *
M6800TargetLowering::emitShiftLoop(MachineInstr &MI,
                                   MachineBasicBlock *MBB) const {
  unsigned ShiftOpc;
  const TargetRegisterClass *RC;
  switch (MI.getOpcode()) {
  case M6800::LSLV_8:
    ShiftOpc = M6800::ASL8;
    RC = &M6800::ACC8RegClass;
    break;
  case M6800::LSRV_8:
    ShiftOpc = M6800::LSR8;
    RC = &M6800::ACC8RegClass;
    break;
  case M6800::ASRV_8:
    ShiftOpc = M6800::ASR8;
    RC = &M6800::ACC8RegClass;
    break;
  case M6800::LSLV_16:
    ShiftOpc = M6800::LSL16;
    RC = &M6800::ACC16RegClass;
    break;
  case M6800::LSRV_16:
    ShiftOpc = M6800::LSR16;
    RC = &M6800::ACC16RegClass;
    break;
  case M6800::ASRV_16:
    ShiftOpc = M6800::ASR16;
    RC = &M6800::ACC16RegClass;
    break;
  default:
    llvm_unreachable("M6800: not a shift loop");
  }

  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const BasicBlock *BB = MBB->getBasicBlock();
  MachineFunction::iterator It = std::next(MBB->getIterator());

  MachineBasicBlock *LoopMBB = MF->CreateMachineBasicBlock(BB);
  MachineBasicBlock *DoneMBB = MF->CreateMachineBasicBlock(BB);
  MF->insert(It, LoopMBB);
  MF->insert(It, DoneMBB);
  // The pseudo may sit inside a call sequence.
  unsigned CallFrameSize = TII.getCallFrameSizeAt(MI);
  LoopMBB->setCallFrameSize(CallFrameSize);
  DoneMBB->setCallFrameSize(CallFrameSize);

  DoneMBB->splice(DoneMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  DoneMBB->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(LoopMBB);
  MBB->addSuccessor(DoneMBB);
  LoopMBB->addSuccessor(LoopMBB);
  LoopMBB->addSuccessor(DoneMBB);

  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  Register Cnt = MI.getOperand(2).getReg();
  Register V = MRI.createVirtualRegister(RC);
  Register V2 = MRI.createVirtualRegister(RC);
  Register C = MRI.createVirtualRegister(&M6800::IDX16RegClass);
  Register C2 = MRI.createVirtualRegister(&M6800::IDX16RegClass);

  BuildMI(MBB, DL, TII.get(M6800::CB16ri))
      .addReg(Cnt)
      .addImm(0)
      .addImm(M6800CC::EQ)
      .addMBB(DoneMBB);

  BuildMI(LoopMBB, DL, TII.get(TargetOpcode::PHI), V)
      .addReg(Src)
      .addMBB(MBB)
      .addReg(V2)
      .addMBB(LoopMBB);
  BuildMI(LoopMBB, DL, TII.get(TargetOpcode::PHI), C)
      .addReg(Cnt)
      .addMBB(MBB)
      .addReg(C2)
      .addMBB(LoopMBB);
  BuildMI(LoopMBB, DL, TII.get(ShiftOpc), V2).addReg(V);
  BuildMI(LoopMBB, DL, TII.get(M6800::ADD16ri), C2).addReg(C).addImm(-1);
  BuildMI(LoopMBB, DL, TII.get(M6800::CB16ri))
      .addReg(C2)
      .addImm(0)
      .addImm(M6800CC::NE)
      .addMBB(LoopMBB);

  BuildMI(*DoneMBB, DoneMBB->begin(), DL, TII.get(TargetOpcode::PHI), Dst)
      .addReg(Src)
      .addMBB(MBB)
      .addReg(V2)
      .addMBB(LoopMBB);

  MI.eraseFromParent();
  return DoneMBB;
}

// MUL8 and UMUL8: copy the operands into A and B, MUL, and take the low
// byte (B) or the whole product (D).
MachineBasicBlock *M6800TargetLowering::emitMul8(MachineInstr &MI,
                                                 MachineBasicBlock *MBB) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  BuildMI(*MBB, MI, DL, TII.get(TargetOpcode::COPY), M6800::A)
      .add(MI.getOperand(1));
  BuildMI(*MBB, MI, DL, TII.get(TargetOpcode::COPY), M6800::B)
      .add(MI.getOperand(2));
  BuildMI(*MBB, MI, DL, TII.get(M6800::MUL));
  BuildMI(*MBB, MI, DL, TII.get(TargetOpcode::COPY), MI.getOperand(0).getReg())
      .addReg(MI.getOpcode() == M6800::UMUL8 ? M6800::D : M6800::B);
  MI.eraseFromParent();
  return MBB;
}

MachineBasicBlock *
M6800TargetLowering::emitSelect(MachineInstr &MI,
                                MachineBasicBlock *MBB) const {
  unsigned CBOpc;
  switch (MI.getOpcode()) {
  case M6800::SELECT8_c8:
  case M6800::SELECT16_c8:
    CBOpc = M6800::CB8rr;
    break;
  case M6800::SELECT8_c16:
  case M6800::SELECT16_c16:
    CBOpc = M6800::CB16rr;
    break;
  case M6800::SELECT8_c8i:
  case M6800::SELECT16_c8i:
    CBOpc = M6800::CB8ri;
    break;
  case M6800::SELECT8_c16i:
  case M6800::SELECT16_c16i:
    CBOpc = M6800::CB16ri;
    break;
  default:
    llvm_unreachable("M6800: unexpected instruction for custom inserter");
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
  // The pseudo may sit inside a call sequence.
  unsigned CallFrameSize = TII.getCallFrameSizeAt(MI);
  FalseMBB->setCallFrameSize(CallFrameSize);
  SinkMBB->setCallFrameSize(CallFrameSize);

  // Everything after the select moves to SinkMBB.
  SinkMBB->splice(SinkMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(FalseMBB);
  MBB->addSuccessor(SinkMBB);
  FalseMBB->addSuccessor(SinkMBB);

  BuildMI(MBB, DL, TII.get(CBOpc))
      .addReg(MI.getOperand(3).getReg())
      .add(MI.getOperand(4)) // Register or immediate.
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

// SELECT32 compares two i32 values (as halves) and picks between two pairs
// of halves:
//
//   ThisMBB:  subd lhs.lo, rhs.lo              ; only for C
//             SBCBR* lhs.hi, rhs.hi, cc, SinkMBB
//   FalseMBB: (falls through)
//   SinkMBB:  dst.lo = PHI [t.lo, ThisMBB], [f.lo, FalseMBB]
//             dst.hi = PHI [t.hi, ThisMBB], [f.hi, FalseMBB]
//
// With a zero rhs.lo the low halves cannot borrow, and a CB16 on the high
// halves does.
MachineBasicBlock *
M6800TargetLowering::emitSelect32(MachineInstr &MI,
                                  MachineBasicBlock *MBB) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const BasicBlock *BB = MBB->getBasicBlock();
  MachineFunction::iterator It = std::next(MBB->getIterator());
  bool Imm = MI.getOpcode() == M6800::SELECT32ri;

  MachineBasicBlock *FalseMBB = MF->CreateMachineBasicBlock(BB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(BB);
  MF->insert(It, FalseMBB);
  MF->insert(It, SinkMBB);
  // The pseudo may sit inside a call sequence.
  unsigned CallFrameSize = TII.getCallFrameSizeAt(MI);
  FalseMBB->setCallFrameSize(CallFrameSize);
  SinkMBB->setCallFrameSize(CallFrameSize);
  SinkMBB->splice(SinkMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(FalseMBB);
  MBB->addSuccessor(SinkMBB);
  FalseMBB->addSuccessor(SinkMBB);

  Register LLo = MI.getOperand(6).getReg(), LHi = MI.getOperand(7).getReg();
  const MachineOperand &RLo = MI.getOperand(8), &RHi = MI.getOperand(9);
  int64_t CC = MI.getOperand(10).getImm();
  auto CopyTo = [&](const TargetRegisterClass *RC, Register R) {
    Register V = MRI.createVirtualRegister(RC);
    BuildMI(MBB, DL, TII.get(TargetOpcode::COPY), V).addReg(R);
    return V;
  };

  if (Imm && RLo.getImm() == 0) {
    BuildMI(MBB, DL, TII.get(M6800::CB16ri))
        .addReg(LHi)
        .add(RHi)
        .addImm(CC)
        .addMBB(SinkMBB);
  } else {
    Register A = CopyTo(&M6800::ACC16RegClass, LLo);
    Register Dead = MRI.createVirtualRegister(&M6800::ACC16RegClass);
    if (Imm) {
      BuildMI(MBB, DL, TII.get(M6800::SUBDi), Dead).addReg(A).add(RLo);
    } else {
      Register RL = CopyTo(&M6800::IDX16RegClass, RLo.getReg());
      BuildMI(MBB, DL, TII.get(M6800::SUBDrr), Dead).addReg(A).addReg(RL);
    }
    Register H = CopyTo(&M6800::ACC16RegClass, LHi);
    Register RH =
        Imm ? Register() : CopyTo(&M6800::IDX16RegClass, RHi.getReg());
    MachineInstrBuilder Br =
        BuildMI(MBB, DL, TII.get(Imm ? M6800::SBCBRi : M6800::SBCBRrr))
            .addReg(H);
    if (Imm)
      Br.add(RHi);
    else
      Br.addReg(RH);
    Br.addImm(CC).addMBB(SinkMBB);
  }

  for (unsigned I = 0; I != 2; ++I)
    BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(TargetOpcode::PHI),
            MI.getOperand(I).getReg())
        .addReg(MI.getOperand(2 + I).getReg())
        .addMBB(MBB)
        .addReg(MI.getOperand(4 + I).getReg())
        .addMBB(FalseMBB);

  MI.eraseFromParent();
  return SinkMBB;
}

//===----------------------------------------------------------------------===//
// Calling convention
//===----------------------------------------------------------------------===//

SDValue M6800TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_M6800);

  for (const CCValAssign &VA : ArgLocs) {
    MVT VT = VA.getLocVT();
    if (VA.isRegLoc()) {
      const TargetRegisterClass *RC =
          VT == MVT::i8 ? &M6800::ACC8RegClass : &M6800::GR16RegClass;
      Register VReg = MRI.createVirtualRegister(RC);
      MRI.addLiveIn(VA.getLocReg(), VReg);
      InVals.push_back(DAG.getCopyFromReg(Chain, DL, VReg, VT));
      continue;
    }

    // Stack argument. Offsets are relative to S on entry, which points at
    // the return address.
    ISD::ArgFlagsTy Flags = Ins[VA.getValNo()].Flags;
    if (Flags.isByVal()) {
      // The caller's copy of the struct; its address is the argument.
      // An empty struct still needs an object to point at.
      int FI = MFI.CreateFixedObject(std::max(1u, Flags.getByValSize()),
                                     VA.getLocMemOffset() + ReturnAddressSize,
                                     /*IsImmutable=*/false);
      InVals.push_back(DAG.getFrameIndex(FI, MVT::i16));
      continue;
    }
    int FI = MFI.CreateFixedObject(VT.getStoreSize(),
                                   VA.getLocMemOffset() + ReturnAddressSize,
                                   /*IsImmutable=*/true);
    SDValue FIN = DAG.getFrameIndex(FI, MVT::i16);
    InVals.push_back(DAG.getLoad(VT, DL, Chain, FIN,
                                 MachinePointerInfo::getFixedStack(MF, FI)));
  }

  // The variadic arguments start right after the fixed stack arguments.
  if (IsVarArg) {
    int FI = MFI.CreateFixedObject(1, CCInfo.getStackSize() + ReturnAddressSize,
                                   /*IsImmutable=*/true);
    MF.getInfo<M6800FunctionInfo>()->setVarArgsFrameIndex(FI);
  }
  return Chain;
}

SDValue M6800TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                       SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  MachineFunction &MF = DAG.getMachineFunction();

  // No tail calls yet.
  CLI.IsTailCall = false;

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CLI.CallConv, CLI.IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(CLI.Outs, CC_M6800);
  unsigned NumBytes = CCInfo.getStackSize();

  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  // Stack arguments are stored into the reserved call frame, which starts at
  // SP + 1. JSR then pushes the return address, so the callee finds them
  // two bytes further on.
  SmallVector<std::pair<Register, SDValue>, 2> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  SDValue StackPtr;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    SDValue Arg = CLI.OutVals[I];
    ISD::ArgFlagsTy Flags = CLI.Outs[I].Flags;

    if (VA.isRegLoc()) {
      RegsToPass.push_back({VA.getLocReg(), Arg});
      continue;
    }

    if (!StackPtr)
      StackPtr = DAG.getCopyFromReg(Chain, DL, M6800::SP, MVT::i16);
    SDValue Addr =
        DAG.getNode(ISD::ADD, DL, MVT::i16, StackPtr,
                    DAG.getIntPtrConstant(VA.getLocMemOffset() + 1, DL));
    if (Flags.isByVal()) {
      // Arg points at the struct. Copy it inline: a call to memcpy here
      // would nest inside this call's frame.
      SDValue Size = DAG.getConstant(Flags.getByValSize(), DL, MVT::i16);
      MemOpChains.push_back(DAG.getMemcpy(
          Chain, DL, Addr, Arg, Size, Align(1), Align(1), /*isVol=*/false,
          /*AlwaysInline=*/true, /*CI=*/nullptr, std::nullopt,
          MachinePointerInfo(), MachinePointerInfo()));
      continue;
    }
    MemOpChains.push_back(
        DAG.getStore(Chain, DL, Arg, Addr, MachinePointerInfo()));
  }
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  SDValue Glue;
  for (auto &[Reg, Val] : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, Val, Glue);
    Glue = Chain.getValue(1);
  }

  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i16,
                                        G->getOffset());
  else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i16);
  else if (Callee.getOpcode() == M6800ISD::WRAPPER)
    Callee = Callee.getOperand(0);

  SmallVector<SDValue, 8> Ops = {Chain, Callee};
  for (auto &[Reg, Val] : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg, Val.getValueType()));
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  Ops.push_back(
      DAG.getRegisterMask(TRI->getCallPreservedMask(MF, CLI.CallConv)));
  if (Glue.getNode())
    Ops.push_back(Glue);

  SDVTList VTs = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(M6800ISD::CALL, DL, VTs, Ops);
  Glue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, DL);
  Glue = Chain.getValue(1);

  // Results.
  SmallVector<CCValAssign, 4> RVLocs;
  CCState RetInfo(CLI.CallConv, CLI.IsVarArg, MF, RVLocs, *DAG.getContext());
  RetInfo.AnalyzeCallResult(CLI.Ins, RetCC_M6800);
  for (const CCValAssign &VA : RVLocs) {
    SDValue Val =
        DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), Glue);
    Chain = Val.getValue(1);
    Glue = Val.getValue(2);
    InVals.push_back(Val);
  }
  return Chain;
}

bool M6800TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_M6800);
}

SDValue
M6800TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                 bool IsVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 const SDLoc &DL, SelectionDAG &DAG) const {
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_M6800);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);
  // At O0, copy into D last. The other half of an i32 may be computed in D,
  // and the fast register allocator cannot move it out of D's way once D
  // holds its own half. The greedy allocator does better with D first.
  bool DLast = getTargetMachine().getOptLevel() == CodeGenOptLevel::None;
  for (unsigned N = RVLocs.size(), J = 0; J != N; ++J) {
    unsigned I = DLast ? N - 1 - J : J;
    const CCValAssign &VA = RVLocs[I];
    assert(VA.isRegLoc() && "M6800 returns values in registers only");
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[I], Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);
  return DAG.getNode(M6800ISD::RET_GLUE, DL, MVT::Other, RetOps);
}

//===----------------------------------------------------------------------===//
// Inline assembly
//===----------------------------------------------------------------------===//

// Constraint letters, besides the generic ones:
//   a  the index register X
//   q  an 8-bit accumulator (A or B)
//   d  the D accumulator (B for 8-bit values)
TargetLowering::ConstraintType
M6800TargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'a':
    case 'q':
    case 'd':
      return C_RegisterClass;
    default:
      break;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
M6800TargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                  StringRef Constraint,
                                                  MVT VT) const {
  bool Byte = VT == MVT::i8;
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      // Wider values take several 16-bit registers.
      return {0, Byte ? &M6800::ACC8RegClass : &M6800::GR16RegClass};
    case 'a':
      return {0, &M6800::IDX16RegClass};
    case 'q':
      return {0, &M6800::ACC8RegClass};
    case 'd':
      return Byte ? std::make_pair(unsigned(M6800::B), &M6800::ACC8RegClass)
                  : std::make_pair(unsigned(M6800::D), &M6800::ACC16RegClass);
    default:
      break;
    }
  }
  std::pair<unsigned, const TargetRegisterClass *> R =
      TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
  // CC does not copy to or from the allocatable registers: as an operand it
  // is an error, not a copy the backend cannot emit.  As a clobber (VT
  // Other) it stands.
  if (VT != MVT::Other && R.first == M6800::CC)
    return {0, nullptr};
  return R;
}
