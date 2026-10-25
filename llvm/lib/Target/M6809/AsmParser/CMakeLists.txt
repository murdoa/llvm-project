add_llvm_component_library(LLVMM6809AsmParser
  M6809AsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  M6809Desc
  M6809Info
  Support

  ADD_TO_COMPONENT
  M6809
  )
