//===-- M6800InstrInfo.td - M6800 Instruction defs ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Instructions that exist once per register (LDAA/LDAB, LDD/LDX) are
// modelled as one instruction over a register class, with the register name
// spliced into the mnemonic ("lda${dst}" prints "ldaa", "ld${dst}" prints
// "ldx"). The encoder chooses the opcode from the register.
//
// The 6800 has a single index register and no stack-relative addressing.
// Stack slots are memory operands based on SP until after frame lowering,
// when M6800FrameAccess rewrites them to "tsx" and an offset from X.
//
//===----------------------------------------------------------------------===//

include "M6800InstrFormats.td"

//===----------------------------------------------------------------------===//
// Target-specific DAG nodes
//===----------------------------------------------------------------------===//

def M6800RetGlue : SDNode<"M6800ISD::RET_GLUE", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
// The return of an interrupt handler.
def M6800RtiGlue : SDNode<"M6800ISD::RTI_GLUE", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

// Wraps a target address (global, external symbol, block address) so the
// address-mode selector can recognise it.
def M6800Wrapper : SDNode<"M6800ISD::WRAPPER",
                          SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                               SDTCisPtrTy<0>]>>;

// Compare lhs with rhs and branch on condition cc (an M6800CC::CondCode).
// Kept as one node, and one pseudo until after register allocation, so
// nothing that changes the flags can land between the compare and the
// branch.
def M6800CmpBr : SDNode<"M6800ISD::CMPBR",
                        SDTypeProfile<0, 4, [SDTCisSameAs<0, 1>,
                                             SDTCisVT<2, i8>,
                                             SDTCisVT<3, OtherVT>]>,
                        [SDNPHasChain]>;

// Subtract rhs and the carry from lhs, and branch on cc: the high half of an
// i32 compare, after a SUBC of the low halves.
def M6800SbcBr : SDNode<"M6800ISD::SBCBR",
                        SDTypeProfile<0, 4, [SDTCisSameAs<0, 1>,
                                             SDTCisVT<2, i8>,
                                             SDTCisVT<3, OtherVT>]>,
                        [SDNPHasChain, SDNPInGlue]>;

// result = (lhs cc rhs) ? tval : fval
def M6800SelectCC : SDNode<"M6800ISD::SELECT_CC",
                           SDTypeProfile<1, 5, [SDTCisSameAs<0, 1>,
                                                SDTCisSameAs<1, 2>,
                                                SDTCisSameAs<3, 4>,
                                                SDTCisVT<5, i8>]>>;

// Variable-sized stack objects. DYNALLOC moves SP down by its first operand
// and returns the lowest byte of the new space, aligned down to the second.
// STACKSAVE gives SP + 1, the value TSX gives, and STACKRESTORE takes it
// back.
def M6800DynAlloc : SDNode<"M6800ISD::DYNALLOC",
                           SDTypeProfile<1, 2, [SDTCisVT<0, i16>,
                                                SDTCisVT<1, i16>,
                                                SDTCisVT<2, i16>]>,
                           [SDNPHasChain, SDNPSideEffect]>;
def M6800StackSave : SDNode<"ISD::STACKSAVE",
                            SDTypeProfile<1, 0, [SDTCisVT<0, i16>]>,
                            [SDNPHasChain, SDNPSideEffect]>;
def M6800StackRestore : SDNode<"ISD::STACKRESTORE",
                               SDTypeProfile<0, 1, [SDTCisVT<0, i16>]>,
                               [SDNPHasChain, SDNPSideEffect]>;

def M6800Call : SDNode<"M6800ISD::CALL",
                       SDTypeProfile<0, -1, [SDTCisVT<0, i16>]>,
                       [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                        SDNPVariadic]>;
// A sibling call: the callee returns straight to this function's caller.
def M6800TailCall : SDNode<"M6800ISD::TAIL_CALL",
                           SDTypeProfile<0, -1, [SDTCisVT<0, i16>]>,
                           [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def SDT_M6800CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i16>,
                                            SDTCisVT<1, i16>]>;
def SDT_M6800CallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;

def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_M6800CallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end : SDNode<"ISD::CALLSEQ_END", SDT_M6800CallSeqEnd,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

//===----------------------------------------------------------------------===//
// Operands
//===----------------------------------------------------------------------===//

def i8imm_op : Operand<i8>;
def i16imm_op : Operand<i16>;

// Memory operand: base register, offset and addressing mode (an
// M6800Mem::Mode). With X as the base it is indexed ("2,x"); with no base
// it is extended ("sym+2"), or direct when the assembler asks for it. SP as
// the base is a stack slot, only until M6800FrameAccess runs.
def memidx : Operand<i16> {
  let MIOperandInfo = (ops PTR16:$base, i16imm:$offset, i8imm:$mode);
  let PrintMethod = "printMemOperand";
}

def addr : ComplexPattern<iPTR, 3, "SelectAddr", [frameindex], []>;

// Branch condition, printed as the mnemonic suffix ("eq", "cc", ...).
def ccode : Operand<i8> {
  let PrintMethod = "printCondCode";
}

def brtarget : Operand<OtherVT>;

//===----------------------------------------------------------------------===//
// Pseudo instructions
//===----------------------------------------------------------------------===//

let Defs = [SP], Uses = [SP] in {
  def ADJCALLSTACKDOWN : M6800Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                                     "#ADJCALLSTACKDOWN",
                                     [(callseq_start timm:$amt1, timm:$amt2)]>;
  def ADJCALLSTACKUP : M6800Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                                   "#ADJCALLSTACKUP",
                                   [(callseq_end timm:$amt1, timm:$amt2)]>;
}

//===----------------------------------------------------------------------===//
// Control flow
//===----------------------------------------------------------------------===//

let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RTS : M6800Inst<(outs), (ins), "rts", [(M6800RetGlue)], 1>;

// Calls. The register mask operand added by call lowering says which
// registers the callee may clobber.
let isCall = 1, Uses = [SP], Defs = [CC], isCodeGenOnly = 1 in {
  def JSRe : M6800Inst<(outs), (ins i16imm_op:$dst), "jsr\t$dst", [], 3>;
  def JSRr : M6800Inst<(outs), (ins IDX16:$r), "jsr\t0,$r",
                       [(M6800Call IDX16:$r)], 2>;
}
def : Pat<(M6800Call tglobaladdr:$dst), (JSRe tglobaladdr:$dst)>;
def : Pat<(M6800Call texternalsym:$dst), (JSRe texternalsym:$dst)>;

// Sibling calls, "jmp sym" and "jmp 0,x": a call and a return in one. The
// epilogue goes before them.
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [SP],
    isCodeGenOnly = 1 in {
  def TAILJMPe : M6800Inst<(outs), (ins i16imm_op:$dst), "jmp\t$dst", [], 3>;
  def TAILJMPr : M6800Inst<(outs), (ins IDX16:$r), "jmp\t0,$r",
                           [(M6800TailCall IDX16:$r)], 2>;
}
def : Pat<(M6800TailCall tglobaladdr:$dst), (TAILJMPe tglobaladdr:$dst)>;
def : Pat<(M6800TailCall texternalsym:$dst), (TAILJMPe texternalsym:$dst)>;

//===----------------------------------------------------------------------===//
// Moves
//===----------------------------------------------------------------------===//

// Immediate loads: ldaa/ldab #n, ldd/ldx #nn. They set N and Z, clear V and
// leave C alone.
let Defs = [CC], isReMaterializable = 1, isAsCheapAsAMove = 1,
    isMoveImm = 1 in {
  def LD8i : M6800Inst<(outs ACC8:$dst), (ins i8imm_op:$imm),
                       "lda${dst}\t#$imm",
                       [(set ACC8:$dst, imm:$imm)], 2>;
  def LD16i : M6800Inst<(outs GR16:$dst), (ins i16imm_op:$imm),
                        "ld${dst}\t#$imm",
                        [(set GR16:$dst, imm:$imm)], 3>;
}

// clra/clrb: one byte shorter than "ldab #0". Also clears C.
let Defs = [CC], isReMaterializable = 1, isAsCheapAsAMove = 1,
    AddedComplexity = 1 in
def CLR8 : M6800Inst<(outs ACC8:$dst), (ins), "clr${dst}",
                     [(set ACC8:$dst, 0)], 1>;

// An add of two registers. A constant operand is better as an offset or
// immediate than loaded into D.
def addrr : PatFrag<(ops node:$a, node:$b), (add node:$a, node:$b), [{
  return !isa<ConstantSDNode>(N->getOperand(0)) &&
         !isa<ConstantSDNode>(N->getOperand(1));
}]>;

// Loads and stores. Size is a guess until the encoder knows the mode. A load
// is rematerialized only when its memory cannot change, such as an argument
// on the stack; with so few registers, reloading those beats spilling.
let Defs = [CC] in {
  let mayLoad = 1, canFoldAsLoad = 1, isReMaterializable = 1 in {
    def LD8m : M6800Inst<(outs ACC8:$dst), (ins memidx:$addr),
                         "lda${dst}\t$addr",
                         [(set ACC8:$dst, (load addr:$addr))], 2>;
    def LD16m : M6800Inst<(outs GR16:$dst), (ins memidx:$addr),
                          "ld${dst}\t$addr",
                          [(set GR16:$dst, (load addr:$addr))], 2>;
  }
  let mayStore = 1 in {
    def ST8m : M6800Inst<(outs), (ins ACC8:$src, memidx:$addr),
                         "sta${src}\t$addr",
                         [(store ACC8:$src, addr:$addr)], 2>;
    def ST16m : M6800Inst<(outs), (ins GR16:$src, memidx:$addr),
                          "st${src}\t$addr",
                          [(store GR16:$src, addr:$addr)], 2>;
  }
}

// The stack pointer as a plain register.
let Defs = [SP, CC] in {
  def LDSi : M6800Inst<(outs), (ins i16imm_op:$imm), "lds\t#$imm", [], 3>;
  let mayLoad = 1 in
  def LDSm : M6800Inst<(outs), (ins memidx:$addr), "lds\t$addr", [], 2>;
}
let Uses = [SP], Defs = [CC], mayStore = 1 in
def STSm : M6800Inst<(outs), (ins memidx:$addr), "sts\t$addr", [], 2>;

// Register transfers. TAB and TBA set N and Z and clear V; the others leave
// the flags alone. TSX gives X = SP + 1, the lowest byte in use, and TXS
// sets SP = X - 1.
let Defs = [CC] in {
  let Uses = [A], Defs = [B, CC] in
  def TAB : M6800Inst<(outs), (ins), "tab", [], 1>;
  let Uses = [B], Defs = [A, CC] in
  def TBA : M6800Inst<(outs), (ins), "tba", [], 1>;
}
let Uses = [D, X], Defs = [D, X] in
def XGDX : M6800Inst<(outs), (ins), "xgdx", [], 1>;
let Uses = [SP], Defs = [X] in
def TSX : M6800Inst<(outs), (ins), "tsx", [], 1>;
let Uses = [X], Defs = [SP] in
def TXS : M6800Inst<(outs), (ins), "txs", [], 1>;

// X and SP plus or minus one. INX and DEX set Z.
let Uses = [X], Defs = [X, CC] in {
  def INX : M6800Inst<(outs), (ins), "inx", [], 1>;
  def DEX : M6800Inst<(outs), (ins), "dex", [], 1>;
}
let Uses = [SP], Defs = [SP] in {
  def INS : M6800Inst<(outs), (ins), "ins", [], 1>;
  def DES : M6800Inst<(outs), (ins), "des", [], 1>;
}
// X += B, unsigned. Leaves the flags alone.
let Uses = [X, B], Defs = [X] in
def ABX : M6800Inst<(outs), (ins), "abx", [], 1>;

// x + (zero-extended b) is "abx", 1 byte and 1 cycle; x + 2 * b, an index
// into an array of words, is two of them. Expanded after register
// allocation.
let Constraints = "$src = $dst", AddedComplexity = 10 in {
  def ABXp : M6800Pseudo<(outs IDX16:$dst), (ins IDX16:$src, BREG:$b), "",
                         [(set IDX16:$dst, (add IDX16:$src, (zext BREG:$b)))]>;
  def ABX2p : M6800Pseudo<(outs IDX16:$dst), (ins IDX16:$src, BREG:$b), "",
                          []>; // pattern after M6800Lsl
}

//===----------------------------------------------------------------------===//
// Stack push and pull of a single register
//===----------------------------------------------------------------------===//

// psha/pshb/pshx and pula/pulb/pulx leave the flags alone.
let Defs = [SP], Uses = [SP], isCodeGenOnly = 1 in {
  let mayStore = 1 in {
    def PSH8 : M6800Inst<(outs), (ins ACC8:$r), "psh${r}", [], 1>;
    def PSHX : M6800Inst<(outs), (ins IDX16:$r), "pshx", [], 1>;
  }
  let mayLoad = 1 in {
    def PUL8 : M6800Inst<(outs ACC8:$r), (ins), "pul${r}", [], 1>;
    def PULX : M6800Inst<(outs IDX16:$r), (ins), "pulx", [], 1>;
  }
}

//===----------------------------------------------------------------------===//
// Arithmetic and logic
//===----------------------------------------------------------------------===//

// Each operation comes in the forms the selector can use: immediate,
// memory, and register-register. The register-register form is a pseudo,
// expanded after register allocation: A and B use ABA/SBA where they can,
// and otherwise the right operand is pushed and read back through X, which
// the pseudo clobbers.

multiclass ALU8<string mnem, SDNode node> {
  let Constraints = "$src = $dst", Defs = [CC] in {
    def i : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src, i8imm_op:$imm),
                      mnem # "${dst}\t#$imm",
                      [(set ACC8:$dst, (node ACC8:$src, imm:$imm))], 2>;
    def m : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src, memidx:$addr),
                      mnem # "${dst}\t$addr",
                      [(set ACC8:$dst, (node ACC8:$src, (load addr:$addr)))],
                      2> {
      let mayLoad = 1;
    }
    let Uses = [SP], Defs = [SP, X, CC] in
    def rr : M6800Pseudo<(outs ACC8:$dst), (ins ACC8:$src, ACC8:$rhs), "",
                         [(set ACC8:$dst, (node ACC8:$src, ACC8:$rhs))]>;
  }
}

defm ADD8 : ALU8<"add", add>;
defm SUB8 : ALU8<"sub", sub>;
defm AND8 : ALU8<"and", and>;
defm OR8 : ALU8<"ora", or>;
defm EOR8 : ALU8<"eor", xor>;

// aba (A += B), sba (A -= B), cba (compare A with B).
let Uses = [A, B], Defs = [A, CC] in {
  def ABA : M6800Inst<(outs), (ins), "aba", [], 1>;
  def SBA : M6800Inst<(outs), (ins), "sba", [], 1>;
}
let Uses = [A, B], Defs = [CC] in
def CBA : M6800Inst<(outs), (ins), "cba", [], 1>;

// Add and subtract with carry: adca/adcb, sbca/sbcb. Only used to build the
// 16-bit forms below.
multiclass ALU8C<string mnem> {
  let Constraints = "$src = $dst", Defs = [CC], Uses = [CC] in {
    def i : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src, i8imm_op:$imm),
                      mnem # "${dst}\t#$imm", [], 2>;
    let mayLoad = 1 in
    def m : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src, memidx:$addr),
                      mnem # "${dst}\t$addr", [], 2>;
  }
}

defm ADC8 : ALU8C<"adc">;
defm SBC8 : ALU8C<"sbc">;

// 16-bit add and subtract exist only on D (ADDD/SUBD). In the register
// forms the other operand is X, pushed and read back: "pshx; tsx; addd 0,x;
// pulx".
multiclass ALU16D<string mnem, SDNode node> {
  let Constraints = "$src = $dst", Defs = [CC] in {
    // No pattern: immediate adds select ADD16ri below, and the DAG turns
    // subtracting a constant into adding its negation.
    def i : M6800Inst<(outs ACC16:$dst), (ins ACC16:$src, i16imm_op:$imm),
                      mnem # "\t#$imm", [], 3>;
    def m : M6800Inst<(outs ACC16:$dst), (ins ACC16:$src, memidx:$addr),
                      mnem # "\t$addr",
                      [(set ACC16:$dst, (node ACC16:$src, (load addr:$addr)))],
                      2> {
      let mayLoad = 1;
    }
    let Uses = [SP], Defs = [SP, CC] in
    def rr : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src, IDX16:$rhs), "",
                         [(set ACC16:$dst, (node ACC16:$src, IDX16:$rhs))]>;
  }
}

defm ADDD : ALU16D<"addd", add>;
defm SUBD : ALU16D<"subd", sub>;

// The low half of a multi-word add or subtract: plain ADDD/SUBD, which set C.
def : Pat<(addc ACC16:$s, imm:$i), (ADDDi ACC16:$s, imm:$i)>;
def : Pat<(addc ACC16:$s, (load addr:$a)), (ADDDm ACC16:$s, addr:$a)>;
def : Pat<(addc ACC16:$s, IDX16:$r), (ADDDrr ACC16:$s, IDX16:$r)>;
def : Pat<(subc ACC16:$s, imm:$i), (SUBDi ACC16:$s, imm:$i)>;
def : Pat<(subc ACC16:$s, (load addr:$a)), (SUBDm ACC16:$s, addr:$a)>;
def : Pat<(subc ACC16:$s, IDX16:$r), (SUBDrr ACC16:$s, IDX16:$r)>;
// SUBC for the low half of an i32 compare, which the generic combines must
// not fold away (subc x, 0 would leave a carry_false glue).
def M6800Subc : SDNode<"M6800ISD::SUBC", SDTIntBinOp, [SDNPOutGlue]>;
def : Pat<(M6800Subc ACC16:$s, imm:$i), (SUBDi ACC16:$s, imm:$i)>;
def : Pat<(M6800Subc ACC16:$s, (load addr:$a)), (SUBDm ACC16:$s, addr:$a)>;
def : Pat<(M6800Subc ACC16:$s, IDX16:$r), (SUBDrr ACC16:$s, IDX16:$r)>;

// The higher halves: there is no 16-bit add or subtract with carry, so
// these pseudos become adcb/adca (sbcb/sbca), low byte first. Nothing the
// register allocator inserts in between (loads, stores, TAB/TBA, XGDX,
// pushes and pulls, TSX) changes C.
multiclass CARRY16<SDNode node> {
  let Constraints = "$src = $dst", Defs = [CC], Uses = [CC] in {
    def i : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src, i16imm_op:$imm),
                        "", [(set ACC16:$dst, (node ACC16:$src, imm:$imm))]>;
    let mayLoad = 1 in
    def m : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src, memidx:$addr), "",
                        [(set ACC16:$dst,
                              (node ACC16:$src, (load addr:$addr)))]>;
    let Uses = [SP, CC], Defs = [SP, CC] in
    def rr : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src, IDX16:$rhs), "",
                         [(set ACC16:$dst, (node ACC16:$src, IDX16:$rhs))]>;
  }
}

defm ADC16 : CARRY16<adde>;
defm SBC16 : CARRY16<sube>;

// Add a constant to D or X. Expanded after register allocation to ADDD for
// D, and for X to INX/DEX or "xgdx; addd #n; xgdx".
let Constraints = "$src = $dst", Defs = [CC] in
def ADD16ri : M6800Pseudo<(outs GR16:$dst), (ins GR16:$src, i16imm_op:$imm), "",
                          [(set GR16:$dst, (add GR16:$src, imm:$imm))]>;

// The address of a stack slot, in D or X. Expanded by M6800FrameAccess.
let Defs = [CC] in
def LEAFI : M6800Pseudo<(outs GR16:$dst), (ins memidx:$addr), "", []>;

// Variable-sized stack objects, expanded after register allocation:
//   DYNALLOC:       pshb; psha; tsx; xgdx; tsx; subd 0,x; xgdx; inx; inx; txs
//   STACKSAVE16:    tsx
//   STACKRESTORE16: txs
let hasSideEffects = 1 in {
  let Uses = [SP], Defs = [D, SP, CC] in
  def DYNALLOC : M6800Pseudo<(outs IDX16:$dst),
                             (ins ACC16:$size, i16imm:$align), "",
                             [(set IDX16:$dst,
                                   (M6800DynAlloc ACC16:$size, timm:$align))]>;
  let Uses = [SP] in
  def STACKSAVE16 : M6800Pseudo<(outs IDX16:$dst), (ins), "",
                                [(set IDX16:$dst, (M6800StackSave))]>;
  let Uses = [SP], Defs = [SP] in
  def STACKRESTORE16 : M6800Pseudo<(outs), (ins IDX16:$src), "",
                                   [(M6800StackRestore IDX16:$src)]>;
}

// There is no 16-bit AND/OR/EOR. These pseudos become the 8-bit instruction
// on A (high byte) then B (low byte).
multiclass LOGIC16<SDNode node> {
  let Constraints = "$src = $dst", Defs = [CC] in {
    def i : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src, i16imm_op:$imm),
                        "", [(set ACC16:$dst, (node ACC16:$src, imm:$imm))]>;
    let mayLoad = 1 in
    def m : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src, memidx:$addr), "",
                        [(set ACC16:$dst,
                              (node ACC16:$src, (load addr:$addr)))]>;
    let Uses = [SP], Defs = [SP, CC] in
    def rr : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src, IDX16:$rhs), "",
                         [(set ACC16:$dst, (node ACC16:$src, IDX16:$rhs))]>;
  }
}

defm AND16 : LOGIC16<and>;
defm OR16 : LOGIC16<or>;
defm EOR16 : LOGIC16<xor>;

//===----------------------------------------------------------------------===//
// Extension and truncation
//===----------------------------------------------------------------------===//

let Constraints = "$src = $dst", Defs = [CC] in {
  // Sign-extend B into A: "tba; asla; ldaa #0; sbca #0", expanded after
  // register allocation.
  def SEX : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                        [(set ACC16:$dst, (sext_inreg ACC16:$src, i8))]>;
  // CLRA: zero the high byte of D. Emitted as CLR8 A.
  def CLRA : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                         [(set ACC16:$dst, (and ACC16:$src, 255))]>;
}

def : Pat<(i16 (anyext ACC8:$s)),
          (INSERT_SUBREG (i16 (IMPLICIT_DEF)), ACC8:$s, sub_lo)>;
def : Pat<(i16 (zext ACC8:$s)),
          (CLRA (INSERT_SUBREG (i16 (IMPLICIT_DEF)), ACC8:$s, sub_lo))>;
def : Pat<(i16 (sext ACC8:$s)),
          (SEX (INSERT_SUBREG (i16 (IMPLICIT_DEF)), ACC8:$s, sub_lo))>;
def : Pat<(i8 (trunc GR16:$s)),
          (EXTRACT_SUBREG (i16 (COPY_TO_REGCLASS GR16:$s, ACC16)), sub_lo)>;

//===----------------------------------------------------------------------===//
// Addresses
//===----------------------------------------------------------------------===//

def : Pat<(M6800Wrapper tglobaladdr:$a), (LD16i tglobaladdr:$a)>;
def : Pat<(M6800Wrapper texternalsym:$a), (LD16i texternalsym:$a)>;
def : Pat<(M6800Wrapper tblockaddress:$a), (LD16i tblockaddress:$a)>;
def : Pat<(M6800Wrapper tjumptable:$a), (LD16i tjumptable:$a)>;
// An index into a table: "addd #table" rather than the address in X and
// D added to it through the stack.
def : Pat<(add GR16:$s, (M6800Wrapper tglobaladdr:$a)),
          (ADD16ri GR16:$s, tglobaladdr:$a)>;
def : Pat<(add GR16:$s, (M6800Wrapper texternalsym:$a)),
          (ADD16ri GR16:$s, texternalsym:$a)>;

//===----------------------------------------------------------------------===//
// Compares and branches
//===----------------------------------------------------------------------===//

// tsta/tstb: compare with zero. TST clears V and C.
let Defs = [CC] in
def TST8 : M6800Inst<(outs), (ins ACC8:$r), "tst${r}", [], 1>;

// Real compares, used after the CB* pseudos below are expanded.
// cmpa/cmpb, and cpx.
let Defs = [CC] in {
  def CMP8i : M6800Inst<(outs), (ins ACC8:$lhs, i8imm_op:$imm),
                        "cmp${lhs}\t#$imm", [], 2>;
  def CPXi : M6800Inst<(outs), (ins IDX16:$lhs, i16imm_op:$imm),
                       "cpx\t#$imm", [], 3>;
  let mayLoad = 1 in {
    def CMP8m : M6800Inst<(outs), (ins ACC8:$lhs, memidx:$addr),
                          "cmp${lhs}\t$addr", [], 2>;
    def CPXm : M6800Inst<(outs), (ins IDX16:$lhs, memidx:$addr),
                         "cpx\t$addr", [], 2>;
  }
}

let isBranch = 1, isTerminator = 1 in {
  let isBarrier = 1 in
  def BRA : M6800Inst<(outs), (ins brtarget:$dst), "bra\t$dst",
                      [(br bb:$dst)], 2>;

  let Uses = [CC] in
  def Bcc : M6800Inst<(outs), (ins ccode:$cc, brtarget:$dst), "b${cc}\t$dst",
                      [], 2>;

  // Compare-and-branch pseudos. Expanded after register allocation into a
  // compare followed by Bcc. A 16-bit compare of D goes through X
  // ("xgdx; cpx; xgdx") or, when D dies, SUBD.
  let Defs = [CC] in {
    def CB8ri : M6800Pseudo<(outs),
                            (ins ACC8:$lhs, i8imm_op:$rhs, ccode:$cc,
                                 brtarget:$dst), "",
                            [(M6800CmpBr ACC8:$lhs, (i8 imm:$rhs), timm:$cc,
                                         bb:$dst)]>;
    def CB16ri : M6800Pseudo<(outs),
                             (ins GR16:$lhs, i16imm_op:$rhs, ccode:$cc,
                                  brtarget:$dst), "",
                             [(M6800CmpBr GR16:$lhs, (i16 imm:$rhs), timm:$cc,
                                          bb:$dst)]>;
    // A byte in memory against zero: "tst mem", which needs no register.
    // TST sets the flags as a compare with zero would.
    let mayLoad = 1, AddedComplexity = 1 in
    def CB8m0 : M6800Pseudo<(outs),
                            (ins memidx:$addr, ccode:$cc, brtarget:$dst), "",
                            [(M6800CmpBr (i8 (load addr:$addr)), (i8 0),
                                         timm:$cc, bb:$dst)]>;
    let mayLoad = 1, Uses = [SP], Defs = [SP, CC] in {
      def CB8rm : M6800Pseudo<(outs),
                              (ins ACC8:$lhs, memidx:$rhs, ccode:$cc,
                                   brtarget:$dst), "",
                              [(M6800CmpBr ACC8:$lhs, (i8 (load addr:$rhs)),
                                           timm:$cc, bb:$dst)]>;
      // D or X against memory.
      def CB16rm : M6800Pseudo<(outs),
                               (ins GR16:$lhs, memidx:$rhs, ccode:$cc,
                                    brtarget:$dst), "",
                               [(M6800CmpBr GR16:$lhs, (i16 (load addr:$rhs)),
                                            timm:$cc, bb:$dst)]>;
    }
    let Uses = [SP], Defs = [SP, CC] in {
      // A against B is CBA, with the condition swapped for B against A.
      def CB8rr : M6800Pseudo<(outs),
                              (ins ACC8:$lhs, ACC8:$rhs, ccode:$cc,
                                   brtarget:$dst), "",
                              [(M6800CmpBr ACC8:$lhs, ACC8:$rhs, timm:$cc,
                                           bb:$dst)]>;
      // D against X: "pshx; tsx; subd 0,x; pulx", with D pushed and pulled
      // around it when still live. The left side may be in X too, so that
      // no copy into D comes just before the terminator: there the register
      // allocator may need an accumulator to move a byte, and finds none.
      // A spilled right side then folds into CB16rm, CPX for an X.
      def CB16rr : M6800Pseudo<(outs),
                               (ins GR16:$lhs, IDX16:$rhs, ccode:$cc,
                                    brtarget:$dst), "",
                               [(M6800CmpBr GR16:$lhs, IDX16:$rhs, timm:$cc,
                                            bb:$dst)]>;
    }
  }
}

// The high half of an i32 compare-and-branch: SBC16* then Bcc after
// register allocation. The subtraction overwrites D, which is pushed and
// pulled around it when still live (PULA/PULB leave the flags alone).
let isBranch = 1, isTerminator = 1, Uses = [CC, SP], Defs = [CC, SP] in {
  def SBCBRi : M6800Pseudo<(outs),
                           (ins ACC16:$lhs, i16imm_op:$rhs, ccode:$cc,
                                brtarget:$dst), "",
                           [(M6800SbcBr ACC16:$lhs, (i16 imm:$rhs), timm:$cc,
                                        bb:$dst)]>;
  let mayLoad = 1 in
  def SBCBRm : M6800Pseudo<(outs),
                           (ins ACC16:$lhs, memidx:$rhs, ccode:$cc,
                                brtarget:$dst), "",
                           [(M6800SbcBr ACC16:$lhs, (i16 (load addr:$rhs)),
                                        timm:$cc, bb:$dst)]>;
  def SBCBRrr : M6800Pseudo<(outs),
                            (ins ACC16:$lhs, IDX16:$rhs, ccode:$cc,
                                 brtarget:$dst), "",
                            [(M6800SbcBr ACC16:$lhs, IDX16:$rhs, timm:$cc,
                                         bb:$dst)]>;
}

// select_cc. The custom inserter turns these into a CB* diamond and a PHI.
multiclass SELECT<RegisterClass ValRC> {
  let usesCustomInserter = 1 in {
    def _c8 : M6800Pseudo<(outs ValRC:$dst),
                          (ins ValRC:$t, ValRC:$f, ACC8:$l, ACC8:$r,
                               i8imm_op:$cc), "",
                          [(set ValRC:$dst,
                                (M6800SelectCC ValRC:$t, ValRC:$f, (i8 ACC8:$l),
                                               ACC8:$r, timm:$cc))]>;
    def _c16 : M6800Pseudo<(outs ValRC:$dst),
                           (ins ValRC:$t, ValRC:$f, GR16:$l, IDX16:$r,
                                i8imm_op:$cc), "",
                           [(set ValRC:$dst,
                                 (M6800SelectCC ValRC:$t, ValRC:$f,
                                                (i16 GR16:$l), IDX16:$r,
                                                timm:$cc))]>;
  }
}

// The same with an immediate right-hand side: "x < 0 ? a : b".
multiclass SELECTI<RegisterClass ValRC> {
  let usesCustomInserter = 1 in {
    def _c8i : M6800Pseudo<(outs ValRC:$dst),
                           (ins ValRC:$t, ValRC:$f, ACC8:$l, i8imm_op:$r,
                                i8imm_op:$cc), "",
                           [(set ValRC:$dst,
                                 (M6800SelectCC ValRC:$t, ValRC:$f,
                                                (i8 ACC8:$l), (i8 imm:$r),
                                                timm:$cc))]>;
    def _c16i : M6800Pseudo<(outs ValRC:$dst),
                            (ins ValRC:$t, ValRC:$f, GR16:$l, i16imm_op:$r,
                                 i8imm_op:$cc), "",
                            [(set ValRC:$dst,
                                  (M6800SelectCC ValRC:$t, ValRC:$f,
                                                 (i16 GR16:$l), (i16 imm:$r),
                                                 timm:$cc))]>;
  }
}

defm SELECT8 : SELECT<ACC8>, SELECTI<ACC8>;
defm SELECT16 : SELECT<GR16>, SELECTI<GR16>;

// {dst.lo, dst.hi} = (lhs cc rhs) ? t : f for i32 lhs and rhs, given as
// halves: t.lo, t.hi, f.lo, f.hi, lhs.lo, lhs.hi, rhs.lo, rhs.hi, cc.
def M6800Select32
    : SDNode<"M6800ISD::SELECT32",
             SDTypeProfile<2, 9, [SDTCisVT<0, i16>, SDTCisVT<1, i16>,
                                  SDTCisVT<2, i16>, SDTCisVT<3, i16>,
                                  SDTCisVT<4, i16>, SDTCisVT<5, i16>,
                                  SDTCisVT<6, i16>, SDTCisVT<7, i16>,
                                  SDTCisVT<8, i16>, SDTCisVT<9, i16>,
                                  SDTCisVT<10, i8>]>>;
let usesCustomInserter = 1 in {
  def SELECT32rr
      : M6800Pseudo<(outs GR16:$dlo, GR16:$dhi),
                    (ins GR16:$tlo, GR16:$thi, GR16:$flo, GR16:$fhi,
                         GR16:$llo, GR16:$lhi, GR16:$rlo, GR16:$rhi,
                         i8imm_op:$cc), "",
                    [(set GR16:$dlo, GR16:$dhi,
                          (M6800Select32 GR16:$tlo, GR16:$thi, GR16:$flo,
                                         GR16:$fhi, GR16:$llo, GR16:$lhi,
                                         GR16:$rlo, GR16:$rhi, timm:$cc))]>;
  def SELECT32ri
      : M6800Pseudo<(outs GR16:$dlo, GR16:$dhi),
                    (ins GR16:$tlo, GR16:$thi, GR16:$flo, GR16:$fhi,
                         GR16:$llo, GR16:$lhi, i16imm_op:$rlo, i16imm_op:$rhi,
                         i8imm_op:$cc), "",
                    [(set GR16:$dlo, GR16:$dhi,
                          (M6800Select32 GR16:$tlo, GR16:$thi, GR16:$flo,
                                         GR16:$fhi, GR16:$llo, GR16:$lhi,
                                         (i16 imm:$rlo), (i16 imm:$rhi),
                                         timm:$cc))]>;
}

//===----------------------------------------------------------------------===//
// Multiply
//===----------------------------------------------------------------------===//

// MUL: D = A * B, unsigned.
let Uses = [A, B], Defs = [D, CC] in
def MUL : M6800Inst<(outs), (ins), "mul", [], 1>;

// i8 multiply. The custom inserter copies the operands into A and B, emits
// MUL, and takes the low byte from B.
let usesCustomInserter = 1 in
def MUL8 : M6800Pseudo<(outs ACC8:$dst), (ins ACC8:$a, ACC8:$b), "",
                       [(set ACC8:$dst, (mul ACC8:$a, ACC8:$b))]>;

// The whole 16-bit product of two unsigned bytes, for i16 multiplies whose
// operands fit in a byte and for the i8 halves of wider ones.
def M6800UMul8 : SDNode<"M6800ISD::UMUL8",
                       SDTypeProfile<1, 2, [SDTCisVT<0, i16>, SDTCisVT<1, i8>,
                                            SDTCisVT<2, i8>]>,
                       [SDNPCommutative]>;
let usesCustomInserter = 1 in
def UMUL8 : M6800Pseudo<(outs ACC16:$dst), (ins ACC8:$a, ACC8:$b), "",
                       [(set ACC16:$dst, (M6800UMul8 ACC8:$a, ACC8:$b))]>;

//===----------------------------------------------------------------------===//
// Shifts
//===----------------------------------------------------------------------===//

// Shifts go one bit at a time. Shift lowering turns a constant amount into
// a chain of these nodes and a variable amount into a loop.
def SDT_M6800Shift1 : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisInt<0>]>;
def M6800Lsl : SDNode<"M6800ISD::LSL", SDT_M6800Shift1>;

let AddedComplexity = 11 in
def : Pat<(add IDX16:$src, (M6800Lsl (zext BREG:$b))),
          (ABX2p IDX16:$src, BREG:$b)>;
def M6800Lsr : SDNode<"M6800ISD::LSR", SDT_M6800Shift1>;
def M6800Asr : SDNode<"M6800ISD::ASR", SDT_M6800Shift1>;

// Shift by a variable amount. The amount is an i16 count, held in X so DEX
// can count it down.
def SDT_M6800ShiftLoop : SDTypeProfile<1, 2, [SDTCisSameAs<0, 1>,
                                              SDTCisInt<0>,
                                              SDTCisVT<2, i16>]>;
def M6800LslLoop : SDNode<"M6800ISD::LSL_LOOP", SDT_M6800ShiftLoop>;
def M6800LsrLoop : SDNode<"M6800ISD::LSR_LOOP", SDT_M6800ShiftLoop>;
def M6800AsrLoop : SDNode<"M6800ISD::ASR_LOOP", SDT_M6800ShiftLoop>;

// asla/aslb, lsra/lsrb, asra/asrb, and the rotates through carry.
let Constraints = "$src = $dst", Defs = [CC] in {
  def ASL8 : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src), "asl${dst}",
                       [(set ACC8:$dst, (M6800Lsl ACC8:$src))], 1>;
  def LSR8 : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src), "lsr${dst}",
                       [(set ACC8:$dst, (M6800Lsr ACC8:$src))], 1>;
  def ASR8 : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src), "asr${dst}",
                       [(set ACC8:$dst, (M6800Asr ACC8:$src))], 1>;
  let Uses = [CC] in {
    def ROL8 : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src), "rol${dst}", [],
                         1>;
    def ROR8 : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src), "ror${dst}", [],
                         1>;
  }

  // One-bit shifts of D: asld and lsrd, and "asra; rorb".
  def LSL16 : M6800Inst<(outs ACC16:$dst), (ins ACC16:$src), "asld",
                        [(set ACC16:$dst, (M6800Lsl ACC16:$src))], 1>;
  def LSR16 : M6800Inst<(outs ACC16:$dst), (ins ACC16:$src), "lsrd",
                        [(set ACC16:$dst, (M6800Lsr ACC16:$src))], 1>;
  def ASR16 : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                          [(set ACC16:$dst, (M6800Asr ACC16:$src))]>;
}

// nega/negb, coma/comb, inca/incb, deca/decb. INC and DEC leave C alone.
let Constraints = "$src = $dst", Defs = [CC] in {
  def NEG8 : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src), "neg${dst}",
                       [(set ACC8:$dst, (ineg ACC8:$src))], 1>;
  def COM8 : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src), "com${dst}",
                       [(set ACC8:$dst, (not ACC8:$src))], 1>;
  let AddedComplexity = 1 in {
    def INC8 : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src), "inc${dst}",
                         [(set ACC8:$dst, (add ACC8:$src, 1))], 1>;
    def DEC8 : M6800Inst<(outs ACC8:$dst), (ins ACC8:$src), "dec${dst}",
                         [(set ACC8:$dst, (add ACC8:$src, -1))], 1>;
  }
}

// Negate D: "nega; negb; sbca #0", expanded after register allocation.
let Constraints = "$src = $dst", Defs = [CC] in
def NEG16 : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                        [(set ACC16:$dst, (ineg ACC16:$src))]>;

// Carry-linked one-bit shifts for multi-word values. The first node shifts
// one word and leaves the bit that fell out in C; the second rotates C into
// the next word.
def SDT_M6800Shift1C : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                            SDTCisVT<0, i16>]>;
def M6800LslC : SDNode<"M6800ISD::LSL_C", SDT_M6800Shift1C, [SDNPOutGlue]>;
def M6800LsrC : SDNode<"M6800ISD::LSR_C", SDT_M6800Shift1C, [SDNPOutGlue]>;
def M6800AsrC : SDNode<"M6800ISD::ASR_C", SDT_M6800Shift1C, [SDNPOutGlue]>;
def M6800RolC : SDNode<"M6800ISD::ROL_C", SDT_M6800Shift1C,
                       [SDNPInGlue, SDNPOutGlue]>;
def M6800RorC : SDNode<"M6800ISD::ROR_C", SDT_M6800Shift1C,
                       [SDNPInGlue, SDNPOutGlue]>;

def : Pat<(M6800LslC ACC16:$x), (LSL16 ACC16:$x)>;
def : Pat<(M6800LsrC ACC16:$x), (LSR16 ACC16:$x)>;
def : Pat<(M6800AsrC ACC16:$x), (ASR16 ACC16:$x)>;

// "rolb; rola" and "rora; rorb".
let Constraints = "$src = $dst", Defs = [CC], Uses = [CC] in {
  def ROL16 : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                          [(set ACC16:$dst, (M6800RolC ACC16:$src))]>;
  def ROR16 : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                          [(set ACC16:$dst, (M6800RorC ACC16:$src))]>;
}

// Shifting D by 8 moves one byte across and fills the other. These are
// pseudos on a single D value, expanded after register allocation into
// "tba; clrb", "tab; clra" and "tab; asla; ldaa #0; sbca #0".
let Constraints = "$src = $dst", Defs = [CC] in {
  def SHL16_8 : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                            [(set ACC16:$dst, (shl ACC16:$src, (i8 8)))]>;
  def LSR16_8 : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                            [(set ACC16:$dst, (srl ACC16:$src, (i8 8)))]>;
  def ASR16_8 : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                            [(set ACC16:$dst, (sra ACC16:$src, (i8 8)))]>;
}
def : Pat<(i8 (trunc (srl ACC16:$x, (i8 8)))),
          (EXTRACT_SUBREG ACC16:$x, sub_hi)>;
def : Pat<(i8 (trunc (sra ACC16:$x, (i8 8)))),
          (EXTRACT_SUBREG ACC16:$x, sub_hi)>;

// Variable shifts. The custom inserter builds the loop.
multiclass SHIFTLOOP<SDNode node> {
  let usesCustomInserter = 1, Defs = [CC] in {
    def _8 : M6800Pseudo<(outs ACC8:$dst), (ins ACC8:$src, IDX16:$cnt), "",
                         [(set ACC8:$dst, (node ACC8:$src, IDX16:$cnt))]>;
    def _16 : M6800Pseudo<(outs ACC16:$dst), (ins ACC16:$src, IDX16:$cnt), "",
                          [(set ACC16:$dst, (node ACC16:$src, IDX16:$cnt))]>;
  }
}

defm LSLV : SHIFTLOOP<M6800LslLoop>;
defm LSRV : SHIFTLOOP<M6800LsrLoop>;
defm ASRV : SHIFTLOOP<M6800AsrLoop>;

//===----------------------------------------------------------------------===//
// Instructions only the assembler uses (for now)
//===----------------------------------------------------------------------===//

// bita/bitb: AND without keeping the result.
let Defs = [CC] in {
  def BIT8i : M6800Inst<(outs), (ins ACC8:$r, i8imm_op:$imm),
                        "bit${r}\t#$imm", [], 2>;
  let mayLoad = 1 in
  def BIT8m : M6800Inst<(outs), (ins ACC8:$r, memidx:$addr),
                        "bit${r}\t$addr", [], 2>;
}

// Read-modify-write on memory, indexed or extended: "inc 3,x", "clr count".
multiclass RMW<string mnem> {
  let Defs = [CC], mayLoad = 1, mayStore = 1 in
  def m : M6800Inst<(outs), (ins memidx:$addr), mnem # "\t$addr", [], 2>;
}
defm NEG : RMW<"neg">;
defm COM : RMW<"com">;
defm LSR : RMW<"lsr">;
defm ASR : RMW<"asr">;
defm ASL : RMW<"asl">;
defm DEC : RMW<"dec">;
defm INC : RMW<"inc">;
defm CLR : RMW<"clr">;
let Uses = [CC] in {
  defm ROR : RMW<"ror">;
  defm ROL : RMW<"rol">;
}
let Defs = [CC], mayLoad = 1 in
def TSTm : M6800Inst<(outs), (ins memidx:$addr), "tst\t$addr", [], 2>;

// HD6301 logic on memory with an immediate, direct or indexed: "aim #$0f,
// $40" is mem &= $0f. TIM only sets the flags.
let Defs = [CC], mayLoad = 1 in {
  let mayStore = 1 in {
    def AIM : M6800Inst<(outs), (ins i8imm_op:$imm, memidx:$addr),
                        "aim\t#$imm, $addr", [], 3>;
    def OIM : M6800Inst<(outs), (ins i8imm_op:$imm, memidx:$addr),
                        "oim\t#$imm, $addr", [], 3>;
    def EIM : M6800Inst<(outs), (ins i8imm_op:$imm, memidx:$addr),
                        "eim\t#$imm, $addr", [], 3>;
  }
  def TIM : M6800Inst<(outs), (ins i8imm_op:$imm, memidx:$addr),
                      "tim\t#$imm, $addr", [], 3>;
}

// Jumps and calls through memory: "jmp 0,x", "jsr sub".
let isBranch = 1, isIndirectBranch = 1, isTerminator = 1, isBarrier = 1 in
def JMPm : M6800Inst<(outs), (ins memidx:$addr), "jmp\t$addr", [], 2>;
// An indirect branch through X (mode 0 is Auto).
def : Pat<(brind IDX16:$r), (JMPm IDX16:$r, 0, 0)>;

// __builtin_setjmp and __builtin_longjmp. The buffer holds, in 16-bit words:
// [0] the frame address and [2] SP + 1 (from STACKSAVE16), both stored by
// clang, and [1] the resume address and [3] __m6800_fp, stored by
// SJLJ_SETJMP. SJLJ_SETJMP gives 0, or 1 when SJLJ_LONGJMP resumes there.
def M6800SjLjSetjmp
    : SDNode<"M6800ISD::EH_SJLJ_SETJMP",
             SDTypeProfile<1, 1, [SDTCisVT<0, i16>, SDTCisPtrTy<1>]>,
             [SDNPHasChain, SDNPSideEffect]>;
def M6800SjLjLongjmp
    : SDNode<"M6800ISD::EH_SJLJ_LONGJMP", SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>,
             [SDNPHasChain, SDNPSideEffect]>;
let usesCustomInserter = 1, hasSideEffects = 1 in {
  def SJLJ_SETJMP : M6800Pseudo<(outs GR16:$dst), (ins IDX16:$buf), "",
                                [(set GR16:$dst, (M6800SjLjSetjmp IDX16:$buf))]>;
  let isTerminator = 1, isBarrier = 1 in
  def SJLJ_LONGJMP : M6800Pseudo<(outs), (ins IDX16:$buf), "",
                                 [(M6800SjLjLongjmp IDX16:$buf)]>;
}
// Marks where SJLJ_SETJMP's resume block is entered from. Emits nothing.
let isBranch = 1, isTerminator = 1, hasSideEffects = 1 in
def SJLJ_SETUP : M6800Pseudo<(outs), (ins brtarget:$dst), "", []>;
let isCall = 1, Uses = [SP], Defs = [SP] in
def JSRm : M6800Inst<(outs), (ins memidx:$addr), "jsr\t$addr", [], 2>;

// The never-taken branch, and the relative call.
def BRN : M6800Inst<(outs), (ins brtarget:$dst), "brn\t$dst", [], 2>;
let isCall = 1, Uses = [SP], Defs = [SP] in
def BSR : M6800Inst<(outs), (ins brtarget:$dst), "bsr\t$dst", [], 2>;

// Long forms that branches relax to when the target is out of range:
// "jmp target", "b<opposite> *+5; jmp target" and "jsr target". The
// assembler only.
let isCodeGenOnly = 1 in {
  let isBranch = 1, isTerminator = 1, isBarrier = 1 in
  def LBRA : M6800Inst<(outs), (ins brtarget:$dst), "jmp\t$dst", [], 3>;
  let isBranch = 1, isTerminator = 1, Uses = [CC] in
  def LBcc : M6800Inst<(outs), (ins ccode:$cc, brtarget:$dst),
                       "lb${cc}\t$dst", [], 5>;
  let isCall = 1, Uses = [SP], Defs = [SP] in
  def LBSR : M6800Inst<(outs), (ins brtarget:$dst), "jsr\t$dst", [], 3>;
}

// Inherent instructions with no operands.
class Inherent<string mnem, int size = 1>
    : M6800Inst<(outs), (ins), mnem, [], size>;
def NOP : Inherent<"nop">;
let Uses = [A, CC], Defs = [A, CC] in
def DAA : Inherent<"daa">;
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RTI : M6800Inst<(outs), (ins), "rti", [(M6800RtiGlue)], 1>;
def SWI : Inherent<"swi">;
def WAI : Inherent<"wai">;
def SLP : Inherent<"slp">;
let Defs = [CC] in {
  def CLC : Inherent<"clc">;
  def SEC : Inherent<"sec">;
  def CLI : Inherent<"cli">;
  def SEI : Inherent<"sei">;
  def CLV : Inherent<"clv">;
  def SEV : Inherent<"sev">;
}
let Uses = [A], Defs = [CC] in
def TAP : Inherent<"tap">;
let Uses = [CC], Defs = [A] in
def TPA : Inherent<"tpa">;

// Atomics: see M6800TargetLowering's constructor.
def : Pat<(i8 (atomic_load_nonext_8 addr:$a)), (LD8m addr:$a)>;
def : Pat<(i16 (atomic_load_nonext_16 addr:$a)), (LD16m addr:$a)>;
def : Pat<(atomic_store_8 ACC8:$v, addr:$a), (ST8m ACC8:$v, addr:$a)>;
def : Pat<(atomic_store_16 GR16:$v, addr:$a), (ST16m GR16:$v, addr:$a)>;
def : Pat<(atomic_fence timm, timm), (MEMBARRIER)>;
