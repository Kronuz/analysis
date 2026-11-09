cmake_minimum_required(VERSION 3.14)
project(analysis LANGUAGES C CXX)

include(FetchContent)
FetchContent_Declare(unicode GIT_REPOSITORY https://github.com/Kronuz/unicode.git GIT_TAG 297413c889fd99a182f1e9b8c1bc97bb2f2ded29)
FetchContent_Declare(snowball GIT_REPOSITORY https://github.com/Kronuz/snowball.git GIT_TAG 3efcd0faa97997acf361cf8edff97cb083e14bc8)
FetchContent_Declare(fst GIT_REPOSITORY https://github.com/Kronuz/fst.git GIT_TAG 7338f564f7c1c6c67e9f0cc8040d68e466592fb3)
FetchContent_MakeAvailable(unicode snowball fst)

# Header-only: analysis.hh is the whole implementation.
add_library(analysis INTERFACE)
target_include_directories(analysis INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(analysis INTERFACE cxx_std_20)
target_link_libraries(analysis INTERFACE unicode::unicode snowball::snowball)
add_library(analysis::analysis ALIAS analysis)

# The CJK word segmenter: compiled, with ICU's dictionary built into it from data/cjdict.txt.
add_executable(analysis_cjdict_gen tools/cjdict_gen.cc)
target_link_libraries(analysis_cjdict_gen PRIVATE fst::fst)
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/cjdict.inc"
  COMMAND analysis_cjdict_gen "${CMAKE_CURRENT_SOURCE_DIR}/data/cjdict.txt" "${CMAKE_CURRENT_BINARY_DIR}/cjdict.inc"
  DEPENDS analysis_cjdict_gen "${CMAKE_CURRENT_SOURCE_DIR}/data/cjdict.txt"
  COMMENT "Building the CJK dictionary")
add_library(analysis_cjk STATIC cjk/segmenter.cc "${CMAKE_CURRENT_BINARY_DIR}/cjdict.inc")
target_include_directories(analysis_cjk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(analysis_cjk PUBLIC cxx_std_20)
target_link_libraries(analysis_cjk PRIVATE unicode::unicode fst::fst)
add_library(analysis::cjk ALIAS analysis_cjk)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(analysis_test test/test.cc)
  target_link_libraries(analysis_test PRIVATE analysis)
  add_test(NAME analysis COMMAND analysis_test "${CMAKE_CURRENT_SOURCE_DIR}/test/vectors")
  add_executable(analysis_cjk_test test/cjk_test.cc)
  target_link_libraries(analysis_cjk_test PRIVATE analysis_cjk)
  add_test(NAME cjk COMMAND analysis_cjk_test "${CMAKE_CURRENT_SOURCE_DIR}/test/vectors" "${CMAKE_CURRENT_SOURCE_DIR}/data/cjdict.txt")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_executable(analysis_bench bench/bench.cc)
  target_link_libraries(analysis_bench PRIVATE analysis)
  # Xapian's TermGenerator is the comparator. It is found (never fetched) only by this target, which
  # then also checks that both produce the same terms, wdfs and positions; the library never links it.
  option(ANALYSIS_BENCH_XAPIAN "Time Xapian::TermGenerator beside analysis in analysis_bench (needs Xapian 2.1)" OFF)
  if(ANALYSIS_BENCH_XAPIAN)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XAPIAN REQUIRED IMPORTED_TARGET xapian-core)
    target_link_libraries(analysis_bench PRIVATE PkgConfig::XAPIAN)
    target_compile_definitions(analysis_bench PRIVATE ANALYSIS_BENCH_XAPIAN=1)
  endif()
  option(ANALYSIS_FUZZ "Build the libFuzzer targets" OFF)
  if(ANALYSIS_FUZZ)
    add_executable(analysis_fuzz fuzz/fuzz_index.cc)
    target_link_libraries(analysis_fuzz PRIVATE analysis)
    target_compile_options(analysis_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_options(analysis_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
    # The segmenter's code is in the library: instrument it for coverage and the sanitizers.
    target_compile_options(analysis_cjk PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all)
    target_link_options(analysis_cjk INTERFACE -fsanitize=address,undefined)
    add_executable(analysis_cjk_fuzz fuzz/fuzz_cjk.cc)
    target_link_libraries(analysis_cjk_fuzz PRIVATE analysis_cjk)
    target_compile_options(analysis_cjk_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_options(analysis_cjk_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()
  # ICU as the oracle: tables, test vectors, corpus comparison and timing. Never linked
  # by the library.
  option(ANALYSIS_ICU_TOOLS "Build the ICU oracle tools (tools/gen_tables.cc, tools/icu_compare.cc)" OFF)
  if(ANALYSIS_ICU_TOOLS)
    find_package(ICU 78 REQUIRED COMPONENTS uc data)
    add_executable(analysis_gen_tables tools/gen_tables.cc)
    target_link_libraries(analysis_gen_tables PRIVATE ICU::uc ICU::data)
    target_compile_features(analysis_gen_tables PRIVATE cxx_std_20)
    add_executable(analysis_icu_compare tools/icu_compare.cc)
    target_link_libraries(analysis_icu_compare PRIVATE analysis_cjk ICU::uc ICU::data)
  endif()
endif()
