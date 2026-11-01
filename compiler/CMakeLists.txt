
cmake_minimum_required(VERSION 3.20)

project(C4R LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

set(LLVM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/LLVM")
set(LLVM_DIR "${LLVM_ROOT}/lib/cmake/llvm")

# External dependencies: must be found before LLVM
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)

find_package(LLVM REQUIRED CONFIG)

add_executable(C4R
    source/main.cpp
    source/LLVM.cpp
)

target_include_directories(C4R PRIVATE
    source
)

target_include_directories(C4R SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
)

separate_arguments(
    LLVM_DEFINITIONS_LIST
    NATIVE_COMMAND
    "${LLVM_DEFINITIONS}"
)

add_definitions(${LLVM_DEFINITIONS_LIST})

if(MSVC)
    target_compile_options(C4R PRIVATE
        /W4
        /external:W0
        /external:anglebrackets
    )
endif()

llvm_map_components_to_libnames(LLVM_LIBS
    core
    support
    target
    targetparser
    mc
    codegen
    asmprinter
    x86codegen
    x86desc
    x86info
)

target_link_libraries(C4R PRIVATE
    ntdll
    ${LLVM_LIBS}
)
