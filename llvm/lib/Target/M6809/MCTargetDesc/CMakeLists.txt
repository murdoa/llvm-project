add_llvm_component_library(LLVMM6809Desc
  M6809AsmBackend.cpp
  M6809ELFObjectWriter.cpp
  M6809InstPrinter.cpp
  M6809MCAsmInfo.cpp
  M6809MCCodeEmitter.cpp
  M6809MCTargetDesc.cpp
  M6809OpcodeTable.cpp

  LINK_COMPONENTS
  BinaryFormat
  MC
  Support
  M6809Info

  ADD_TO_COMPONENT
  M6809
  )
