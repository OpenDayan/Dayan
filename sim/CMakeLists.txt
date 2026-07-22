cmake_minimum_required(VERSION 3.15)
project(npu_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(verilator HINTS $ENV{VERILATOR_ROOT} ${VERILATOR_ROOT})
if(NOT verilator_FOUND)
    message(
        FATAL_ERROR
        "Verilator was not found. Either install it, or set the VERILATOR_ROOT environment variable"
    )
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
pkg_check_modules(ZSTD REQUIRED libzstd)

find_package(ZLIB REQUIRED)

set(TOP_NAME "TestRvtsoc" CACHE STRING "Top module")
message(STATUS "TOP_NAME: ${TOP_NAME}")
set(RTL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../generated/${TOP_NAME})
set(VERILOG_FILELIST ${RTL_DIR}/filelist.f)
set(INCLUDELIST ${RTL_DIR}/includelist.f)
# reconfigure when filelist.f changes
configure_file(
    ${VERILOG_FILELIST}
    "${CMAKE_CURRENT_BINARY_DIR}/filelist_${TOP_NAME}_copy.f"
    COPYONLY
)
configure_file(
    ${INCLUDELIST} 
    "${CMAKE_CURRENT_BINARY_DIR}/includelist_${TOP_NAME}_copy.f"
    COPYONLY
)
file(STRINGS ${INCLUDELIST} INCLUDELIST_STR)
# set(MANAGED_FILELIST "${CMAKE_CURRENT_BINARY_DIR}/filelist_${TOP_NAME}_managed.f")
# message(STATUS "Reading filelist from: ${VERILOG_FILELIST}")
# file(STRINGS ${VERILOG_FILELIST} FILELIST_SOURCES)
# list(TRANSFORM FILELIST_SOURCES PREPEND ${RTL_DIR}/)
# list(JOIN FILELIST_SOURCES "\n" FILELIST_STRING)
# file(WRITE ${MANAGED_FILELIST} "${FILELIST_STRING}\n")

set(V_SOURCES ${RTL_DIR}/${TOP_NAME}.sv)

set(C_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB_RECURSE C_SOURCES ${C_SRC_DIR}/*.cpp ${C_SRC_DIR}/*.h)

# cpp
set(DRAMSIM3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../deps/rvt/csrc/DRAMsim3)
add_subdirectory(${DRAMSIM3_DIR} ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/dramsim3.dir EXCLUDE_FROM_ALL)

add_executable(${TOP_NAME} ${C_SOURCES})
target_include_directories(${TOP_NAME} PRIVATE
    ${INCLUDELIST_STR}
    ${RTL_DIR} ${C_SRC_DIR}
    ${DRAMSIM3_DIR}/ext/headers
    ${FUSE3_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dotenv-cpp/include
)
target_link_libraries(${TOP_NAME} PRIVATE dramsim3 ${FUSE3_LIBRARIES} ${ZSTD_LIBRARIES} ${ZLIB_LIBRARIES})
# target_link_options(${TOP_NAME} PRIVATE -static)

# verilator
set(V_ARGS -f ${VERILOG_FILELIST} --timescale 1ns/1ps -Wno-fatal --max-num-width 1048576 --timing -O3 --x-assign fast --x-initial fast --output-split 10000 --output-split-cfuncs 100 -CFLAGS -O3 -CFLAGS "-march=native"
-CFLAGS -DVL_USER_FATAL -CFLAGS -DVL_USER_STOP)

# threads for simulation
set(THREADS 1 CACHE STRING "Number of threads to use for simulation (0 to use all available cores)")
cmake_host_system_information(RESULT PHYSICAL_CORES QUERY NUMBER_OF_PHYSICAL_CORES)
cmake_host_system_information(RESULT LOGICAL_CORES QUERY NUMBER_OF_LOGICAL_CORES)
if (${THREADS} STREQUAL "0")
    set(THREADS ${PHYSICAL_CORES})
endif()
message(STATUS "THREADS: ${THREADS} (Physical cores: ${PHYSICAL_CORES}, Logical cores: ${LOGICAL_CORES})")

# TRACE
option(TRACE_FST "Enable FST tracing" ON)
option(TRACE_VCD "Enable VCD tracing" OFF)
if (TRACE_FST AND TRACE_VCD)
    message(FATAL_ERROR "TRACE_FST and TRACE_VCD cannot both be enabled")
endif()
message(STATUS "FST: ${TRACE_FST}")
message(STATUS "VCD: ${TRACE_VCD}")

set(VERILATE_PREFIX V${TOP_NAME})
set(VDIR
    "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${TOP_NAME}.dir/${VERILATE_PREFIX}.dir"
)
set(VARGS_FILE "${VDIR}/verilator_args.txt")
# force re-generation
file(WRITE ${VARGS_FILE} "")

if (TRACE_FST)
    verilate(${TOP_NAME}
        SOURCES ${V_SOURCES}
        TOP_MODULE ${TOP_NAME}
        PREFIX ${VERILATE_PREFIX}
        TRACE_FST
        INCLUDE_DIRS ${RTL_DIR}
        THREADS ${THREADS}
        VERILATOR_ARGS ${V_ARGS}
    )
elseif (TRACE_VCD)
    verilate(${TOP_NAME}
        SOURCES ${V_SOURCES}
        TOP_MODULE ${TOP_NAME}
        PREFIX ${VERILATE_PREFIX}
        TRACE_VCD
        INCLUDE_DIRS ${RTL_DIR}
        THREADS ${THREADS}
        VERILATOR_ARGS ${V_ARGS}
    )
else()
    verilate(${TOP_NAME}
        SOURCES ${V_SOURCES}
        TOP_MODULE ${TOP_NAME}
        PREFIX ${VERILATE_PREFIX}
        INCLUDE_DIRS ${RTL_DIR}
        THREADS ${THREADS}
        VERILATOR_ARGS ${V_ARGS}
    )
endif()
