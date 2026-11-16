
cmake_minimum_required(VERSION 3.20)

project(C4R LANGUAGES CXX)
set(C4R_COMPILER_VERSION "0.0.0-dev" CACHE STRING "C4R compiler version")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

set(LLVM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/LLVM")
set(LLVM_DIR "${LLVM_ROOT}/lib/cmake/llvm")

# External dependencies: must be found before LLVM
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)

find_package(LLVM REQUIRED CONFIG)

set(C4R_SEMANTIC_SOURCES
    source/ConstantExpression.cpp
    source/Semantic.cpp
    source/SemanticBodyChecker.cpp
    source/SemanticBorrows.cpp
    source/SemanticDeclaration.cpp
    source/SemanticExpressions.cpp
    source/SemanticStatements.cpp
)

add_executable(C4R
    source/main.cpp
    source/Compilation.cpp
    source/CompilerApplication.cpp
    source/EditorSymbols.cpp
    source/ExecutableWriter.cpp
    source/IRCalls.cpp
    source/IRConstantEmitter.cpp
    source/IRExpressions.cpp
    source/IRFunctionEmitter.cpp
    source/IRModuleEmitter.cpp
    source/IRObjects.cpp
    source/IROperatorEmitter.cpp
    source/IRStatements.cpp
    source/Lexer.cpp
    source/LLVM.cpp
    source/Parser.cpp
    source/ParserDeclaration.cpp
    source/ParserExpression.cpp
    source/ParserStatement.cpp
    ${C4R_SEMANTIC_SOURCES}
)
set_target_properties(C4R PROPERTIES OUTPUT_NAME c4rc)
target_compile_definitions(C4R PRIVATE C4R_VERSION="${C4R_COMPILER_VERSION}")

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

add_executable(ownership_semantic_test EXCLUDE_FROM_ALL
    tests/ownership_semantic.cpp
    ${C4R_SEMANTIC_SOURCES}
)
target_include_directories(ownership_semantic_test PRIVATE source ${LLVM_INCLUDE_DIRS})
target_link_libraries(ownership_semantic_test PRIVATE ${LLVM_LIBS})
