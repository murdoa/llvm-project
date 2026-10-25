//===-- M6309InstrInfo.td - HD6309 instructions ------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Instructions the HD6309 adds in emulation mode. Operations on E and F
// reuse the 8-bit definitions (COM8, LD8i, ST8m, ...) with E or F as the
// register, and LDW, STW and CMPW reuse LD16i/LD16m, ST16m and CMP16i/m
// with W; M6809Opcodes.def gives their encodings.
//
//===----------------------------------------------------------------------===//

def i32imm_op : Operand<i32>;

def Has6309 : Predicate<"Subtarget->has6309()">;

// Inherent operations on D or W: "negd", "lsrw", ...
let Constraints = "$src = $dst", Defs = [CC] in {
  def NEG16r : M6809Inst<(outs DW16:$dst), (ins DW16:$src), "neg${dst}", [], 2>;
  def COM16r : M6809Inst<(outs DW16:$dst), (ins DW16:$src), "com${dst}", [], 2>;
  def LSR16r : M6809Inst<(outs DW16:$dst), (ins DW16:$src), "lsr${dst}", [], 2>;
  def ASR16r : M6809Inst<(outs DW16:$dst), (ins DW16:$src), "asr${dst}", [], 2>;
  def ASL16r : M6809Inst<(outs DW16:$dst), (ins DW16:$src), "asl${dst}", [], 2>;
  def INC16r : M6809Inst<(outs DW16:$dst), (ins DW16:$src), "inc${dst}", [], 2>;
  def DEC16r : M6809Inst<(outs DW16:$dst), (ins DW16:$src), "dec${dst}", [], 2>;
  let Uses = [CC] in {
    def ROR16r : M6809Inst<(outs DW16:$dst), (ins DW16:$src), "ror${dst}", [],
                           2>;
    def ROL16r : M6809Inst<(outs DW16:$dst), (ins DW16:$src), "rol${dst}", [],
                           2>;
  }
}
let Defs = [CC] in {
  def CLR16r : M6809Inst<(outs DW16:$dst), (ins), "clr${dst}", [], 2>;
  def TST16r : M6809Inst<(outs), (ins DW16:$r), "tst${r}", [], 2>;
}

// D = W sign-extended to 32 bits, high half.
let Uses = [W], Defs = [CC] in
def SEXW : M6809Inst<(outs ACC16:$dst), (ins), "sexw", [], 1>;

// Push and pull W.
let Uses = [S, W], Defs = [S], mayStore = 1 in
def PSHSW : M6809Inst<(outs), (ins), "pshsw", [], 2>;
let Uses = [S], Defs = [S, W], mayLoad = 1 in
def PULSW : M6809Inst<(outs), (ins), "pulsw", [], 2>;
let Uses = [U, W], Defs = [U], mayStore = 1 in
def PSHUW : M6809Inst<(outs), (ins), "pshuw", [], 2>;
let Uses = [U], Defs = [U, W], mayLoad = 1 in
def PULUW : M6809Inst<(outs), (ins), "puluw", [], 2>;

// 16-bit operations on D and W with an immediate or memory operand.
multiclass ALU16<string mnem, RegisterClass RC, bit carry = 0,
                 bit store = 1> {
  let Defs = [CC], Uses = !if(carry, [CC], []<Register>) in {
    let Constraints = !if(store, "$src = $dst", "") in {
      def i : M6809Inst<!if(store, (outs RC:$dst), (outs)),
                        (ins RC:$src, i16imm_op:$imm), mnem # "\t#$imm", [],
                        4>;
      let mayLoad = 1 in
      def m : M6809Inst<!if(store, (outs RC:$dst), (outs)),
                        (ins RC:$src, memidx:$addr), mnem # "\t$addr", [], 3>;
    }
  }
}
defm SUBW : ALU16<"subw", WREG>;
defm ADDW : ALU16<"addw", WREG>;
defm SBCD : ALU16<"sbcd", ACC16, 1>;
defm ADCD : ALU16<"adcd", ACC16, 1>;
defm ANDD : ALU16<"andd", ACC16>;
defm ORD : ALU16<"ord", ACC16>;
defm EORD : ALU16<"eord", ACC16>;
defm BITD : ALU16<"bitd", ACC16, 0, 0>;

// Q = D:W.
let Defs = [D, W, CC] in {
  def LDQi : M6809Inst<(outs), (ins i32imm_op:$imm), "ldq\t#$imm", [], 5>;
  let mayLoad = 1 in
  def LDQm : M6809Inst<(outs), (ins memidx:$addr), "ldq\t$addr", [], 3>;
}
let Uses = [D, W], Defs = [CC], mayStore = 1 in
def STQm : M6809Inst<(outs), (ins memidx:$addr), "stq\t$addr", [], 3>;

// Signed multiply and divide. MULD: Q = D * operand. DIVD: B = D / operand
// (8 bits), A = remainder. DIVQ: W = Q / operand, D = remainder.
let Uses = [D], Defs = [D, W, CC] in {
  def MULDi : M6809Inst<(outs), (ins i16imm_op:$imm), "muld\t#$imm", [], 4>;
  let mayLoad = 1 in
  def MULDm : M6809Inst<(outs), (ins memidx:$addr), "muld\t$addr", [], 3>;
}
let Uses = [D], Defs = [D, CC] in {
  def DIVDi : M6809Inst<(outs), (ins i8imm_op:$imm), "divd\t#$imm", [], 3>;
  let mayLoad = 1 in
  def DIVDm : M6809Inst<(outs), (ins memidx:$addr), "divd\t$addr", [], 3>;
}
let Uses = [D, W], Defs = [D, W, CC] in {
  def DIVQi : M6809Inst<(outs), (ins i16imm_op:$imm), "divq\t#$imm", [], 4>;
  let mayLoad = 1 in
  def DIVQm : M6809Inst<(outs), (ins memidx:$addr), "divq\t$addr", [], 3>;
}

// Register-register operations: "addr x,d" is d = d + x. The registers are
// any two of the same size that TFR can name.
class RROp<string mnem, bit carry = 0>
    : M6809Inst<(outs), (ins TFRREG:$src, TFRREG:$dst),
                mnem # "\t$src, $dst", [], 3> {
  let Defs = [CC];
  let Uses = !if(carry, [CC], []<Register>);
}
def ADDR : RROp<"addr">;
def ADCR : RROp<"adcr", 1>;
def SUBR : RROp<"subr">;
def SBCR : RROp<"sbcr", 1>;
def ANDR : RROp<"andr">;
def ORR : RROp<"orr">;
def EORR : RROp<"eorr">;
def CMPR : RROp<"cmpr">;

// Block moves of W bytes: "tfm x+,y+", "tfm x-,y-", "tfm x+,y", "tfm x,y+".
let Uses = [W], Defs = [W], mayLoad = 1, mayStore = 1 in {
  def TFMpp : M6809Inst<(outs), (ins TFRREG:$src, TFRREG:$dst),
                        "tfm\t$src+, $dst+", [], 3>;
  def TFMmm : M6809Inst<(outs), (ins TFRREG:$src, TFRREG:$dst),
                        "tfm\t$src-, $dst-", [], 3>;
  def TFMpn : M6809Inst<(outs), (ins TFRREG:$src, TFRREG:$dst),
                        "tfm\t$src+, $dst", [], 3>;
  def TFMnp : M6809Inst<(outs), (ins TFRREG:$src, TFRREG:$dst),
                        "tfm\t$src, $dst+", [], 3>;
}

// Logic on memory with an immediate: "aim #$0f,<$40" is mem &= $0f.
// TIM only sets the flags.
let Defs = [CC], mayLoad = 1 in {
  let mayStore = 1 in {
    def AIM : M6809Inst<(outs), (ins i8imm_op:$imm, memidx:$addr),
                        "aim\t#$imm, $addr", [], 3>;
    def OIM : M6809Inst<(outs), (ins i8imm_op:$imm, memidx:$addr),
                        "oim\t#$imm, $addr", [], 3>;
    def EIM : M6809Inst<(outs), (ins i8imm_op:$imm, memidx:$addr),
                        "eim\t#$imm, $addr", [], 3>;
  }
  def TIM : M6809Inst<(outs), (ins i8imm_op:$imm, memidx:$addr),
                      "tim\t#$imm, $addr", [], 3>;
}

// The mode register: emulation/native mode and FIRQ behaviour.
let hasSideEffects = 1 in {
  def LDMD : M6809Inst<(outs), (ins i8imm_op:$imm), "ldmd\t#$imm", [], 3>;
  let Defs = [CC] in
  def BITMD : M6809Inst<(outs), (ins i8imm_op:$imm), "bitmd\t#$imm", [], 3>;
}

//===----------------------------------------------------------------------===//
// Code generation
//===----------------------------------------------------------------------===//

// i16 multiply: "muld x; tfr w,d", expanded after register allocation. The
// low half of the signed 32-bit product is the product for both signs. W
// is clobbered.
let Predicates = [Has6309], Constraints = "$src = $dst", Defs = [W, CC] in {
  def MUL16i : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, i16imm_op:$imm),
                           "", [(set ACC16:$dst, (mul ACC16:$src, imm:$imm))]>;
  let mayLoad = 1 in
  def MUL16m : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, memidx:$addr),
                           "", [(set ACC16:$dst,
                                     (mul ACC16:$src, (load addr:$addr)))]>;
  let Uses = [S], Defs = [S, W, CC] in
  def MUL16rr : M6809Pseudo<(outs ACC16:$dst), (ins ACC16:$src, IDX16:$rhs),
                            "", [(set ACC16:$dst, (mul ACC16:$src, IDX16:$rhs))]>;
}

// Register-register arithmetic and logic on any 16-bit registers: "andr x,y"
// is y = y & x. The 6809 forms above want the left operand in D; these leave
// both to the allocator. Plain add keeps LEA16dr, which is shorter.
multiclass RR16<SDNode node, bit carry = 0> {
  let Predicates = [Has6309], Constraints = "$src = $dst", Defs = [CC],
      Uses = !if(carry, [CC], []<Register>), AddedComplexity = 1 in
  def "" : M6809Pseudo<(outs GR16:$dst), (ins GR16:$src, GR16:$rhs), "",
                       [(set GR16:$dst, (node GR16:$src, GR16:$rhs))]>;
}
defm ADDR16 : RR16<addc>;
defm SUBR16 : RR16<sub>;
defm ANDR16 : RR16<and>;
defm ORR16 : RR16<or>;
defm EORR16 : RR16<xor>;
defm ADCR16 : RR16<adde, 1>;
defm SBCR16 : RR16<sube, 1>;
let Predicates = [Has6309], AddedComplexity = 1 in {
  def : Pat<(subc GR16:$s, GR16:$r), (SUBR16 GR16:$s, GR16:$r)>;
  def : Pat<(M6809Subc GR16:$s, GR16:$r), (SUBR16 GR16:$s, GR16:$r)>;
}
