# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)
project(ForgeConsumer LANGUAGES CXX)
find_package(Forge 2.0 CONFIG REQUIRED)
add_executable(forge-consumer main.cpp)
target_link_libraries(forge-consumer PRIVATE Forge::forge)
target_compile_features(forge-consumer PRIVATE cxx_std_20)
