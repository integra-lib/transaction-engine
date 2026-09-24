cmake_minimum_required(VERSION 3.21)

project(
  hwlib_transaction_engine
  VERSION 0.2.0
  LANGUAGES CXX)

# When this repository is built on its own (its own CI), the sibling components
# are not there yet. Fetch them at the versions this release was verified
# against; a consumer never reaches this branch, because it supplies them
# itself.
if(PROJECT_IS_TOP_LEVEL AND NOT TARGET Hwlib::crc)
  include(FetchContent)
  set(HWLIB_REMOTE
      "https://gitlab.integrasources.com/internal-projects/a000-hwlib"
      CACHE
        STRING
        "Base URL the sibling components are fetched from in standalone builds")
  FetchContent_Declare(
    hwlib_crc
    GIT_REPOSITORY ${HWLIB_REMOTE}/algorithms/crc.git
    GIT_TAG v0.2.0)
  FetchContent_Declare(
    hwlib_bit_ops
    GIT_REPOSITORY ${HWLIB_REMOTE}/utilities/bit-ops.git
    GIT_TAG v0.2.0)
  FetchContent_Declare(
    hwlib_dedup_cache
    GIT_REPOSITORY ${HWLIB_REMOTE}/data-structures/dedup-cache.git
    GIT_TAG v0.2.0)
  FetchContent_MakeAvailable(hwlib_crc hwlib_bit_ops hwlib_dedup_cache)
endif()

# Dependencies are equal components, not submodules of this one: the consumer
# adds them next to this repository. Checking them here turns a mismatched set
# into a configure error naming both versions, instead of a template error deep
# inside frame.cpp or, worse, a build that silently links a different checksum.
function(hwlib_require name minimum below)
  if(NOT TARGET Hwlib::${name})
    message(
      FATAL_ERROR
        "transaction_engine requires Hwlib::${name}. Add the ${name} component next to this one "
        "and add_subdirectory() it before this directory.")
  endif()
  get_target_property(found Hwlib::${name} HWLIB_VERSION)
  if(NOT found)
    message(
      FATAL_ERROR
        "Hwlib::${name} carries no HWLIB_VERSION property — it is too old or not an hwlib component."
    )
  endif()
  if(found VERSION_LESS minimum OR NOT found VERSION_LESS below)
    message(
      FATAL_ERROR
        "transaction_engine needs Hwlib::${name} >= ${minimum} and < ${below}, found ${found}."
    )
  endif()
endfunction()

hwlib_require(crc 0.2.0 0.3.0)
hwlib_require(bit_ops 0.2.0 0.3.0)
hwlib_require(dedup_cache 0.2.0 0.3.0)

add_library(hwlib_transaction_engine STATIC src/frame.cpp)
add_library(Hwlib::transaction_engine ALIAS hwlib_transaction_engine)

target_include_directories(hwlib_transaction_engine
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(hwlib_transaction_engine PUBLIC cxx_std_20)
target_link_libraries(hwlib_transaction_engine PUBLIC Hwlib::crc Hwlib::bit_ops
                                                      Hwlib::dedup_cache)

# Dependants read the version from this property rather than from a variable: a
# variable set here is invisible in a sibling directory, and a stale one left in
# the parent scope would be read instead — a version check that silently passes.
set_target_properties(hwlib_transaction_engine PROPERTIES HWLIB_VERSION
                                                          ${PROJECT_VERSION})

if(PROJECT_IS_TOP_LEVEL)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2)
  FetchContent_MakeAvailable(googletest)

  include(GoogleTest)
  enable_testing()

  add_executable(
    transaction-engine-test
    tests/transaction_engine_frame_test.cpp
    tests/transaction_engine_sender_test.cpp
    tests/transaction_engine_receiver_test.cpp)
  target_link_libraries(transaction-engine-test
                        PRIVATE Hwlib::transaction_engine GTest::gtest_main)
  gtest_discover_tests(transaction-engine-test)
endif()
