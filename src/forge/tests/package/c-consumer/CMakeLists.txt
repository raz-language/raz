# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)
project(ForgeCConsumer LANGUAGES C CXX)
find_package(Forge 2.0 CONFIG REQUIRED)
add_executable(forge-c-consumer main.c)
target_link_libraries(forge-c-consumer PRIVATE Forge::forge)
set_target_properties(forge-c-consumer PROPERTIES LINKER_LANGUAGE CXX)
