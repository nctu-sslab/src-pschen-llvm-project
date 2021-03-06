include_directories(..)

add_custom_target(ScudoUnitTests)
set_target_properties(ScudoUnitTests PROPERTIES
  FOLDER "Compiler-RT Tests")

set(SCUDO_UNITTEST_CFLAGS
  ${COMPILER_RT_UNITTEST_CFLAGS}
  ${COMPILER_RT_GTEST_CFLAGS}
  -I${COMPILER_RT_SOURCE_DIR}/include
  -I${COMPILER_RT_SOURCE_DIR}/lib
  -I${COMPILER_RT_SOURCE_DIR}/lib/scudo/standalone
  -DGTEST_HAS_RTTI=0
  # Extra flags for the C++ tests
  -fsized-deallocation
  -Wno-mismatched-new-delete)

set(SCUDO_TEST_ARCH ${SCUDO_STANDALONE_SUPPORTED_ARCH})

# gtests requires c++
set(LINK_FLAGS ${COMPILER_RT_UNITTEST_LINK_FLAGS})
foreach(lib ${SANITIZER_TEST_CXX_LIBRARIES})
  list(APPEND LINK_FLAGS -l${lib})
endforeach()
list(APPEND LINK_FLAGS -pthread)

set(SCUDO_TEST_HEADERS)
foreach (header ${SCUDO_HEADERS})
  list(APPEND SCUDO_TEST_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/../${header})
endforeach()

macro(add_scudo_unittest testname)
  cmake_parse_arguments(TEST "" "" "SOURCES;ADDITIONAL_RTOBJECTS" ${ARGN})
  if(COMPILER_RT_HAS_SCUDO_STANDALONE)
    foreach(arch ${SCUDO_TEST_ARCH})
      # Additional runtime objects get added along RTScudoStandalone
      set(SCUDO_TEST_RTOBJECTS $<TARGET_OBJECTS:RTScudoStandalone.${arch}>)
      foreach(rtobject ${TEST_ADDITIONAL_RTOBJECTS})
        list(APPEND SCUDO_TEST_RTOBJECTS $<TARGET_OBJECTS:${rtobject}.${arch}>)
      endforeach()
      # Add the static runtime library made of all the runtime objects
      set(RUNTIME RT${testname}.${arch})
      add_library(${RUNTIME} STATIC ${SCUDO_TEST_RTOBJECTS})
      set(ScudoUnitTestsObjects)
      generate_compiler_rt_tests(ScudoUnitTestsObjects ScudoUnitTests
        "${testname}-${arch}-Test" ${arch}
        SOURCES ${TEST_SOURCES} ${COMPILER_RT_GTEST_SOURCE}
        COMPILE_DEPS ${SCUDO_TEST_HEADERS}
        DEPS gtest scudo_standalone
        RUNTIME ${RUNTIME}
        CFLAGS ${SCUDO_UNITTEST_CFLAGS}
        LINK_FLAGS ${LINK_FLAGS})
    endforeach()
  endif()
endmacro()

set(SCUDO_UNIT_TEST_SOURCES
  atomic_test.cc
  bytemap_test.cc
  checksum_test.cc
  chunk_test.cc
  combined_test.cc
  flags_test.cc
  list_test.cc
  map_test.cc
  mutex_test.cc
  primary_test.cc
  quarantine_test.cc
  release_test.cc
  report_test.cc
  secondary_test.cc
  size_class_map_test.cc
  stats_test.cc
  strings_test.cc
  tsd_test.cc
  vector_test.cc
  scudo_unit_test_main.cc)

add_scudo_unittest(ScudoUnitTest
  SOURCES ${SCUDO_UNIT_TEST_SOURCES})

set(SCUDO_C_UNIT_TEST_SOURCES
  wrappers_c_test.cc
  scudo_unit_test_main.cc)

add_scudo_unittest(ScudoCUnitTest
  SOURCES ${SCUDO_C_UNIT_TEST_SOURCES}
  ADDITIONAL_RTOBJECTS RTScudoStandaloneCWrappers)

set(SCUDO_CXX_UNIT_TEST_SOURCES
  wrappers_cpp_test.cc
  scudo_unit_test_main.cc)

add_scudo_unittest(ScudoCxxUnitTest
  SOURCES ${SCUDO_CXX_UNIT_TEST_SOURCES}
  ADDITIONAL_RTOBJECTS RTScudoStandaloneCWrappers RTScudoStandaloneCxxWrappers)
