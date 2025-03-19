add_llvm_library(mpopt-plugin MODULE 
  mpopt-plugin.cpp 
  utils.cpp 
  ExpandPrecision.cpp 
  TransformMutations.cpp 
  StmtRecursiveVisitor.cpp
  PLUGIN_TOOL clang)

if(LLVM_ENABLE_PLUGINS AND (WIN32 OR CYGWIN))
  target_link_libraries(mpopt-plugin PRIVATE
    clangAST
    clangBasic
    clangFrontend
    LLVMSupport
    )
endif()
