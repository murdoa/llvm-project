add_llvm_component_library(LLVMM6800AsmParser
  M6800AsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  M6800Desc
  M6800Info
  Support

  ADD_TO_COMPONENT
  M6800
  )
