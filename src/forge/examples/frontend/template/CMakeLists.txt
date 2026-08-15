# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)
project(MyForgeFrontend LANGUAGES CXX)
find_package(Forge 2.0 CONFIG REQUIRED)
add_executable(my-frontend main.cpp)
target_link_libraries(my-frontend PRIVATE Forge::forge)
target_compile_features(my-frontend PRIVATE cxx_std_20)
