add_llvm_component_library(LLVMM6800Info
  M6800TargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  M6800
  )
