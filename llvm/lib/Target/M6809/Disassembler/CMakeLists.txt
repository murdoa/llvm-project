add_llvm_component_library(LLVMM6809Disassembler
  M6809Disassembler.cpp

  LINK_COMPONENTS
  MC
  MCDisassembler
  M6809Desc
  M6809Info
  Support

  ADD_TO_COMPONENT
  M6809
  )
