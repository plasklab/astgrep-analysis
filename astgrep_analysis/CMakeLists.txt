add_library(astgrep MODULE AstgrepAnalysis.cpp)

target_compile_features(astgrep PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(astgrep PROPERTIES COMPILE_FLAGS "-fno-rtti")
