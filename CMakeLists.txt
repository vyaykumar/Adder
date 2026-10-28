cmake_minimum_required(VERSION 3.25)
project(Adder
    VERSION 1.0.0
    DESCRIPTION "High-performance RDMA communication library: Ping-Pong, Ring All-Reduce, Straggler Mitigation, and In-Network Aggregation"
    LANGUAGES C CXX
)

# -----------------------------------------------------------------------------
# Language Standards and Hot-Path Compiler Options
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable compiler warnings; configure optimization flags per build configuration
# so Debug builds retain unoptimized -O0 -g3 for GDB inspection without being clobbered by -O3.
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O3 -g>
    $<$<CONFIG:Debug>:-O0 -g3>
)

# Check for architecture-native optimizations if supported
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
    add_compile_options($<$<NOT:$<CONFIG:Debug>>:-march=native>)
endif()

# -----------------------------------------------------------------------------
# Dependency Resolution
# -----------------------------------------------------------------------------
find_package(Threads REQUIRED)

# Priority 1.1: Multi-Name Resolution Bug Fix
# find_library with multiple names stops after locating the first match.
# Explicitly resolve both libibverbs and librdmacm so RDMA CM symbols link properly.
find_library(LIBIBVERBS_LIB NAMES ibverbs REQUIRED)
find_library(LIBRDMACM_LIB NAMES rdmacm REQUIRED)
set(RDMA_LIBRARIES ${LIBIBVERBS_LIB} ${LIBRDMACM_LIB})
find_path(IBVERBS_INCLUDE_DIR NAMES infiniband/verbs.h REQUIRED)

# Optional MPI support for comparative benchmarking
find_package(MPI COMPONENTS C CXX QUIET)
if(MPI_FOUND)
    message(STATUS "MPI found: ${MPI_CXX_COMPILER}. MPI comparative benchmarks will be built.")
else()
    message(WARNING "MPI not found. MPI baseline comparisons in allreduce will be skipped.")
endif()

# Optional libbpf and clang for eBPF target (D4)
find_library(LIBBPF_LIB NAMES bpf)
find_path(LIBBPF_INCLUDE_DIR NAMES bpf/libbpf.h bpf/bpf.h)
find_program(CLANG_BPF_COMPILER NAMES clang)
find_program(BPFTOOL NAMES bpftool)

# -----------------------------------------------------------------------------
# Include Directories & Aliasing
# -----------------------------------------------------------------------------
# Exposes both direct paths ("shared/affinity.h") and canonical alias paths ("adder/shared/affinity.h")
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IBVERBS_INCLUDE_DIR}
)

# Create an alias include directory for <adder/...> includes
set(ADDER_ALIAS_DIR "${CMAKE_CURRENT_BINARY_DIR}/include_alias/adder")
file(MAKE_DIRECTORY "${ADDER_ALIAS_DIR}")

# Create symlinks or directory mappings for clean <adder/...> include directives
foreach(subfolder shared pingpong allreduce straggler innet_agg)
    file(CREATE_LINK
        "${CMAKE_CURRENT_SOURCE_DIR}/${subfolder}"
        "${ADDER_ALIAS_DIR}/${subfolder}"
        SYMBOLIC
    )
endforeach()
include_directories("${CMAKE_CURRENT_BINARY_DIR}/include_alias")

# -----------------------------------------------------------------------------
# Shared Interface Library
# -----------------------------------------------------------------------------
add_library(adder_shared INTERFACE)
target_include_directories(adder_shared INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include_alias
    ${IBVERBS_INCLUDE_DIR}
)
target_link_libraries(adder_shared INTERFACE
    Threads::Threads
    ${RDMA_LIBRARIES}
)

# Priority 1.2: Guard C++ headers with generator expressions
# Prevents pure C targets (agg_userspace.c, agg_control.c) from failing when PCH is applied.
target_precompile_headers(adder_shared INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:
        <infiniband/verbs.h>
        <expected>
        <span>
        <chrono>
        <atomic>
        <memory>
        <vector>
        <string>
        <string_view>
    >
    $<$<COMPILE_LANGUAGE:C>:
        <infiniband/verbs.h>
    >
)

# -----------------------------------------------------------------------------
# D1: RDMA Ping-Pong Latency Tool
# -----------------------------------------------------------------------------
add_executable(pingpong
    pingpong/pingpong.cc
)
target_link_libraries(pingpong PRIVATE adder_shared)

# -----------------------------------------------------------------------------
# D2: Ring All-Reduce Core & Benchmark Driver
# -----------------------------------------------------------------------------
add_library(adder_allreduce_lib STATIC
    allreduce/verbs_qp.cc
    allreduce/ring.cc
    allreduce/tree.cc
)
target_link_libraries(adder_allreduce_lib PUBLIC adder_shared)

add_executable(allreduce_bench
    allreduce/main.cc
)
target_link_libraries(allreduce_bench PRIVATE adder_allreduce_lib)

if(MPI_FOUND)
    target_link_libraries(allreduce_bench PRIVATE MPI::MPI_CXX)
    target_compile_definitions(allreduce_bench PRIVATE HAVE_MPI=1)
endif()

# -----------------------------------------------------------------------------
# D3: Straggler-Aware Chunk Scheduling
# -----------------------------------------------------------------------------
add_library(adder_straggler_lib STATIC
    straggler/detector.cc
    straggler/scheduler.cc
    straggler/injection.cc
)
target_link_libraries(adder_straggler_lib PUBLIC adder_allreduce_lib)

add_executable(straggler_bench
    straggler/main.cc
)
target_link_libraries(straggler_bench PRIVATE adder_straggler_lib)

# -----------------------------------------------------------------------------
# D4: In-Network Aggregation Emulation (Stretch Goal)
# -----------------------------------------------------------------------------
# Socket-based ring baseline for fair comparison
add_executable(sock_ring_bench
    innet_agg/sock_ring.cc
    innet_agg/agg_test.cc
)
target_link_libraries(sock_ring_bench PRIVATE adder_shared)

# C23 Fallback Aggregator
add_executable(agg_userspace
    innet_agg/agg_userspace.c
)
set_target_properties(agg_userspace PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
)
target_link_libraries(agg_userspace PRIVATE Threads::Threads)

# eBPF / XDP Target Compilation
if(CLANG_BPF_COMPILER AND LIBBPF_LIB AND LIBBPF_INCLUDE_DIR)
    message(STATUS "Clang BPF compiler and libbpf found. Enabling eBPF XDP target.")

    # Priority 1.4: Multiarch architecture headers detection for eBPF target
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(HOST_ARCH "x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(HOST_ARCH "aarch64")
    else()
        set(HOST_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    set(BPF_EXTRA_INCLUDES "")
    if(EXISTS "/usr/include/${HOST_ARCH}-linux-gnu")
        set(BPF_EXTRA_INCLUDES "-I/usr/include/${HOST_ARCH}-linux-gnu")
    endif()

    set(BPF_C_SRC "${CMAKE_CURRENT_SOURCE_DIR}/innet_agg/xdp_agg.bpf.c")
    set(BPF_OBJ "${CMAKE_CURRENT_BINARY_DIR}/xdp_agg.bpf.o")

    add_custom_command(
        OUTPUT ${BPF_OBJ}
        COMMAND ${CLANG_BPF_COMPILER} -target bpf -O2 -std=gnu11 -g
                -I${LIBBPF_INCLUDE_DIR}
                -I${CMAKE_CURRENT_SOURCE_DIR}
                ${BPF_EXTRA_INCLUDES}
                -c ${BPF_C_SRC} -o ${BPF_OBJ}
        DEPENDS ${BPF_C_SRC}
        COMMENT "Compiling eBPF program: ${BPF_C_SRC} -> ${BPF_OBJ} (with arch ${HOST_ARCH})"
    )

    add_custom_target(ebpf_prog ALL DEPENDS ${BPF_OBJ})

    add_executable(agg_control
        innet_agg/agg_control.c
    )
    set_target_properties(agg_control PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
    target_include_directories(agg_control PRIVATE ${LIBBPF_INCLUDE_DIR})
    target_link_libraries(agg_control PRIVATE ${LIBBPF_LIB} Threads::Threads)
    add_dependencies(agg_control ebpf_prog)
else()
    message(WARNING "Clang BPF compiler or libbpf not found. Skipping xdp_agg.bpf.o and agg_control (userspace fallback remains available).")
endif()

# -----------------------------------------------------------------------------
# Target Summary
# -----------------------------------------------------------------------------
message(STATUS "Adder RDMA Configuration Complete.")
message(STATUS "  - C++ Standard: C++23")
message(STATUS "  - C Standard:   C23")
message(STATUS "  - Targets:      pingpong, allreduce_bench, straggler_bench, sock_ring_bench, agg_userspace")
