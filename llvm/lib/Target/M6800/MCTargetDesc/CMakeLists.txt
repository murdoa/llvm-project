add_llvm_component_library(LLVMM6800Desc
  M6800AsmBackend.cpp
  M6800ELFObjectWriter.cpp
  M6800InstPrinter.cpp
  M6800MCAsmInfo.cpp
  M6800MCCodeEmitter.cpp
  M6800MCTargetDesc.cpp
  M6800OpcodeTable.cpp

  LINK_COMPONENTS
  BinaryFormat
  MC
  Support
  M6800Info

  ADD_TO_COMPONENT
  M6800
  )
