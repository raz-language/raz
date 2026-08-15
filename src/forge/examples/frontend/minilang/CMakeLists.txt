# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)
project(ForgeMiniLang LANGUAGES CXX)

find_package(Forge 2.0 CONFIG REQUIRED)

add_executable(minilang
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/codegen.cpp
)
target_include_directories(minilang PRIVATE include)
target_link_libraries(minilang PRIVATE Forge::forge)
target_compile_features(minilang PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(minilang PRIVATE /W4 /WX)
else()
    target_compile_options(minilang PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
