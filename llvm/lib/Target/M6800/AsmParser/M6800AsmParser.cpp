//===-- M6800AsmParser.cpp - Parse M6800 assembly to MCInst ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Motorola syntax:
//
//   ldaa #10       ldx #table      immediate
//   ldaa <$40      direct          ldaa >$0040   extended (forced)
//   ldaa label     extended, or direct when the assembler can tell
//   ldaa 5,x       ldaa ,x         indexed
//   aim #$0f,$40   oim #1,3,x      HD6301 logic on memory
//   beq label      bsr sub         branches
//
// The opcode table (M6800Opcodes.def) gives the forms each mnemonic takes, so
// there is no TableGen matcher: the parser builds the MCInst directly.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/M6800MCTargetDesc.h"
#include "MCTargetDesc/M6800OpcodeTable.h"
#include "TargetInfo/M6800TargetInfo.h"
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
class M6800Operand : public MCParsedAsmOperand {
  StringRef Tok;
  SMLoc Start, End;

public:
  M6800Operand(StringRef Tok, SMLoc S) : Tok(Tok), Start(S), End(S) {}
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
  unsigned Mode = M6800Mem::Auto;
};

class M6800AsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;
  MCInst Pending;

  AsmLexer &getLexer() { return Parser.getLexer(); }
  const AsmToken &tok() { return Parser.getTok(); }

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
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
  bool parseMemory(MemOperand &M, bool AllowExtended);
  bool buildInstruction(const SmallVectorImpl<const M6800::OpcodeEntry *> &Rows,
                        int CondCode, SMLoc NameLoc, MCInst &Inst);
  bool addFixedRegisters(MCInst &Inst, unsigned Count, unsigned Reg, SMLoc Loc);
  void addMemory(MCInst &Inst, const MemOperand &M);

public:
  M6800AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                 const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
  }
};

} // namespace

static MCRegister matchRegister(StringRef Name) {
  return StringSwitch<MCRegister>(Name.lower())
      .Case("a", M6800::A)
      .Case("b", M6800::B)
      .Case("d", M6800::D)
      .Case("x", M6800::X)
      .Cases({"sp", "s"}, M6800::SP)
      .Case("pc", M6800::PC)
      .Cases({"cc", "ccr"}, M6800::CC)
      .Default(MCRegister());
}

static int matchCondition(StringRef S) {
  return StringSwitch<int>(S)
      .Case("hi", M6800CC::HI)
      .Case("ls", M6800CC::LS)
      .Cases({"hs", "cc"}, M6800CC::HS)
      .Cases({"lo", "cs"}, M6800CC::LO)
      .Case("ne", M6800CC::NE)
      .Case("eq", M6800CC::EQ)
      .Case("vc", M6800CC::VC)
      .Case("vs", M6800CC::VS)
      .Case("pl", M6800CC::PL)
      .Case("mi", M6800CC::MI)
      .Case("ge", M6800CC::GE)
      .Case("lt", M6800CC::LT)
      .Case("gt", M6800CC::GT)
      .Case("le", M6800CC::LE)
      .Default(-1);
}

ParseStatus M6800AsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
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

bool M6800AsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                   SMLoc &EndLoc) {
  if (!tryParseRegister(Reg, StartLoc, EndLoc).isSuccess())
    return Error(tok().getLoc(), "expected a register");
  return false;
}

// An expression, as an immediate when it is a constant.
bool M6800AsmParser::parseValue(MCOperand &Op) {
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

// "off,x", ",x", "<addr", ">addr" or "addr".
bool M6800AsmParser::parseMemory(MemOperand &M, bool AllowExtended) {
  auto ParseX = [&] {
    SMLoc Loc = tok().getLoc();
    if (tok().isNot(AsmToken::Identifier) ||
        matchRegister(tok().getIdentifier()) != M6800::X)
      return Error(Loc, "expected x");
    Parser.Lex();
    M.Base = M6800::X;
    return false;
  };

  if (tok().is(AsmToken::Comma)) {
    Parser.Lex();
    return ParseX();
  }
  unsigned Mode = M6800Mem::Auto;
  SMLoc Loc = tok().getLoc();
  if (tok().is(AsmToken::Less)) {
    Mode = M6800Mem::Direct;
    Parser.Lex();
  } else if (tok().is(AsmToken::Greater)) {
    Mode = M6800Mem::Extended;
    Parser.Lex();
  }
  if (parseValue(M.Off))
    return true;
  if (tok().is(AsmToken::Comma)) {
    if (Mode != M6800Mem::Auto)
      return Error(Loc, "'<' and '>' need an address, not an index");
    Parser.Lex();
    return ParseX();
  }
  if (Mode == M6800Mem::Extended && !AllowExtended)
    return Error(Loc, "instruction has no extended form");
  // Without an extended form the address must fit the direct page.
  M.Mode = AllowExtended ? Mode : unsigned(M6800Mem::Direct);
  return false;
}

void M6800AsmParser::addMemory(MCInst &Inst, const MemOperand &M) {
  Inst.addOperand(MCOperand::createReg(M.Base));
  Inst.addOperand(M.Off);
  Inst.addOperand(MCOperand::createImm(M.Mode));
}

bool M6800AsmParser::addFixedRegisters(MCInst &Inst, unsigned Count,
                                       unsigned Reg, SMLoc Loc) {
  if (!M6800::addFixedRegisters(Inst, MII, *getContext().getRegisterInfo(),
                                Count, Reg))
    return Error(Loc, "internal error: unexpected operand");
  return false;
}

bool M6800AsmParser::buildInstruction(
    const SmallVectorImpl<const M6800::OpcodeEntry *> &Rows, int CondCode,
    SMLoc NameLoc, MCInst &Inst) {
  auto Find = [&](std::initializer_list<M6800::OperandForm> Forms)
      -> const M6800::OpcodeEntry * {
    for (const M6800::OpcodeEntry *E : Rows)
      for (M6800::OperandForm F : Forms)
        if (E->Form == F)
          return E;
    return nullptr;
  };
  auto Start = [&](const M6800::OpcodeEntry &E) {
    Inst.setOpcode(E.Opcode);
    Inst.setLoc(NameLoc);
    return MII.get(E.Opcode).getNumOperands();
  };
  SMLoc Loc = tok().getLoc();

  if (const M6800::OpcodeEntry *E = Find({M6800::Inh}))
    return addFixedRegisters(Inst, Start(*E), E->Reg, NameLoc);

  // "aim #n,addr": the immediate, then a direct or indexed address.
  if (const M6800::OpcodeEntry *E = Find({M6800::ImmMem})) {
    Start(*E);
    if (tok().isNot(AsmToken::Hash))
      return Error(Loc, "expected '#'");
    Parser.Lex();
    MCOperand Imm;
    if (parseValue(Imm))
      return true;
    if (tok().isNot(AsmToken::Comma))
      return Error(tok().getLoc(), "expected ','");
    Parser.Lex();
    Inst.addOperand(Imm);
    MemOperand M;
    if (parseMemory(M, /*AllowExtended=*/false))
      return true;
    addMemory(Inst, M);
    return false;
  }

  if (tok().is(AsmToken::Hash)) {
    const M6800::OpcodeEntry *E = Find({M6800::Imm8, M6800::Imm16});
    if (!E)
      return Error(Loc, "instruction has no immediate form");
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

  if (const M6800::OpcodeEntry *E = Find({M6800::Mem, M6800::MemIX})) {
    unsigned N = Start(*E);
    if (addFixedRegisters(Inst, N - 3, E->Reg, NameLoc))
      return true;
    MemOperand M;
    if (parseMemory(M, /*AllowExtended=*/true))
      return true;
    if (M.Mode == M6800Mem::Direct && !M6800::hasDirect(*E))
      return Error(Loc, "instruction has no direct form");
    addMemory(Inst, M);
    return false;
  }

  if (const M6800::OpcodeEntry *E = Find({M6800::Rel8, M6800::Cond8})) {
    Start(*E);
    if (E->Form == M6800::Cond8)
      Inst.addOperand(MCOperand::createImm(CondCode));
    MCOperand Op;
    if (parseValue(Op))
      return true;
    Inst.addOperand(Op);
    return false;
  }

  return Error(Loc, "invalid operand for instruction");
}

bool M6800AsmParser::parseInstruction(ParseInstructionInfo &Info,
                                      StringRef Name, SMLoc NameLoc,
                                      OperandVector &Operands) {
  Operands.push_back(std::make_unique<M6800Operand>(Name, NameLoc));

  std::string Mnem = Name.lower();
  // The long forms that branches relax to are not written by hand: "jmp"
  // and "jsr" take a memory operand instead.
  auto Usable = [](const M6800::OpcodeEntry &E) {
    return E.Form != M6800::Abs16 && E.Form != M6800::LongCond;
  };
  SmallVector<const M6800::OpcodeEntry *, 4> Rows;
  for (const M6800::OpcodeEntry &E : M6800::getOpcodeTable())
    if (Mnem == E.Mnemonic && Usable(E))
      Rows.push_back(&E);

  // Conditional branches: b<cc>.
  int CondCode = -1;
  if (Rows.empty() && StringRef(Mnem).starts_with("b")) {
    CondCode = matchCondition(StringRef(Mnem).drop_front());
    if (CondCode >= 0)
      for (const M6800::OpcodeEntry &E : M6800::getOpcodeTable())
        if (E.Form == M6800::Cond8)
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

bool M6800AsmParser::matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                             OperandVector &Operands,
                                             MCStreamer &Out,
                                             uint64_t &ErrorInfo,
                                             bool MatchingInlineAsm) {
  Opcode = Pending.getOpcode();
  Out.emitInstruction(Pending, getSTI());
  return false;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeM6800AsmParser() {
  RegisterMCAsmParser<M6800AsmParser> X(getTheM6800Target());
}
