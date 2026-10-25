add_llvm_component_group(M6800)

set(LLVM_TARGET_DEFINITIONS M6800.td)

tablegen(LLVM M6800GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM M6800GenCallingConv.inc -gen-callingconv)
tablegen(LLVM M6800GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM M6800GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM M6800GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM M6800GenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM M6800GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(M6800CommonTableGen)

add_llvm_target(M6800CodeGen
  M6800AsmPrinter.cpp
  M6800FrameAccess.cpp
  M6800FrameLowering.cpp
  M6800InstrInfo.cpp
  M6800ISelDAGToDAG.cpp
  M6800ISelLowering.cpp
  M6800MachineFunctionInfo.cpp
  M6800MCInstLower.cpp
  M6800RegisterInfo.cpp
  M6800SelectionDAGInfo.cpp
  M6800PostRACleanup.cpp
  M6800SplitDOperands.cpp
  M6800Subtarget.cpp
  M6800TargetMachine.cpp

  LINK_COMPONENTS
  M6800Desc
  M6800Info
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
  M6800
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
