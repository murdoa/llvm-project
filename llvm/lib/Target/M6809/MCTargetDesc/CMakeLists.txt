add_llvm_component_library(LLVMM6809Desc
  M6809InstPrinter.cpp
  M6809MCAsmInfo.cpp
  M6809MCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  Support
  M6809Info

  ADD_TO_COMPONENT
  M6809
  )
