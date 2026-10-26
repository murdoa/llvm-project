//===-- M6809InstrInfo.td - M6809 Instruction defs ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Many 6809 instructions exist once per register (LDA/LDB, LDD/LDX/LDY/LDU).
// They are modelled as one instruction over a register class, with the
// register name spliced into the mnemonic ("ld${dst}" prints "lda", "ldx").
// The encoder will choose the opcode from the register.
//
//===----------------------------------------------------------------------===//

include "M6809InstrFormats.td"

//===----------------------------------------------------------------------===//
// Target-specific DAG nodes
//===----------------------------------------------------------------------===//

def M6809RetGlue : SDNode<"M6809ISD::RET_GLUE", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
// The return of an interrupt handler.
def M6809RtiGlue : SDNode<"M6809ISD::RTI_GLUE", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

// Wraps a target address (global, external symbol, block address) so the
// address-mode selector can recognise it.
def M6809Wrapper : SDNode<"M6809ISD::WRAPPER",
                          SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                               SDTCisPtrTy<0>]>>;

// Compare lhs with rhs and branch on condition cc (an M6809CC::CondCode).
// Kept as one node, and one pseudo until after register allocation, so
// nothing that changes the flags can land between the compare and the
// branch.
def M6809CmpBr : SDNode<"M6809ISD::CMPBR",
                        SDTypeProfile<0, 4, [SDTCisSameAs<0, 1>,
                                             SDTCisVT<2, i8>,
                                             SDTCisVT<3, OtherVT>]>,
                        [SDNPHasChain]>;

// Subtract rhs and the carry from lhs, and branch on cc: the high half of an
// i32 compare, after a SUBC of the low halves.
def M6809SbcBr : SDNode<"M6809ISD::SBCBR",
                        SDTypeProfile<0, 4, [SDTCisSameAs<0, 1>,
                                             SDTCisVT<2, i8>,
                                             SDTCisVT<3, OtherVT>]>,
                        [SDNPHasChain, SDNPInGlue]>;

// result = (lhs cc rhs) ? tval : fval
def M6809SelectCC : SDNode<"M6809ISD::SELECT_CC",
                           SDTypeProfile<1, 5, [SDTCisSameAs<0, 1>,
                                                SDTCisSameAs<1, 2>,
                                                SDTCisSameAs<3, 4>,
                                                SDTCisVT<5, i8>]>>;

def M6809Call : SDNode<"M6809ISD::CALL",
                       SDTypeProfile<0, -1, [SDTCisVT<0, i16>]>,
                       [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                        SDNPVariadic]>;
// A sibling call: the callee returns straight to this function's caller.
def M6809TailCall : SDNode<"M6809ISD::TAIL_CALL",
                           SDTypeProfile<0, -1, [SDTCisVT<0, i16>]>,
                           [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def SDT_M6809CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i16>,
                                            SDTCisVT<1, i16>]>;
def SDT_M6809CallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;

def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_M6809CallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end : SDNode<"ISD::CALLSEQ_END", SDT_M6809CallSeqEnd,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

//===----------------------------------------------------------------------===//
// Operands
//===----------------------------------------------------------------------===//

def i8imm_op : Operand<i8>;
def i16imm_op : Operand<i16>;

// Memory operand: base register, offset and addressing mode (an
// M6809Mem::Mode). In code generation the mode is always Auto: "off,reg" is
// indexed ("2,s", ",x"), and with no base register it is extended
// addressing ("sym+2", "49152"). The assembler can ask for any 6809 mode.
def memidx : Operand<i16> {
  let MIOperandInfo = (ops PTR16:$base, i16imm:$offset, i8imm:$mode);
  let PrintMethod = "printMemIndexedOperand";
}

def addr : ComplexPattern<iPTR, 3, "SelectAddr", [frameindex], []>;

// Branch condition, printed as the mnemonic suffix ("eq", "hs", ...).
def ccode : Operand<i8> {
  let PrintMethod = "printCondCode";
}

def brtarget : Operand<OtherVT>;

//===----------------------------------------------------------------------===//
// Pseudo instructions
//===----------------------------------------------------------------------===//

let Defs = [S], Uses = [S] in {
  def ADJCALLSTACKDOWN : M6809Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                                     "#ADJCALLSTACKDOWN",
                                     [(callseq_start timm:$amt1, timm:$amt2)]>;
  def ADJCALLSTACKUP : M6809Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                                   "#ADJCALLSTACKUP",
                                   [(callseq_end timm:$amt1, timm:$amt2)]>;
}

//===----------------------------------------------------------------------===//
// Control flow
//===----------------------------------------------------------------------===//

// x + (zero-extended b) is "abx": 1 byte and 3 cycles, and the flags are
// left alone. Expanded to ABX after register allocation.
let Constraints = "$src = $dst", AddedComplexity = 10 in
def ABXp : M6809Pseudo<(outs XREG:$dst), (ins XREG:$src, BREG:$b), "",
                       [(set XREG:$dst, (add XREG:$src, (zext BREG:$b)))]>;

let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RTS : M6809Inst<(outs), (ins), "rts", [(M6809RetGlue)], 1>;

// Calls. The register mask operand added by call lowering says which
// registers the callee may clobber.
let isCall = 1, Uses = [S], Defs = [CC], isCodeGenOnly = 1 in {
  def JSRe : M6809Inst<(outs), (ins i16imm_op:$dst), "jsr\t$dst", [], 3>;
  def JSRr : M6809Inst<(outs), (ins IDX16:$r), "jsr\t,$r",
                       [(M6809Call IDX16:$r)], 2>;
}
def : Pat<(M6809Call tglobaladdr:$dst), (JSRe tglobaladdr:$dst)>;
def : Pat<(M6809Call texternalsym:$dst), (JSRe texternalsym:$dst)>;

// Sibling calls, "jmp sym" and "jmp ,x": a call and a return in one. The
// epilogue goes before them.
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [S],
    isCodeGenOnly = 1 in {
  def TAILJMPe : M6809Inst<(outs), (ins i16imm_op:$dst), "jmp\t$dst", [], 3>;
  def TAILJMPr : M6809Inst<(outs), (ins TAILIDX16:$r), "jmp\t,$r",
                           [(M6809TailCall TAILIDX16:$r)], 2>;
}
def : Pat<(M6809TailCall tglobaladdr:$dst), (TAILJMPe tglobaladdr:$dst)>;
def : Pat<(M6809TailCall texternalsym:$dst), (TAILJMPe texternalsym:$dst)>;

//===----------------------------------------------------------------------===//
// Moves
//===----------------------------------------------------------------------===//

// Immediate loads: lda/ldb #n, ldd/ldx/ldy/ldu #nn. They set N and Z and
// clear V.
let Defs = [CC], isReMaterializable = 1, isAsCheapAsAMove = 1,
    isMoveImm = 1 in {
  def LD8i : M6809Inst<(outs ACC8:$dst), (ins i8imm_op:$imm),
                       "ld${dst}\t#$imm",
                       [(set ACC8:$dst, imm:$imm)], 2>;
  def LD16i : M6809Inst<(outs GR16:$dst), (ins i16imm_op:$imm),
                        "ld${dst}\t#$imm",
                        [(set GR16:$dst, imm:$imm)], 3>;
}

// clra/clrb: one byte shorter than "ldb #0". Also clears C.
let Defs = [CC], isReMaterializable = 1, isAsCheapAsAMove = 1,
    AddedComplexity = 1 in
def CLR8 : M6809Inst<(outs ACC8:$dst), (ins), "clr${dst}",
                     [(set ACC8:$dst, 0)], 1>;

// Register to register. TFR leaves the flags alone unless CC is the
// destination.
let isMoveReg = 1, isCodeGenOnly = 1 in {
  def TFR8 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src),
                       "tfr\t$src, $dst", [], 2>;
  def TFR16 : M6809Inst<(outs XFER16:$dst), (ins XFER16:$src),
                        "tfr\t$src, $dst", [], 2>;
}

// An add of two registers. A constant operand is better as an offset or
// immediate than loaded into D.
// A symbol operand is better as a 16-bit offset ("ldb tab,x"), which leaves
// D free.
def addrr : PatFrag<(ops node:$a, node:$b), (add node:$a, node:$b), [{
  auto IsImm = [](SDValue V) {
    return isa<ConstantSDNode>(V) || V.getOpcode() == M6809ISD::WRAPPER;
  };
  return !IsImm(N->getOperand(0)) && !IsImm(N->getOperand(1));
}]>;

// Indexed loads and stores. Size is a guess (no offset or a 5-bit one) until
// the encoder knows the real postbyte form. A load is rematerialized only when
// its memory cannot change, such as an argument on the stack.
let Defs = [CC] in {
  let mayLoad = 1, canFoldAsLoad = 1, isReMaterializable = 1 in {
    def LD8m : M6809Inst<(outs ACC8:$dst), (ins memidx:$addr),
                         "ld${dst}\t$addr",
                         [(set ACC8:$dst, (load addr:$addr))], 2>;
    def LD16m : M6809Inst<(outs GR16:$dst), (ins memidx:$addr),
                          "ld${dst}\t$addr",
                          [(set GR16:$dst, (load addr:$addr))], 2>;
  }
  let mayStore = 1 in {
    def ST8m : M6809Inst<(outs), (ins ACC8:$src, memidx:$addr),
                         "st${src}\t$addr",
                         [(store ACC8:$src, addr:$addr)], 2>;
    def ST16m : M6809Inst<(outs), (ins GR16:$src, memidx:$addr),
                          "st${src}\t$addr",
                          [(store GR16:$src, addr:$addr)], 2>;
  }
}

// The same with D as the offset: "ldx d,u" for p[i]. The stored value
// cannot be in D, which holds the offset. Lowered to the memidx forms with
// the AccD mode.
let Defs = [CC], AddedComplexity = 20 in {
  let mayLoad = 1 in {
    def LD8dr : M6809Pseudo<(outs ACC8:$dst), (ins PTR16:$base, ACC16:$idx),
                            "", [(set ACC8:$dst,
                                      (load (addrr PTR16:$base, ACC16:$idx)))]>;
    def LD16dr : M6809Pseudo<(outs GR16:$dst), (ins PTR16:$base, ACC16:$idx),
                             "", [(set GR16:$dst,
                                       (load (addrr PTR16:$base, ACC16:$idx)))]>;
  }
  let mayStore = 1 in
  def ST16dr : M6809Pseudo<(outs),
                           (ins IDX16:$src, PTR16:$base, ACC16:$idx), "",
                           [(store IDX16:$src,
                                   (addrr PTR16:$base, ACC16:$idx))]>;
}

// Post-increment: ",x+" for a byte, ",x++" for a word, the pointer coming
// back advanced (see getPostIndexedAddressParts).  Selected by hand and
// lowered to the memidx forms with the PostInc modes.  "ldx ,x++" would
// lose the advanced pointer, so a loaded word goes to another register.
let Defs = [CC], hasSideEffects = 0 in {
  let mayLoad = 1, Constraints = "$ptr = $wb" in
  def LD8pi : M6809Pseudo<(outs ACC8:$dst, IDX16:$wb), (ins IDX16:$ptr), "",
                          []>;
  let mayLoad = 1, Constraints = "$ptr = $wb,@earlyclobber $dst" in
  def LD16pi : M6809Pseudo<(outs GR16:$dst, IDX16:$wb), (ins IDX16:$ptr), "",
                           []>;
  let mayStore = 1, Constraints = "$ptr = $wb" in {
    def ST8pi : M6809Pseudo<(outs IDX16:$wb), (ins ACC8:$src, IDX16:$ptr),
                            "", []>;
    def ST16pi : M6809Pseudo<(outs IDX16:$wb), (ins GR16:$src, IDX16:$ptr),
                             "", []>;
  }
}

// Pre-decrement: ",-x" and ",--x", selected and lowered the same way.
let Defs = [CC], hasSideEffects = 0 in {
  let mayLoad = 1, Constraints = "$ptr = $wb" in
  def LD8pd : M6809Pseudo<(outs ACC8:$dst, IDX16:$wb), (ins IDX16:$ptr), "",
                          []>;
  let mayLoad = 1, Constraints = "$ptr = $wb,@earlyclobber $dst" in
  def LD16pd : M6809Pseudo<(outs GR16:$dst, IDX16:$wb), (ins IDX16:$ptr), "",
                           []>;
  let mayStore = 1, Constraints = "$ptr = $wb" in {
    def ST8pd : M6809Pseudo<(outs IDX16:$wb), (ins ACC8:$src, IDX16:$ptr),
                            "", []>;
    def ST16pd : M6809Pseudo<(outs IDX16:$wb), (ins GR16:$src, IDX16:$ptr),
                             "", []>;
  }
}

// Stack pointer adjustment for the prologue and epilogue. LEAS leaves the
// flags alone.
let Defs = [S], Uses = [S], isCodeGenOnly = 1 in
def LEASi : M6809Inst<(outs), (ins i16imm_op:$off), "leas\t$off,s", [], 2>;

//===----------------------------------------------------------------------===//
// Stack push and pull of a single register
//===----------------------------------------------------------------------===//

// PSHS/PULS leave the flags alone. They are what makes register-register
// arithmetic possible: "pshs x" then "addd ,s++".
let Defs = [S], Uses = [S], isCodeGenOnly = 1 in {
  let mayStore = 1 in {
    def PSHS8 : M6809Inst<(outs), (ins ACC8:$r), "pshs\t$r", [], 2>;
    def PSHS16 : M6809Inst<(outs), (ins GR16:$r), "pshs\t$r", [], 2>;
  }
  let mayLoad = 1 in {
    def PULS8 : M6809Inst<(outs ACC8:$r), (ins), "puls\t$r", [], 2>;
    def PULS16 : M6809Inst<(outs GR16:$r), (ins), "puls\t$r", [], 2>;
  }
}

//===----------------------------------------------------------------------===//
// Arithmetic and logic
//===----------------------------------------------------------------------===//

// Each operation comes in three forms the selector can use (immediate,
// indexed memory, register-register) plus a "pop" form that reads its
// operand from ,s+ / ,s++. The register-register form is a pseudo that is
// expanded after register allocation into a push followed by the pop form.

multiclass ALU8<string mnem, SDNode node> {
  let Constraints = "$src = $dst", Defs = [CC] in {
    def i : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src, i8imm_op:$imm),
                      mnem # "${dst}\t#$imm",
                      [(set ACC8:$dst, (node ACC8:$src, imm:$imm))], 2>;
    def m : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src, memidx:$addr),
                      mnem # "${dst}\t$addr",
                      [(set ACC8:$dst, (node ACC8:$src, (load addr:$addr)))],
                      2> {
      let mayLoad = 1;
    }
    let Uses = [S], Defs = [S, CC], mayLoad = 1, isCodeGenOnly = 1 in
    def pop : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src),
                        mnem # "${dst}\t,s+", [], 2>;
    let Uses = [S], Defs = [S, CC] in
    def rr : M6809Pseudo<(outs ACC8:$dst), (ins ACC8:$src, ACC8:$rhs), "",
                         [(set ACC8:$dst, (node ACC8:$src, ACC8:$rhs))]>;
  }
}

defm ADD8 : ALU8<"add", add>;
defm SUB8 : ALU8<"sub", sub>;
defm AND8 : ALU8<"and", and>;
defm OR8 : ALU8<"or", or>;
defm EOR8 : ALU8<"eor", xor>;

// Add and subtract with carry: adca/adcb, sbca/sbcb. Only used to build the
// 16-bit forms below. The "pop2" form reads ,s++ and is the high-byte half
// of a register-register operation, whose low byte was read from 1,s.
multiclass ALU8C<string mnem> {
  let Constraints = "$src = $dst", Defs = [CC], Uses = [CC] in {
    def i : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src, i8imm_op:$imm),
                      mnem # "${dst}\t#$imm", [], 2>;
    let mayLoad = 1 in
    def m : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src, memidx:$addr),
                      mnem # "${dst}\t$addr", [], 2>;
    let Uses = [S, CC], Defs = [S, CC], mayLoad = 1, isCodeGenOnly = 1 in
    def pop2 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src),
                         mnem # "${dst}\t,s++", [], 2>;
  }
}

defm ADC8 : ALU8C<"adc">;
defm SBC8 : ALU8C<"sbc">;

// 16-bit add and subtract exist only on D (ADDD/SUBD). In the register
// forms here and below, the other operand is in X, Y or U: D holds the first,
// and allowing D for the second would let both come from instructions that
// write D, with no room left for a copy between them.
multiclass ALU16D<string mnem, SDNode node> {
  let Constraints = "$src = $dst", Defs = [CC] in {
    // No pattern: immediate adds select ADD16ri below, and the DAG turns
    // subtracting a constant into adding its negation.
    def i : M6809Inst<(outs ACC16:$dst), (ins ACC16:$src, i16imm_op:$imm),
                      mnem # "\t#$imm", [], 3>;
    def m : M6809Inst<(outs ACC16:$dst), (ins ACC16:$src, memidx:$addr),
                      mnem # "\t$addr",
                      [(set ACC16:$dst, (node ACC16:$src, (load addr:$addr)))],
                      2> {
      let mayLoad = 1;
    }
    let Uses = [S], Defs = [S, CC], mayLoad = 1, isCodeGenOnly = 1 in
    def pop : M6809Inst<(outs ACC16:$dst), (ins ACC16:$src),
                        mnem # "\t,s++", [], 2>;
    let Uses = [S], Defs = [S, CC] in
    def rr : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, IDX16:$rhs), "",
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
def M6809Subc : SDNode<"M6809ISD::SUBC", SDTIntBinOp, [SDNPOutGlue]>;
def : Pat<(M6809Subc ACC16:$s, imm:$i), (SUBDi ACC16:$s, imm:$i)>;
def : Pat<(M6809Subc ACC16:$s, (load addr:$a)), (SUBDm ACC16:$s, addr:$a)>;
def : Pat<(M6809Subc ACC16:$s, IDX16:$r), (SUBDrr ACC16:$s, IDX16:$r)>;

// The higher halves: the 6809 has no 16-bit add or subtract with carry, so
// these pseudos become adcb/adca (sbcb/sbca), low byte first. Nothing the
// register allocator inserts in between (LD, ST, TFR, LEA, PSHS) changes C.
multiclass CARRY16<SDNode node> {
  let Constraints = "$src = $dst", Defs = [CC], Uses = [CC] in {
    def i : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, i16imm_op:$imm),
                        "", [(set ACC16:$dst, (node ACC16:$src, imm:$imm))]>;
    let mayLoad = 1 in
    def m : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, memidx:$addr), "",
                        [(set ACC16:$dst,
                              (node ACC16:$src, (load addr:$addr)))]>;
    let Uses = [S, CC], Defs = [S, CC] in
    def rr : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, IDX16:$rhs), "",
                         [(set ACC16:$dst, (node ACC16:$src, IDX16:$rhs))]>;
  }
}

defm ADC16 : CARRY16<adde>;
defm SBC16 : CARRY16<sube>;

// Load effective address into X, Y or U: "leax 2,s", "leau -1,u". Sets Z.
let Defs = [CC] in
def LEA16 : M6809Inst<(outs IDX16:$dst), (ins memidx:$addr),
                      "lea${dst}\t$addr", [], 2>;

// dst = base + D: "leax d,u". Two bytes, against four for pushing one
// operand and adding it to D, and the sum lands in an index register ready
// to address memory.
let Defs = [CC], AddedComplexity = 1 in
def LEA16dr : M6809Pseudo<(outs IDX16:$dst), (ins PTR16:$base, ACC16:$idx),
                          "", [(set IDX16:$dst, (addrr PTR16:$base, ACC16:$idx))]>;

// Add a constant to any 16-bit register. Expanded after register allocation
// to ADDD for D, or LEAX/LEAY/LEAU n,r for the index registers, so values
// such as loop counters and pointers need not live in D.
let Constraints = "$src = $dst", Defs = [CC] in
def ADD16ri : M6809Pseudo<(outs GR16:$dst), (ins GR16:$src, i16imm_op:$imm), "",
                          [(set GR16:$dst, (add GR16:$src, imm:$imm))]>;

// The 6809 has no 16-bit AND/OR/EOR. These pseudos become the 8-bit
// instruction on A (high byte) then B (low byte).
multiclass LOGIC16<SDNode node> {
  let Constraints = "$src = $dst", Defs = [CC] in {
    def i : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, i16imm_op:$imm),
                        "", [(set ACC16:$dst, (node ACC16:$src, imm:$imm))]>;
    let mayLoad = 1 in
    def m : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, memidx:$addr), "",
                        [(set ACC16:$dst,
                              (node ACC16:$src, (load addr:$addr)))]>;
    let Uses = [S], Defs = [S, CC] in
    def rr : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, IDX16:$rhs), "",
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
  // SEX: sign-extend B into A.
  def SEX : M6809Inst<(outs ACC16:$dst), (ins ACC16:$src), "sex",
                      [(set ACC16:$dst, (sext_inreg ACC16:$src, i8))], 1>;
  // CLRA: zero the high byte of D. Emitted as CLR8 A.
  let isCodeGenOnly = 1 in
  def CLRA : M6809Inst<(outs ACC16:$dst), (ins ACC16:$src), "clra",
                       [(set ACC16:$dst, (and ACC16:$src, 255))], 1>;
}

def : Pat<(i16 (anyext ACC8:$s)),
          (INSERT_SUBREG (i16 (IMPLICIT_DEF)), ACC8:$s, sub_lo)>;
def : Pat<(i16 (zext ACC8:$s)),
          (CLRA (INSERT_SUBREG (i16 (IMPLICIT_DEF)), ACC8:$s, sub_lo))>;
def : Pat<(i16 (sext ACC8:$s)),
          (SEX (INSERT_SUBREG (i16 (IMPLICIT_DEF)), ACC8:$s, sub_lo))>;
// Through D: the low byte of W is F, which 8-bit code cannot use.
def : Pat<(i8 (trunc GR16:$s)),
          (EXTRACT_SUBREG (i16 (COPY_TO_REGCLASS GR16:$s, ACC16)), sub_lo)>;

//===----------------------------------------------------------------------===//
// Addresses
//===----------------------------------------------------------------------===//

def : Pat<(M6809Wrapper tglobaladdr:$a), (LD16i tglobaladdr:$a)>;
def : Pat<(M6809Wrapper texternalsym:$a), (LD16i texternalsym:$a)>;
def : Pat<(M6809Wrapper tblockaddress:$a), (LD16i tblockaddress:$a)>;
def : Pat<(M6809Wrapper tjumptable:$a), (LD16i tjumptable:$a)>;

//===----------------------------------------------------------------------===//
// Compares and branches
//===----------------------------------------------------------------------===//

// tsta/tstb: compare with zero.
let Defs = [CC] in
def TST8 : M6809Inst<(outs), (ins ACC8:$r), "tst${r}", [], 1>;

// Real compares, used after the CB* pseudos below are expanded.
// cmpa/cmpb, and cmpd/cmpx/cmpy/cmpu.
let Defs = [CC] in {
  def CMP8i : M6809Inst<(outs), (ins ACC8:$lhs, i8imm_op:$imm),
                        "cmp${lhs}\t#$imm", [], 2>;
  def CMP16i : M6809Inst<(outs), (ins GR16:$lhs, i16imm_op:$imm),
                         "cmp${lhs}\t#$imm", [], 4>;
  let mayLoad = 1 in {
    def CMP8m : M6809Inst<(outs), (ins ACC8:$lhs, memidx:$addr),
                          "cmp${lhs}\t$addr", [], 2>;
    def CMP16m : M6809Inst<(outs), (ins GR16:$lhs, memidx:$addr),
                           "cmp${lhs}\t$addr", [], 3>;
    let Uses = [S], Defs = [S, CC], isCodeGenOnly = 1 in {
      def CMP8pop : M6809Inst<(outs), (ins ACC8:$lhs), "cmp${lhs}\t,s+", [],
                              2>;
      def CMP16pop : M6809Inst<(outs), (ins GR16:$lhs), "cmp${lhs}\t,s++", [],
                               3>;
    }
  }
}

let isBranch = 1, isTerminator = 1 in {
  let isBarrier = 1 in
  def LBRA : M6809Inst<(outs), (ins brtarget:$dst), "lbra\t$dst",
                       [(br bb:$dst)], 3>;

  let Uses = [CC] in
  def LBcc : M6809Inst<(outs), (ins ccode:$cc, brtarget:$dst),
                       "lb${cc}\t$dst", [], 4>;

  // Compare-and-branch pseudos. Expanded after register allocation into a
  // CMP* followed by LBcc.
  let Defs = [CC] in {
    def CB8ri : M6809Pseudo<(outs),
                            (ins ACC8:$lhs, i8imm_op:$rhs, ccode:$cc,
                                 brtarget:$dst), "",
                            [(M6809CmpBr ACC8:$lhs, (i8 imm:$rhs), timm:$cc,
                                         bb:$dst)]>;
    def CB16ri : M6809Pseudo<(outs),
                             (ins GR16:$lhs, i16imm_op:$rhs, ccode:$cc,
                                  brtarget:$dst), "",
                             [(M6809CmpBr GR16:$lhs, (i16 imm:$rhs), timm:$cc,
                                          bb:$dst)]>;
    let mayLoad = 1 in {
      def CB8rm : M6809Pseudo<(outs),
                              (ins ACC8:$lhs, memidx:$rhs, ccode:$cc,
                                   brtarget:$dst), "",
                              [(M6809CmpBr ACC8:$lhs, (i8 (load addr:$rhs)),
                                           timm:$cc, bb:$dst)]>;
      def CB16rm : M6809Pseudo<(outs),
                               (ins GR16:$lhs, memidx:$rhs, ccode:$cc,
                                    brtarget:$dst), "",
                               [(M6809CmpBr GR16:$lhs, (i16 (load addr:$rhs)),
                                            timm:$cc, bb:$dst)]>;
    }
    let Uses = [S], Defs = [S, CC] in {
      def CB8rr : M6809Pseudo<(outs),
                              (ins ACC8:$lhs, ACC8:$rhs, ccode:$cc,
                                   brtarget:$dst), "",
                              [(M6809CmpBr ACC8:$lhs, ACC8:$rhs, timm:$cc,
                                           bb:$dst)]>;
      // As with ADDD, the second operand is not D, so that two values
      // computed in D can be compared.
      def CB16rr : M6809Pseudo<(outs),
                               (ins GR16:$lhs, IDX16:$rhs, ccode:$cc,
                                    brtarget:$dst), "",
                               [(M6809CmpBr GR16:$lhs, IDX16:$rhs, timm:$cc,
                                            bb:$dst)]>;
    }
  }
}

// The high half of an i32 compare-and-branch: SBC16* then LBcc after
// register allocation. The subtraction overwrites D, which is pushed and
// pulled around it when still live (PULS leaves the flags alone): a
// terminator that clobbers a register leaves the register allocator no
// place to move a value out of it.
let isBranch = 1, isTerminator = 1, Uses = [CC], Defs = [CC] in {
  def SBCBRi : M6809Pseudo<(outs),
                           (ins ACC16:$lhs, i16imm_op:$rhs, ccode:$cc,
                                brtarget:$dst), "",
                           [(M6809SbcBr ACC16:$lhs, (i16 imm:$rhs), timm:$cc,
                                        bb:$dst)]>;
  let mayLoad = 1 in
  def SBCBRm : M6809Pseudo<(outs),
                           (ins ACC16:$lhs, memidx:$rhs, ccode:$cc,
                                brtarget:$dst), "",
                           [(M6809SbcBr ACC16:$lhs, (i16 (load addr:$rhs)),
                                        timm:$cc, bb:$dst)]>;
  let Uses = [S, CC], Defs = [S, CC] in
  def SBCBRrr : M6809Pseudo<(outs),
                            (ins ACC16:$lhs, IDX16:$rhs, ccode:$cc,
                                 brtarget:$dst), "",
                            [(M6809SbcBr ACC16:$lhs, IDX16:$rhs, timm:$cc,
                                         bb:$dst)]>;
}

// select_cc. The custom inserter turns these into a CB* diamond and a PHI.
multiclass SELECT<RegisterClass ValRC> {
  let usesCustomInserter = 1 in {
    def _c8 : M6809Pseudo<(outs ValRC:$dst),
                          (ins ValRC:$t, ValRC:$f, ACC8:$l, ACC8:$r,
                               i8imm_op:$cc), "",
                          [(set ValRC:$dst,
                                (M6809SelectCC ValRC:$t, ValRC:$f, (i8 ACC8:$l),
                                               ACC8:$r, timm:$cc))]>;
    def _c16 : M6809Pseudo<(outs ValRC:$dst),
                           (ins ValRC:$t, ValRC:$f, GR16:$l, IDX16:$r,
                                i8imm_op:$cc), "",
                           [(set ValRC:$dst,
                                 (M6809SelectCC ValRC:$t, ValRC:$f,
                                                (i16 GR16:$l), IDX16:$r,
                                                timm:$cc))]>;
  }
}

// The same with an immediate right-hand side: "x < 0 ? a : b".
multiclass SELECTI<RegisterClass ValRC> {
  let usesCustomInserter = 1 in {
    def _c8i : M6809Pseudo<(outs ValRC:$dst),
                           (ins ValRC:$t, ValRC:$f, ACC8:$l, i8imm_op:$r,
                                i8imm_op:$cc), "",
                           [(set ValRC:$dst,
                                 (M6809SelectCC ValRC:$t, ValRC:$f,
                                                (i8 ACC8:$l), (i8 imm:$r),
                                                timm:$cc))]>;
    def _c16i : M6809Pseudo<(outs ValRC:$dst),
                            (ins ValRC:$t, ValRC:$f, GR16:$l, i16imm_op:$r,
                                 i8imm_op:$cc), "",
                            [(set ValRC:$dst,
                                  (M6809SelectCC ValRC:$t, ValRC:$f,
                                                 (i16 GR16:$l), (i16 imm:$r),
                                                 timm:$cc))]>;
  }
}

defm SELECT8 : SELECT<ACC8>, SELECTI<ACC8>;
defm SELECT16 : SELECT<GR16>, SELECTI<GR16>;

// {dst.lo, dst.hi} = (lhs cc rhs) ? t : f for i32 lhs and rhs, given as
// halves: t.lo, t.hi, f.lo, f.hi, lhs.lo, lhs.hi, rhs.lo, rhs.hi, cc.
def M6809Select32
    : SDNode<"M6809ISD::SELECT32",
             SDTypeProfile<2, 9, [SDTCisVT<0, i16>, SDTCisVT<1, i16>,
                                  SDTCisVT<2, i16>, SDTCisVT<3, i16>,
                                  SDTCisVT<4, i16>, SDTCisVT<5, i16>,
                                  SDTCisVT<6, i16>, SDTCisVT<7, i16>,
                                  SDTCisVT<8, i16>, SDTCisVT<9, i16>,
                                  SDTCisVT<10, i8>]>>;
let usesCustomInserter = 1 in {
  def SELECT32rr
      : M6809Pseudo<(outs GR16:$dlo, GR16:$dhi),
                    (ins GR16:$tlo, GR16:$thi, GR16:$flo, GR16:$fhi,
                         GR16:$llo, GR16:$lhi, GR16:$rlo, GR16:$rhi,
                         i8imm_op:$cc), "",
                    [(set GR16:$dlo, GR16:$dhi,
                          (M6809Select32 GR16:$tlo, GR16:$thi, GR16:$flo,
                                         GR16:$fhi, GR16:$llo, GR16:$lhi,
                                         GR16:$rlo, GR16:$rhi, timm:$cc))]>;
  def SELECT32ri
      : M6809Pseudo<(outs GR16:$dlo, GR16:$dhi),
                    (ins GR16:$tlo, GR16:$thi, GR16:$flo, GR16:$fhi,
                         GR16:$llo, GR16:$lhi, i16imm_op:$rlo, i16imm_op:$rhi,
                         i8imm_op:$cc), "",
                    [(set GR16:$dlo, GR16:$dhi,
                          (M6809Select32 GR16:$tlo, GR16:$thi, GR16:$flo,
                                         GR16:$fhi, GR16:$llo, GR16:$lhi,
                                         (i16 imm:$rlo), (i16 imm:$rhi),
                                         timm:$cc))]>;
}

//===----------------------------------------------------------------------===//
// Multiply
//===----------------------------------------------------------------------===//

// MUL: D = A * B, unsigned.
let Uses = [A, B], Defs = [D, CC] in
def MUL : M6809Inst<(outs), (ins), "mul", [], 1>;

// i8 multiply. The custom inserter copies the operands into A and B, emits
// MUL, and takes the low byte from B.
let usesCustomInserter = 1 in
def MUL8 : M6809Pseudo<(outs ACC8:$dst), (ins ACC8:$a, ACC8:$b), "",
                       [(set ACC8:$dst, (mul ACC8:$a, ACC8:$b))]>;

// The whole 16-bit product of two unsigned bytes, for i16 multiplies whose
// operands fit in a byte and for the i8 halves of wider ones.
def M6809UMul8 : SDNode<"M6809ISD::UMUL8",
                       SDTypeProfile<1, 2, [SDTCisVT<0, i16>, SDTCisVT<1, i8>,
                                            SDTCisVT<2, i8>]>,
                       [SDNPCommutative]>;
let usesCustomInserter = 1 in
def UMUL8 : M6809Pseudo<(outs ACC16:$dst), (ins ACC8:$a, ACC8:$b), "",
                       [(set ACC16:$dst, (M6809UMul8 ACC8:$a, ACC8:$b))]>;

//===----------------------------------------------------------------------===//
// Shifts
//===----------------------------------------------------------------------===//

// The 6809 shifts one bit at a time. Shift lowering turns a constant
// amount into a chain of these nodes and a variable amount into a loop.
def SDT_M6809Shift1 : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisInt<0>]>;
def M6809Lsl : SDNode<"M6809ISD::LSL", SDT_M6809Shift1>;
def M6809Lsr : SDNode<"M6809ISD::LSR", SDT_M6809Shift1>;
def M6809Asr : SDNode<"M6809ISD::ASR", SDT_M6809Shift1>;

// Every bit set to the sign bit: an arithmetic shift right by the width
// less one.
def M6809Sign : SDNode<"M6809ISD::SIGN", SDT_M6809Shift1>;

// Shift by a variable amount. The amount is an i16 count, held in an index
// register so "leax -1,x" can count it down.
def SDT_M6809ShiftLoop : SDTypeProfile<1, 2, [SDTCisSameAs<0, 1>,
                                              SDTCisInt<0>,
                                              SDTCisVT<2, i16>]>;
def M6809LslLoop : SDNode<"M6809ISD::LSL_LOOP", SDT_M6809ShiftLoop>;
def M6809LsrLoop : SDNode<"M6809ISD::LSR_LOOP", SDT_M6809ShiftLoop>;
def M6809AsrLoop : SDNode<"M6809ISD::ASR_LOOP", SDT_M6809ShiftLoop>;

// asla/aslb, lsra/lsrb, asra/asrb, and the rotates through carry.
let Constraints = "$src = $dst", Defs = [CC] in {
  def ASL8 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src), "asl${dst}",
                       [(set ACC8:$dst, (M6809Lsl ACC8:$src))], 1>;
  def LSR8 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src), "lsr${dst}",
                       [(set ACC8:$dst, (M6809Lsr ACC8:$src))], 1>;
  def ASR8 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src), "asr${dst}",
                       [(set ACC8:$dst, (M6809Asr ACC8:$src))], 1>;
  let Uses = [CC] in {
    def ROL8 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src), "rol${dst}", [],
                         1>;
    def ROR8 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src), "ror${dst}", [],
                         1>;
  }

  // One-bit shifts of D, expanded after register allocation into
  // "aslb; rola", "lsra; rorb" and "asra; rorb".
  def LSL16 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                          [(set ACC16:$dst, (M6809Lsl ACC16:$src))]>;
  def LSR16 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                          [(set ACC16:$dst, (M6809Lsr ACC16:$src))]>;
  def ASR16 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                          [(set ACC16:$dst, (M6809Asr ACC16:$src))]>;
}

// nega/negb, coma/comb, inca/incb, deca/decb. INC and DEC leave C alone.
let Constraints = "$src = $dst", Defs = [CC] in {
  def NEG8 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src), "neg${dst}",
                       [(set ACC8:$dst, (ineg ACC8:$src))], 1>;
  def COM8 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src), "com${dst}",
                       [(set ACC8:$dst, (not ACC8:$src))], 1>;
  let AddedComplexity = 1 in {
    def INC8 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src), "inc${dst}",
                         [(set ACC8:$dst, (add ACC8:$src, 1))], 1>;
    def DEC8 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src), "dec${dst}",
                         [(set ACC8:$dst, (add ACC8:$src, -1))], 1>;
  }
}

// Negate D: "nega; negb; sbca #0", expanded after register allocation.
let Constraints = "$src = $dst", Defs = [CC] in
def NEG16 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                        [(set ACC16:$dst, (ineg ACC16:$src))]>;

// Carry-linked one-bit shifts for multi-word values. The first node shifts
// one word and leaves the bit that fell out in C; the second rotates C into
// the next word.
def SDT_M6809Shift1C : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                            SDTCisVT<0, i16>]>;
def M6809LslC : SDNode<"M6809ISD::LSL_C", SDT_M6809Shift1C, [SDNPOutGlue]>;
def M6809LsrC : SDNode<"M6809ISD::LSR_C", SDT_M6809Shift1C, [SDNPOutGlue]>;
def M6809AsrC : SDNode<"M6809ISD::ASR_C", SDT_M6809Shift1C, [SDNPOutGlue]>;
def M6809RolC : SDNode<"M6809ISD::ROL_C", SDT_M6809Shift1C,
                       [SDNPInGlue, SDNPOutGlue]>;
def M6809RorC : SDNode<"M6809ISD::ROR_C", SDT_M6809Shift1C,
                       [SDNPInGlue, SDNPOutGlue]>;

def : Pat<(M6809LslC ACC16:$x), (LSL16 ACC16:$x)>;
def : Pat<(M6809LsrC ACC16:$x), (LSR16 ACC16:$x)>;
def : Pat<(M6809AsrC ACC16:$x), (ASR16 ACC16:$x)>;

// "rolb; rola" and "rora; rorb".
let Constraints = "$src = $dst", Defs = [CC], Uses = [CC] in {
  def ROL16 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                          [(set ACC16:$dst, (M6809RolC ACC16:$src))]>;
  def ROR16 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                          [(set ACC16:$dst, (M6809RorC ACC16:$src))]>;
}

// Shifting D by 8 moves one byte across and fills the other. These are
// pseudos on a single D value, expanded after register allocation into
// "tfr b,a; clrb", "tfr a,b; clra" and "tfr a,b; sex". Doing it with
// sub-register copies instead would need two values in D at once.
let Constraints = "$src = $dst", Defs = [CC] in {
  def SHL16_8 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                            [(set ACC16:$dst, (shl ACC16:$src, (i8 8)))]>;
  def LSR16_8 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                            [(set ACC16:$dst, (srl ACC16:$src, (i8 8)))]>;
  def ASR16_8 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                            [(set ACC16:$dst, (sra ACC16:$src, (i8 8)))]>;
}

// The sign of a byte or of a word, in every bit. A byte becomes "asl;
// ld #0; sbc #0" (LD leaves C alone), 5 bytes against 7 for seven ASRs; a
// word becomes "tfr a,b; sex; tfr a,b" in D, 5 bytes against 10. The word
// may come from any register, so a value that stays live (the low half of
// an int to long conversion) can move out of D rather than be spilled.
let Defs = [CC] in {
  let Constraints = "$src = $dst" in
  def SIGN8 : M6809Pseudo<(outs ACC8:$dst), (ins ACC8:$src), "",
                          [(set ACC8:$dst, (M6809Sign ACC8:$src))]>;
  def SIGN16 : M6809Pseudo<(outs ACC16:$dst), (ins GR16:$src), "",
                           [(set ACC16:$dst, (M6809Sign GR16:$src))]>;
}
def : Pat<(i8 (trunc (srl ACC16:$x, (i8 8)))),
          (EXTRACT_SUBREG ACC16:$x, sub_hi)>;
def : Pat<(i8 (trunc (sra ACC16:$x, (i8 8)))),
          (EXTRACT_SUBREG ACC16:$x, sub_hi)>;

// Variable shifts. The custom inserter builds the loop.
multiclass SHIFTLOOP<SDNode node> {
  let usesCustomInserter = 1, Defs = [CC] in {
    def _8 : M6809Pseudo<(outs ACC8:$dst), (ins ACC8:$src, IDX16:$cnt), "",
                         [(set ACC8:$dst, (node ACC8:$src, IDX16:$cnt))]>;
    def _16 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, IDX16:$cnt), "",
                          [(set ACC16:$dst, (node ACC16:$src, IDX16:$cnt))]>;
  }
}

defm LSLV : SHIFTLOOP<M6809LslLoop>;
defm LSRV : SHIFTLOOP<M6809LsrLoop>;
defm ASRV : SHIFTLOOP<M6809AsrLoop>;

// Rotates. The 6809 only rotates through carry, so a one-bit rotate puts the
// bit that falls out back in by hand:
//
//   left:  aslb; rola; adcb #0          (8 bits: aslb; adcb #0)
//   right: pshs b; lsr ,s+; rora; rorb  (8 bits: pshs b; lsr ,s+; rorb)
//
// The right rotate copies the low bit into C through a byte pushed and
// popped in one go. SWAPB is "exg a,b", a rotate of D by 8.
def M6809Rol1 : SDNode<"M6809ISD::ROL1", SDT_M6809Shift1>;
def M6809Ror1 : SDNode<"M6809ISD::ROR1", SDT_M6809Shift1>;
def M6809SwapB : SDNode<"M6809ISD::SWAPB", SDT_M6809Shift1C>;
def M6809RolLoop : SDNode<"M6809ISD::ROL_LOOP", SDT_M6809ShiftLoop>;
def M6809RorLoop : SDNode<"M6809ISD::ROR_LOOP", SDT_M6809ShiftLoop>;

let Constraints = "$src = $dst", Defs = [CC] in {
  def ROL1_8 : M6809Pseudo<(outs ACC8:$dst), (ins ACC8:$src), "",
                           [(set ACC8:$dst, (M6809Rol1 ACC8:$src))]>;
  def ROL1_16 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                            [(set ACC16:$dst, (M6809Rol1 ACC16:$src))]>;
  let Uses = [S], Defs = [S, CC] in {
    def ROR1_8 : M6809Pseudo<(outs ACC8:$dst), (ins ACC8:$src), "",
                             [(set ACC8:$dst, (M6809Ror1 ACC8:$src))]>;
    def ROR1_16 : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                              [(set ACC16:$dst, (M6809Ror1 ACC16:$src))]>;
  }
}
let Constraints = "$src = $dst" in
def SWAPB : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src), "",
                        [(set ACC16:$dst, (M6809SwapB ACC16:$src))]>;

defm ROLV : SHIFTLOOP<M6809RolLoop>;
defm RORV : SHIFTLOOP<M6809RorLoop>;

//===----------------------------------------------------------------------===//
// Instructions only the assembler uses (for now)
//===----------------------------------------------------------------------===//

// bita/bitb: AND without keeping the result.
let Defs = [CC] in {
  def BIT8i : M6809Inst<(outs), (ins ACC8:$r, i8imm_op:$imm),
                        "bit${r}\t#$imm", [], 2>;
  let mayLoad = 1 in
  def BIT8m : M6809Inst<(outs), (ins ACC8:$r, memidx:$addr),
                        "bit${r}\t$addr", [], 2>;
}

// Read-modify-write on memory: "inc 3,x", "clr <$40", "asl counter".
multiclass RMW<string mnem> {
  let Defs = [CC], mayLoad = 1, mayStore = 1 in
  def m : M6809Inst<(outs), (ins memidx:$addr), mnem # "\t$addr", [], 2>;
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
def TSTm : M6809Inst<(outs), (ins memidx:$addr), "tst\t$addr", [], 2>;

// Read-modify-write on memory for code generation. The 6809 reads the
// location before CLR writes it, and a hardware register can notice the read,
// so volatile accesses keep the plain load and store.
def nonvolatile_store : PatFrag<(ops node:$v, node:$a), (store node:$v, node:$a), [{
  return !cast<StoreSDNode>(N)->isVolatile();
}]>;
def nonvolatile_load : PatFrag<(ops node:$a), (load node:$a), [{
  return !cast<LoadSDNode>(N)->isVolatile();
}]>;
// "clr 3,x" rather than "clrb; stb 3,x".
def : Pat<(nonvolatile_store (i8 0), addr:$a), (CLRm addr:$a)>;
// "inc g" rather than "ldb g; incb; stb g".
def : Pat<(nonvolatile_store (add (i8 (nonvolatile_load addr:$a)), 1), addr:$a),
          (INCm addr:$a)>;
def : Pat<(nonvolatile_store (add (i8 (nonvolatile_load addr:$a)), -1), addr:$a),
          (DECm addr:$a)>;
def : Pat<(nonvolatile_store (not (i8 (nonvolatile_load addr:$a))), addr:$a),
          (COMm addr:$a)>;
def : Pat<(nonvolatile_store (ineg (i8 (nonvolatile_load addr:$a))), addr:$a),
          (NEGm addr:$a)>;

// Jumps and calls through any addressing mode.
let isBranch = 1, isIndirectBranch = 1, isTerminator = 1, isBarrier = 1 in
def JMPm : M6809Inst<(outs), (ins memidx:$addr), "jmp\t$addr", [], 2>;
// An indirect branch through a register: "jmp ,x" (mode 0 is Auto).
def : Pat<(brind PTR16:$r), (JMPm PTR16:$r, 0, 0)>;
// __builtin_setjmp and __builtin_longjmp. The buffer holds, in 16-bit words:
// [0] the frame address and [2] S, both stored by clang, and [1] the resume
// address and [3] U, stored by SJLJ_SETJMP. SJLJ_SETJMP gives 0, or 1 when
// SJLJ_LONGJMP resumes there.
def M6809SjLjSetjmp
    : SDNode<"M6809ISD::EH_SJLJ_SETJMP",
             SDTypeProfile<1, 1, [SDTCisVT<0, i16>, SDTCisPtrTy<1>]>,
             [SDNPHasChain, SDNPSideEffect]>;
def M6809SjLjLongjmp
    : SDNode<"M6809ISD::EH_SJLJ_LONGJMP", SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>,
             [SDNPHasChain, SDNPSideEffect]>;
let usesCustomInserter = 1, hasSideEffects = 1 in {
  def SJLJ_SETJMP : M6809Pseudo<(outs GR16:$dst), (ins IDX16:$buf), "",
                                [(set GR16:$dst, (M6809SjLjSetjmp IDX16:$buf))]>;
  let isTerminator = 1, isBarrier = 1 in
  def SJLJ_LONGJMP : M6809Pseudo<(outs), (ins IDX16:$buf), "",
                                 [(M6809SjLjLongjmp IDX16:$buf)]>;
}
// Marks where SJLJ_SETJMP's resume block is entered from. Emits nothing.
let isBranch = 1, isTerminator = 1, hasSideEffects = 1 in
def SJLJ_SETUP : M6809Pseudo<(outs), (ins brtarget:$dst), "", []>;

let isCall = 1, Uses = [S], Defs = [S] in
def JSRm : M6809Inst<(outs), (ins memidx:$addr), "jsr\t$addr", [], 2>;

// The stack pointer as a plain register.
let Defs = [S, CC] in {
  def LDSi : M6809Inst<(outs), (ins i16imm_op:$imm), "lds\t#$imm", [], 4>;
  let mayLoad = 1 in
  def LDSm : M6809Inst<(outs), (ins memidx:$addr), "lds\t$addr", [], 3>;
}
let Uses = [S], Defs = [CC] in {
  let mayStore = 1 in
  def STSm : M6809Inst<(outs), (ins memidx:$addr), "sts\t$addr", [], 3>;
  def CMPSi : M6809Inst<(outs), (ins i16imm_op:$imm), "cmps\t#$imm", [], 4>;
  let mayLoad = 1 in
  def CMPSm : M6809Inst<(outs), (ins memidx:$addr), "cmps\t$addr", [], 3>;
}
let Defs = [S], Uses = [S] in
def LEASm : M6809Inst<(outs), (ins memidx:$addr), "leas\t$addr", [], 2>;

// Register transfer and exchange, with any pair TFR/EXG can name.
def TFRrr : M6809Inst<(outs), (ins TFRREG:$src, TFRREG:$dst),
                      "tfr\t$src, $dst", [], 2>;
def EXGrr : M6809Inst<(outs), (ins TFRREG:$a, TFRREG:$b), "exg\t$a, $b", [],
                      2>;

// Push and pull a register list: "pshs a,b,x". The operand is the postbyte
// mask (bit 0 = CC ... bit 7 = PC).
def reglist : Operand<i8> {
  let PrintMethod = "printRegList";
}
def reglist_u : Operand<i8> {
  let PrintMethod = "printRegListU";
}
let Uses = [S], Defs = [S] in {
  def PSHSr : M6809Inst<(outs), (ins reglist:$regs), "pshs\t$regs", [], 2>;
  def PULSr : M6809Inst<(outs), (ins reglist:$regs), "puls\t$regs", [], 2>;
}
let Uses = [U], Defs = [U] in {
  def PSHUr : M6809Inst<(outs), (ins reglist_u:$regs), "pshu\t$regs", [], 2>;
  def PULUr : M6809Inst<(outs), (ins reglist_u:$regs), "pulu\t$regs", [], 2>;
}

// Short branches, and the never-taken branches.
let isBranch = 1, isTerminator = 1 in {
  let isBarrier = 1 in
  def BRA : M6809Inst<(outs), (ins brtarget:$dst), "bra\t$dst", [], 2>;
  let Uses = [CC] in
  def Bcc : M6809Inst<(outs), (ins ccode:$cc, brtarget:$dst), "b${cc}\t$dst",
                      [], 2>;
}
def BRN : M6809Inst<(outs), (ins brtarget:$dst), "brn\t$dst", [], 2>;
def LBRN : M6809Inst<(outs), (ins brtarget:$dst), "lbrn\t$dst", [], 4>;
let isCall = 1, Uses = [S], Defs = [S] in {
  def BSR : M6809Inst<(outs), (ins brtarget:$dst), "bsr\t$dst", [], 2>;
  def LBSR : M6809Inst<(outs), (ins brtarget:$dst), "lbsr\t$dst", [], 3>;
}

// Inherent instructions with no operands.
class Inherent<string mnem, int size = 1>
    : M6809Inst<(outs), (ins), mnem, [], size>;
def NOP : Inherent<"nop">;
def SYNC : Inherent<"sync">;
let Uses = [A, CC], Defs = [A, CC] in
def DAA : Inherent<"daa">;
let Uses = [X, B], Defs = [X] in
def ABX : Inherent<"abx">;
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RTI : M6809Inst<(outs), (ins), "rti", [(M6809RtiGlue)], 1>;
def SWI : Inherent<"swi">;
def SWI2 : Inherent<"swi2", 2>;
def SWI3 : Inherent<"swi3", 2>;

// Condition code operations with an immediate mask.
let Uses = [CC], Defs = [CC] in {
  def ORCC : M6809Inst<(outs), (ins i8imm_op:$imm), "orcc\t#$imm", [], 2>;
  def ANDCC : M6809Inst<(outs), (ins i8imm_op:$imm), "andcc\t#$imm", [], 2>;
  def CWAI : M6809Inst<(outs), (ins i8imm_op:$imm), "cwai\t#$imm", [], 2>;
}

// Atomics: see M6809TargetLowering's constructor.
def : Pat<(i8 (atomic_load_nonext_8 addr:$a)), (LD8m addr:$a)>;
def : Pat<(i16 (atomic_load_nonext_16 addr:$a)), (LD16m addr:$a)>;
def : Pat<(atomic_store_8 ACC8:$v, addr:$a), (ST8m ACC8:$v, addr:$a)>;
def : Pat<(atomic_store_16 GR16:$v, addr:$a), (ST16m GR16:$v, addr:$a)>;
def : Pat<(atomic_fence timm, timm), (MEMBARRIER)>;
