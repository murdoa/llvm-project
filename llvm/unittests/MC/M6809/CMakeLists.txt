include_directories(
  ${LLVM_MAIN_SRC_DIR}/lib/Target/M6809
  ${LLVM_BINARY_DIR}/lib/Target/M6809
  )

set(LLVM_LINK_COMPONENTS
  M6809
  MCDisassembler
  MC
  Support
  TargetParser
  )

add_llvm_unittest(M6809MCTests
  M6809RoundTripTest.cpp
  )
