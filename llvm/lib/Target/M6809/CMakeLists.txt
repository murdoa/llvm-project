add_llvm_component_group(M6809)

set(LLVM_TARGET_DEFINITIONS M6809.td)

tablegen(LLVM M6809GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM M6809GenCallingConv.inc -gen-callingconv)
tablegen(LLVM M6809GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM M6809GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM M6809GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM M6809GenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM M6809GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(M6809CommonTableGen)

add_llvm_target(M6809CodeGen
  M6809AsmPrinter.cpp
  M6809DirectPage.cpp
  M6809FrameLowering.cpp
  M6809InstrInfo.cpp
  M6809ISelDAGToDAG.cpp
  M6809ISelLowering.cpp
  M6809MachineFunctionInfo.cpp
  M6809MCInstLower.cpp
  M6809RegisterInfo.cpp
  M6809SelectionDAGInfo.cpp
  M6809PostRACleanup.cpp
  M6809TailCalls.cpp
  M6809SplitDOperands.cpp
  M6809Subtarget.cpp
  M6809TargetMachine.cpp
  M6809TargetTransformInfo.cpp

  LINK_COMPONENTS
  M6809Desc
  M6809Info
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  M6809
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
