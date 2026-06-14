cmake_minimum_required(VERSION 3.16)
project(fermi C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(FERMI_SRCS
    src/fearena/arena.c
    src/felexer/lexer.c
    src/felexer/token.c
    src/feparser/parser.c
    src/fehir/hir.c
    src/fetc/tc.c
    src/fesema/sema.c
    src/fecodegen/fir.c
    src/fecodegen/codegen.c
    src/feopt/opt.c
    src/fellvm/llvm_emit.c
    src/main.c
)

add_executable(fermi ${FERMI_SRCS})
target_include_directories(fermi PRIVATE src)
target_compile_options(fermi PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
install(TARGETS fermi DESTINATION bin)
