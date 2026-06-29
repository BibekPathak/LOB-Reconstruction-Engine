cmake_minimum_required(VERSION 3.20)
project(lob LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── Dependencies ─────────────────────────────────────────────────────────────

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(USE_ARROW "Build with Apache Arrow support (Parquet output)" OFF)

if(BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

if(BUILD_BENCHMARKS)
  FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googlebenchmark)
endif()

# ── Core library ─────────────────────────────────────────────────────────────

file(GLOB_RECURSE LOB_SRC src/*.cpp)
file(GLOB_RECURSE LOB_INCLUDE include/*.hpp)

add_library(lob_core ${LOB_SRC} ${LOB_INCLUDE})
target_include_directories(lob_core PUBLIC include)
target_link_libraries(lob_core PUBLIC nlohmann_json::nlohmann_json)

# ── LightGBM predictor (optional) ─────────────────────────────────────────────

option(ENABLE_PREDICTOR "Build ML predictor with LightGBM inference" ON)

if(ENABLE_PREDICTOR)
  # Clone LightGBM headers only (shallow, no checkout of build files)
  FetchContent_Declare(lightgbm_src
    GIT_REPOSITORY https://github.com/microsoft/LightGBM.git
    GIT_TAG v4.6.0
    GIT_SHALLOW TRUE
  )
  set(FETCHCONTENT_QUIET OFF)
  FetchContent_GetProperties(lightgbm_src)
  if(NOT lightgbm_src_POPULATED)
    FetchContent_Populate(lightgbm_src)
  endif()
  target_include_directories(lob_core PUBLIC "${lightgbm_src_SOURCE_DIR}/include")

  # Find pre-built lib_lightgbm.so
  set(_lgbm_python "$ENV{HOME}/.pyenv/versions/3.12.7/lib/python3.12/site-packages/lightgbm/lib")
  find_library(LIGHTGBM_LIB NAMES _lightgbm lightgbm_lightgbm
    PATHS ${_lgbm_python} /usr/local/lib /usr/lib
    NO_DEFAULT_PATH
  )
  if(LIGHTGBM_LIB)
    add_library(lightgbm SHARED IMPORTED)
    set_target_properties(lightgbm PROPERTIES IMPORTED_LOCATION ${LIGHTGBM_LIB})
    target_link_libraries(lob_core PUBLIC lightgbm)
    target_compile_definitions(lob_core PUBLIC LOB_ENABLE_PREDICTOR)
    message(STATUS "LightGBM predictor enabled: ${LIGHTGBM_LIB}")
  else()
    message(WARNING "liblightgbm.so not found — predictor disabled")
  endif()
else()
  message(STATUS "LightGBM predictor disabled")
endif()

if(USE_ARROW)
  find_package(Arrow REQUIRED)
  find_package(Parquet REQUIRED)
  target_link_libraries(lob_core PUBLIC Arrow::arrow_shared Parquet::parquet_shared)
  target_compile_definitions(lob_core PUBLIC LOB_USE_ARROW)
endif()

# ── Tests ────────────────────────────────────────────────────────────────────

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ── Tools ────────────────────────────────────────────────────────────────────

add_executable(itch_replay src/itch_replay_main.cpp)
target_link_libraries(itch_replay PRIVATE lob_core)

# ── Benchmarks ───────────────────────────────────────────────────────────────

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
