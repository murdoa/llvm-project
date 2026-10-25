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

// Wraps a target address (global, external symbol, block address) so the
// address-mode selector can recognise it.
def M6809Wrapper : SDNode<"M6809ISD::WRAPPER",
                          SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                               SDTCisPtrTy<0>]>>;

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

// Memory operand: base register plus offset. "off,reg" is indexed ("2,s",
// ",x"). With no base register it is extended addressing ("sym+2", "49152").
def memidx : Operand<i16> {
  let MIOperandInfo = (ops PTR16:$base, i16imm:$offset);
  let PrintMethod = "printMemIndexedOperand";
}

def addr : ComplexPattern<iPTR, 2, "SelectAddr", [frameindex], []>;

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

let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RTS : M6809Inst<(outs), (ins), "rts", [(M6809RetGlue)], 1>;

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

// Register to register. TFR leaves the flags alone unless CC is the
// destination.
let isMoveReg = 1 in {
  def TFR8 : M6809Inst<(outs ACC8:$dst), (ins ACC8:$src),
                       "tfr\t$src, $dst", [], 2>;
  def TFR16 : M6809Inst<(outs XFER16:$dst), (ins XFER16:$src),
                        "tfr\t$src, $dst", [], 2>;
}

// Indexed loads and stores. Size is a guess (no offset or a 5-bit one) until
// the encoder knows the real postbyte form.
let Defs = [CC] in {
  let mayLoad = 1, canFoldAsLoad = 1 in {
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

// Stack pointer adjustment for the prologue and epilogue. LEAS leaves the
// flags alone.
let Defs = [S], Uses = [S] in
def LEASi : M6809Inst<(outs), (ins i16imm_op:$off), "leas\t$off,s", [], 2>;

//===----------------------------------------------------------------------===//
// Stack push and pull of a single register
//===----------------------------------------------------------------------===//

// PSHS/PULS leave the flags alone. They are what makes register-register
// arithmetic possible: "pshs x" then "addd ,s++".
let Defs = [S], Uses = [S] in {
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
    let Uses = [S], Defs = [S, CC], mayLoad = 1 in
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

// 16-bit add and subtract exist only on D (ADDD/SUBD).
multiclass ALU16D<string mnem, SDNode node> {
  let Constraints = "$src = $dst", Defs = [CC] in {
    def i : M6809Inst<(outs ACC16:$dst), (ins ACC16:$src, i16imm_op:$imm),
                      mnem # "\t#$imm",
                      [(set ACC16:$dst, (node ACC16:$src, imm:$imm))], 3>;
    def m : M6809Inst<(outs ACC16:$dst), (ins ACC16:$src, memidx:$addr),
                      mnem # "\t$addr",
                      [(set ACC16:$dst, (node ACC16:$src, (load addr:$addr)))],
                      2> {
      let mayLoad = 1;
    }
    let Uses = [S], Defs = [S, CC], mayLoad = 1 in
    def pop : M6809Inst<(outs ACC16:$dst), (ins ACC16:$src),
                        mnem # "\t,s++", [], 2>;
    let Uses = [S], Defs = [S, CC] in
    def rr : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, GR16:$rhs), "",
                         [(set ACC16:$dst, (node ACC16:$src, GR16:$rhs))]>;
  }
}

defm ADDD : ALU16D<"addd", add>;
defm SUBD : ALU16D<"subd", sub>;

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
    def rr : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, GR16:$rhs), "",
                         [(set ACC16:$dst, (node ACC16:$src, GR16:$rhs))]>;
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
  // CLRA: zero the high byte of D.
  def CLRA : M6809Inst<(outs ACC16:$dst), (ins ACC16:$src), "clra",
                       [(set ACC16:$dst, (and ACC16:$src, 255))], 1>;
}

def : Pat<(i16 (anyext ACC8:$s)),
          (INSERT_SUBREG (i16 (IMPLICIT_DEF)), ACC8:$s, sub_lo)>;
def : Pat<(i16 (zext ACC8:$s)),
          (CLRA (INSERT_SUBREG (i16 (IMPLICIT_DEF)), ACC8:$s, sub_lo))>;
def : Pat<(i16 (sext ACC8:$s)),
          (SEX (INSERT_SUBREG (i16 (IMPLICIT_DEF)), ACC8:$s, sub_lo))>;
def : Pat<(i8 (trunc GR16:$s)), (EXTRACT_SUBREG GR16:$s, sub_lo)>;

//===----------------------------------------------------------------------===//
// Addresses
//===----------------------------------------------------------------------===//

def : Pat<(M6809Wrapper tglobaladdr:$a), (LD16i tglobaladdr:$a)>;
def : Pat<(M6809Wrapper texternalsym:$a), (LD16i texternalsym:$a)>;
def : Pat<(M6809Wrapper tblockaddress:$a), (LD16i tblockaddress:$a)>;
