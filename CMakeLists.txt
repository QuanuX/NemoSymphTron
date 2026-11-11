cmake_minimum_required(VERSION 3.30)

project(nemosymphtron
  VERSION 1.0.0
  DESCRIPTION "QuanuX NemoSymphTron: C++26 NVIDIA Grace-Blackwell/Vera-Rubin Hardware Acceleration, Nemotron Cognitive Control, Tri-Vector (SHV/SCV/SIV) Governance, and Beads Multi-Agent Clustering Extension for Symphony"
  LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NEMOSYMPHTRON_ENABLE_CUDA "Enable native NVIDIA CUDA/NVML/NCCL acceleration when available" ON)
option(NEMOSYMPHTRON_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

find_package(Threads REQUIRED)
if(NOT APPLE)
  find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
endif()

set(NEMOSYMPHTRON_HAS_CUDA 0)
if(NEMOSYMPHTRON_ENABLE_CUDA)
  find_package(CUDAToolkit QUIET)
  if(CUDAToolkit_FOUND)
    set(NEMOSYMPHTRON_HAS_CUDA 1)
    message(STATUS "NemoSymphTron: Native CUDAToolkit found (${CUDAToolkit_VERSION}) - enabling SM120/SM130 acceleration")
  else()
    message(STATUS "NemoSymphTron: CUDAToolkit not found on host - using deterministic C++26 IEEE-754 hardware-emulation backend")
  endif()
endif()

add_library(nemosymphtron_core STATIC
  src/transport.cpp)

target_include_directories(nemosymphtron_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

target_compile_definitions(nemosymphtron_core
  PUBLIC
    NEMOSYMPHTRON_HAS_CUDA=${NEMOSYMPHTRON_HAS_CUDA}
    NEMOSYMPHTRON_VERSION="1.0.0")

target_compile_options(nemosymphtron_core
  PUBLIC
    -std=c++2c
    -Wall
    -Wextra
    -Wpedantic
    -Werror)

target_link_libraries(nemosymphtron_core PUBLIC Threads::Threads)
if(NOT APPLE)
  target_link_libraries(nemosymphtron_core PUBLIC OpenSSL::Crypto rt)
endif()
if(NEMOSYMPHTRON_HAS_CUDA)
  target_link_libraries(nemosymphtron_core
    PUBLIC
      CUDA::cudart
      CUDA::curand
      CUDA::cusolver
      CUDA::cublas)
endif()

add_executable(nemosymphtron-engine
  src/nemosymphtron_engine.cpp)
target_link_libraries(nemosymphtron-engine PRIVATE nemosymphtron_core)

add_executable(nemosymphtron-ctl
  src/nemosymphtron_ctl.cpp)
target_link_libraries(nemosymphtron-ctl PRIVATE nemosymphtron_core)

add_executable(nemosymphtron-mcp
  src/nemosymphtron_mcp.cpp)
target_link_libraries(nemosymphtron-mcp PRIVATE nemosymphtron_core)

add_executable(symphony-fastpath-client
  src/symphony_fastpath_client.cpp)
target_link_libraries(symphony-fastpath-client PRIVATE nemosymphtron_core)

install(TARGETS
  nemosymphtron-engine
  nemosymphtron-ctl
  nemosymphtron-mcp
  symphony-fastpath-client
  RUNTIME DESTINATION bin)

if(NEMOSYMPHTRON_SANITIZERS)
  foreach(target nemosymphtron_core nemosymphtron-engine nemosymphtron-ctl nemosymphtron-mcp symphony-fastpath-client)
    target_compile_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PUBLIC -fsanitize=address,undefined)
  endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_executable(fastpath_shm_test tests/fastpath_shm_test.cpp)
  target_link_libraries(fastpath_shm_test PRIVATE nemosymphtron_core)
  add_test(NAME fastpath-shm COMMAND fastpath_shm_test)

  add_executable(siv_beads_test tests/siv_beads_test.cpp)
  target_link_libraries(siv_beads_test PRIVATE nemosymphtron_core)
  add_test(NAME siv-beads-unit COMMAND siv_beads_test)

  add_executable(engine_ipc_test tests/engine_ipc_test.cpp)
  target_link_libraries(engine_ipc_test PRIVATE nemosymphtron_core)
  add_test(NAME engine-ipc COMMAND engine_ipc_test)

  add_test(
    NAME mcp-gateway
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mcp_gateway.py"
      "$<TARGET_FILE:nemosymphtron-mcp>"
      "$<TARGET_FILE:nemosymphtron-ctl>"
      "$<TARGET_FILE:nemosymphtron-engine>")

  add_test(
    NAME multi-agent-cluster
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_multi_agent_cluster.py"
      "$<TARGET_FILE:nemosymphtron-mcp>"
      "$<TARGET_FILE:nemosymphtron-ctl>"
      "$<TARGET_FILE:nemosymphtron-engine>"
      "$<TARGET_FILE:symphony-fastpath-client>")

  add_test(
    NAME qxctl-extension
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_qxctl_extension.py"
      "$<TARGET_FILE:nemosymphtron-ctl>")
endif()
