add_llvm_component_library(LLVMM6809Info
  M6809TargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  M6809
  )
