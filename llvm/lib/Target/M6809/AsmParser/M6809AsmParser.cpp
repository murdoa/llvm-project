//===-- M6809AsmParser.cpp - Parse M6809 assembly to MCInst ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Motorola syntax:
//
//   lda #10        ldx #table      immediate
//   lda <$40       direct page     lda >$0040   extended (forced)
//   lda label      extended
//   lda 5,x  lda ,y+  lda ,--s  lda b,u  lda d,x   indexed
//   lda <5,x  lda >5,x  lda <<5,x                  forced offset width
//   lda label,pcr  lda 10,pc                       PC-relative
//   lda [5,x]  jmp [$fffe]                         indirect
//   tfr a,b  exg d,x  pshs a,b,x,pc  beq label
//
// The opcode table (M6809Opcodes.def) gives the forms each mnemonic takes, so
// there is no TableGen matcher: the parser builds the MCInst directly.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/M6809MCTargetDesc.h"
#include "MCTargetDesc/M6809OpcodeTable.h"
#include "TargetInfo/M6809TargetInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

namespace {

// The parser builds the MCInst itself, so the only parsed operand is the
// mnemonic token the generic code expects.
class M6809Operand : public MCParsedAsmOperand {
  StringRef Tok;
  SMLoc Start, End;

public:
  M6809Operand(StringRef Tok, SMLoc S) : Tok(Tok), Start(S), End(S) {}
  bool isToken() const override { return true; }
  bool isImm() const override { return false; }
  bool isReg() const override { return false; }
  MCRegister getReg() const override { return MCRegister(); }
  bool isMem() const override { return false; }
  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }
  void print(raw_ostream &OS, const MCAsmInfo &) const override {
    OS << "Token: " << Tok;
  }
};

struct MemOperand {
  MCRegister Base;
  MCOperand Off = MCOperand::createImm(0);
  unsigned Mode = M6809Mem::Auto;
};

class M6809AsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;
  MCInst Pending;

  AsmLexer &getLexer() { return Parser.getLexer(); }
  const AsmToken &tok() { return Parser.getTok(); }

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                     SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;
  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
  void convertToMapAndConstraints(unsigned Kind,
                                  const OperandVector &Operands) override {}

  bool parseValue(MCOperand &Op);
  bool parseMemory(MemOperand &M);
  bool parseIndexRegister(MCRegister &Reg, bool AllowPC);
  bool parseRegList(int64_t &Mask, bool UStack);
  bool buildInstruction(const SmallVectorImpl<const M6809::OpcodeEntry *> &Rows,
                        int CondCode, SMLoc NameLoc, MCInst &Inst);
  bool addFixedRegisters(MCInst &Inst, unsigned Count, unsigned Reg,
                         SMLoc Loc);

public:
  M6809AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                 const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
  }
};

} // namespace

static MCRegister matchRegister(StringRef Name) {
  return StringSwitch<MCRegister>(Name.lower())
      .Case("a", M6809::A)
      .Case("b", M6809::B)
      .Case("d", M6809::D)
      .Case("x", M6809::X)
      .Case("y", M6809::Y)
      .Case("u", M6809::U)
      .Case("s", M6809::S)
      .Case("pc", M6809::PC)
      .Cases({"cc", "ccr"}, M6809::CC)
      .Cases({"dp", "dpr"}, M6809::DP)
      .Default(MCRegister());
}

static bool is8BitRegister(MCRegister R) {
  return R == M6809::A || R == M6809::B || R == M6809::CC || R == M6809::DP;
}

static int matchCondition(StringRef S) {
  return StringSwitch<int>(S)
      .Case("hi", M6809CC::HI)
      .Case("ls", M6809CC::LS)
      .Cases({"hs", "cc"}, M6809CC::HS)
      .Cases({"lo", "cs"}, M6809CC::LO)
      .Case("ne", M6809CC::NE)
      .Case("eq", M6809CC::EQ)
      .Case("vc", M6809CC::VC)
      .Case("vs", M6809CC::VS)
      .Case("pl", M6809CC::PL)
      .Case("mi", M6809CC::MI)
      .Case("ge", M6809CC::GE)
      .Case("lt", M6809CC::LT)
      .Case("gt", M6809CC::GT)
      .Case("le", M6809CC::LE)
      .Default(-1);
}

ParseStatus M6809AsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                             SMLoc &EndLoc) {
  if (tok().isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;
  Reg = matchRegister(tok().getIdentifier());
  if (!Reg)
    return ParseStatus::NoMatch;
  StartLoc = tok().getLoc();
  EndLoc = tok().getEndLoc();
  Parser.Lex();
  return ParseStatus::Success;
}

bool M6809AsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                   SMLoc &EndLoc) {
  if (!tryParseRegister(Reg, StartLoc, EndLoc).isSuccess())
    return Error(tok().getLoc(), "expected a register");
  return false;
}

// An expression, as an immediate when it is a constant.
bool M6809AsmParser::parseValue(MCOperand &Op) {
  const MCExpr *E;
  SMLoc EndLoc;
  if (Parser.parseExpression(E, EndLoc))
    return true;
  int64_t V;
  if (E->evaluateAsAbsolute(V))
    Op = MCOperand::createImm(V);
  else
    Op = MCOperand::createExpr(E);
  return false;
}

bool M6809AsmParser::parseIndexRegister(MCRegister &Reg, bool AllowPC) {
  SMLoc Loc = tok().getLoc();
  if (tok().isNot(AsmToken::Identifier))
    return Error(Loc, "expected an index register");
  MCRegister R = matchRegister(tok().getIdentifier());
  if (!(R == M6809::X || R == M6809::Y || R == M6809::U || R == M6809::S ||
        (AllowPC && R == M6809::PC)))
    return Error(Loc, "expected x, y, u or s");
  Reg = R;
  Parser.Lex();
  return false;
}

// Everything after the mnemonic of an instruction that addresses memory.
bool M6809AsmParser::parseMemory(MemOperand &M) {
  bool Indirect = false;
  if (tok().is(AsmToken::LBrac)) {
    Indirect = true;
    Parser.Lex();
  }

  // "<<", "<" and ">" force the width of the address or offset.
  unsigned Width = 0;
  if (tok().is(AsmToken::LessLess)) {
    Width = 5;
    Parser.Lex();
  } else if (tok().is(AsmToken::Less)) {
    Width = 8;
    Parser.Lex();
  } else if (tok().is(AsmToken::Greater)) {
    Width = 16;
    Parser.Lex();
  }
  auto OffsetMode = [&] {
    return Width == 5    ? M6809Mem::Off5
           : Width == 8  ? M6809Mem::Off8
           : Width == 16 ? M6809Mem::Off16
                         : M6809Mem::Auto;
  };

  if (tok().is(AsmToken::Comma)) {
    // ,R  ,R+  ,R++  ,-R  ,--R
    Parser.Lex();
    unsigned Mode = OffsetMode();
    if (tok().is(AsmToken::Minus)) {
      Parser.Lex();
      Mode = M6809Mem::PreDec1;
      if (tok().is(AsmToken::Minus)) {
        Parser.Lex();
        Mode = M6809Mem::PreDec2;
      }
      if (parseIndexRegister(M.Base, false))
        return true;
    } else {
      if (parseIndexRegister(M.Base, true))
        return true;
      if (tok().is(AsmToken::Plus)) {
        Parser.Lex();
        Mode = M6809Mem::PostInc1;
        if (tok().is(AsmToken::Plus)) {
          Parser.Lex();
          Mode = M6809Mem::PostInc2;
        }
      }
    }
    if (Width && Mode != OffsetMode())
      return Error(tok().getLoc(), "width prefix on an auto-increment");
    M.Mode = Mode;
  } else if (tok().is(AsmToken::Identifier) &&
             getLexer().peekTok().is(AsmToken::Comma) &&
             (tok().getIdentifier().equals_insensitive("a") ||
              tok().getIdentifier().equals_insensitive("b") ||
              tok().getIdentifier().equals_insensitive("d"))) {
    // A,R  B,R  D,R
    char Acc = tolower(tok().getIdentifier()[0]);
    M.Mode = Acc == 'a'   ? M6809Mem::AccA
             : Acc == 'b' ? M6809Mem::AccB
                          : M6809Mem::AccD;
    Parser.Lex();
    Parser.Lex(); // the comma
    if (parseIndexRegister(M.Base, false))
      return true;
  } else {
    if (parseValue(M.Off))
      return true;
    if (tok().is(AsmToken::Comma)) {
      Parser.Lex();
      if (tok().is(AsmToken::Identifier) &&
          tok().getIdentifier().equals_insensitive("pcr")) {
        // label,pcr: the operand is the target address.
        Parser.Lex();
        M.Base = M6809::PC;
        M.Mode = M6809Mem::PCRel;
      } else {
        if (parseIndexRegister(M.Base, true))
          return true;
        M.Mode = OffsetMode();
      }
    } else {
      // No base register: direct or extended.
      if (Width == 5)
        return Error(tok().getLoc(), "'<<' needs an index register");
      M.Mode = Width == 8    ? M6809Mem::Direct
               : Width == 16 ? M6809Mem::Extended
                             : M6809Mem::Auto;
    }
  }

  if (Indirect) {
    if (tok().isNot(AsmToken::RBrac))
      return Error(tok().getLoc(), "expected ']'");
    Parser.Lex();
    M.Mode |= M6809Mem::Indirect;
  }
  return false;
}

// "a,b,x" or "#mask".
bool M6809AsmParser::parseRegList(int64_t &Mask, bool UStack) {
  Mask = 0;
  if (tok().is(AsmToken::Hash)) {
    Parser.Lex();
    SMLoc Loc = tok().getLoc();
    const MCExpr *E;
    if (Parser.parseExpression(E) || !E->evaluateAsAbsolute(Mask))
      return Error(Loc, "expected a constant register mask");
    return false;
  }
  while (true) {
    SMLoc Loc = tok().getLoc();
    MCRegister R;
    if (tok().is(AsmToken::Identifier))
      R = matchRegister(tok().getIdentifier());
    unsigned Bit = R ? M6809::getPushPullMask(R.id(), UStack) : 0;
    if (!Bit)
      return Error(Loc, UStack ? "cannot push or pull u here"
                               : "expected a register that can be pushed");
    Mask |= Bit;
    Parser.Lex();
    if (tok().isNot(AsmToken::Comma))
      return false;
    Parser.Lex();
  }
}

// Adds the first Count operands of the instruction, which are all
// registers: the one the mnemonic names, or the only member of the
// operand's class (D for SEX and ADDD).
bool M6809AsmParser::addFixedRegisters(MCInst &Inst, unsigned Count,
                                       unsigned Reg, SMLoc Loc) {
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());
  const MCRegisterInfo *MRI = getContext().getRegisterInfo();
  for (unsigned I = 0; I != Count; ++I) {
    const MCOperandInfo &Info = Desc.operands()[I];
    if (Info.OperandType != MCOI::OPERAND_REGISTER)
      return Error(Loc, "internal error: unexpected operand");
    unsigned R = Reg;
    if (R == M6809::NoRegister) {
      const MCRegisterClass &RC = MRI->getRegClass(Info.RegClass);
      if (RC.getNumRegs() != 1)
        return Error(Loc, "internal error: no register for operand");
      R = RC.getRegister(0);
    }
    Inst.addOperand(MCOperand::createReg(R));
  }
  return false;
}

bool M6809AsmParser::buildInstruction(
    const SmallVectorImpl<const M6809::OpcodeEntry *> &Rows, int CondCode,
    SMLoc NameLoc, MCInst &Inst) {
  auto Find = [&](std::initializer_list<M6809::OperandForm> Forms)
      -> const M6809::OpcodeEntry * {
    for (const M6809::OpcodeEntry *E : Rows)
      for (M6809::OperandForm F : Forms)
        if (E->Form == F)
          return E;
    return nullptr;
  };
  auto Start = [&](const M6809::OpcodeEntry &E) {
    Inst.setOpcode(E.Opcode);
    Inst.setLoc(NameLoc);
    return MII.get(E.Opcode).getNumOperands();
  };
  SMLoc Loc = tok().getLoc();

  if (const M6809::OpcodeEntry *E = Find({M6809::Inh}))
    return addFixedRegisters(Inst, Start(*E), E->Reg, NameLoc);

  if (tok().is(AsmToken::Hash)) {
    const M6809::OpcodeEntry *E = Find({M6809::Imm8, M6809::Imm16});
    if (!E) {
      if (Find({M6809::RegList}))
        E = Find({M6809::RegList});
      else
        return Error(Loc, "instruction has no immediate form");
    }
    if (E->Form == M6809::RegList) {
      Start(*E);
      int64_t Mask;
      if (parseRegList(Mask, E->Opcode == M6809::PSHUr ||
                                 E->Opcode == M6809::PULUr))
        return true;
      Inst.addOperand(MCOperand::createImm(Mask));
      return false;
    }
    Parser.Lex();
    unsigned N = Start(*E);
    if (addFixedRegisters(Inst, N - 1, E->Reg, NameLoc))
      return true;
    MCOperand Op;
    if (parseValue(Op))
      return true;
    Inst.addOperand(Op);
    return false;
  }

  if (const M6809::OpcodeEntry *E = Find({M6809::Mem, M6809::Idx})) {
    unsigned N = Start(*E);
    if (addFixedRegisters(Inst, N - 3, E->Reg, NameLoc))
      return true;
    MemOperand M;
    if (parseMemory(M))
      return true;
    Inst.addOperand(MCOperand::createReg(M.Base));
    Inst.addOperand(M.Off);
    Inst.addOperand(MCOperand::createImm(M.Mode));
    return false;
  }

  if (const M6809::OpcodeEntry *E = Find({M6809::Rel8, M6809::Rel16,
                                          M6809::Cond8, M6809::Cond16})) {
    Start(*E);
    if (E->Form == M6809::Cond8 || E->Form == M6809::Cond16)
      Inst.addOperand(MCOperand::createImm(CondCode));
    MCOperand Op;
    if (parseValue(Op))
      return true;
    Inst.addOperand(Op);
    return false;
  }

  if (const M6809::OpcodeEntry *E = Find({M6809::RegPair})) {
    Start(*E);
    MCRegister R1, R2;
    SMLoc S, End;
    if (parseRegister(R1, S, End))
      return true;
    if (tok().isNot(AsmToken::Comma))
      return Error(tok().getLoc(), "expected ','");
    Parser.Lex();
    SMLoc Loc2 = tok().getLoc();
    if (parseRegister(R2, S, End))
      return true;
    if (is8BitRegister(R1) != is8BitRegister(R2))
      return Error(Loc2, "registers must be the same size");
    Inst.addOperand(MCOperand::createReg(R1));
    Inst.addOperand(MCOperand::createReg(R2));
    return false;
  }

  if (const M6809::OpcodeEntry *E = Find({M6809::RegList})) {
    Start(*E);
    int64_t Mask;
    if (parseRegList(Mask,
                     E->Opcode == M6809::PSHUr || E->Opcode == M6809::PULUr))
      return true;
    Inst.addOperand(MCOperand::createImm(Mask));
    return false;
  }

  return Error(Loc, "invalid operand for instruction");
}

bool M6809AsmParser::parseInstruction(ParseInstructionInfo &Info,
                                      StringRef Name, SMLoc NameLoc,
                                      OperandVector &Operands) {
  Operands.push_back(std::make_unique<M6809Operand>(Name, NameLoc));

  std::string Mnem = Name.lower();
  Mnem = StringSwitch<std::string>(Mnem)
             .Case("lsla", "asla")
             .Case("lslb", "aslb")
             .Case("lsl", "asl")
             .Default(Mnem);

  SmallVector<const M6809::OpcodeEntry *, 4> Rows;
  for (const M6809::OpcodeEntry &E : M6809::getOpcodeTable())
    if (Mnem == E.Mnemonic)
      Rows.push_back(&E);

  // Conditional branches: b<cc> and lb<cc>.
  int CondCode = -1;
  if (Rows.empty()) {
    StringRef M = Mnem;
    StringRef Prefix = M.starts_with("lb") ? "lb" : M.starts_with("b") ? "b" : "";
    if (!Prefix.empty())
      CondCode = matchCondition(M.drop_front(Prefix.size()));
    if (CondCode >= 0)
      for (const M6809::OpcodeEntry &E : M6809::getOpcodeTable())
        if (Prefix == E.Mnemonic)
          Rows.push_back(&E);
  }
  if (Rows.empty())
    return Error(NameLoc, "unknown instruction");

  MCInst Inst;
  if (buildInstruction(Rows, CondCode, NameLoc, Inst))
    return true;
  if (tok().isNot(AsmToken::EndOfStatement))
    return Error(tok().getLoc(), "unexpected token after operand");
  Pending = Inst;
  return false;
}

bool M6809AsmParser::matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                             OperandVector &Operands,
                                             MCStreamer &Out,
                                             uint64_t &ErrorInfo,
                                             bool MatchingInlineAsm) {
  Opcode = Pending.getOpcode();
  Out.emitInstruction(Pending, getSTI());
  return false;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeM6809AsmParser() {
  RegisterMCAsmParser<M6809AsmParser> X(getTheM6809Target());
}
