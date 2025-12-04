# SPDX-License-Identifier: Apache-2.0

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(atlantis-bmc)

zephyr_linker_sources(SECTIONS sections-rom.ld)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

foreach(inc_file
  server_cert.der
  server_privkey.der
)
  generate_inc_file_for_target(app
    src/certs/${inc_file}
    ${gen_dir}/${inc_file}.inc
  )
endforeach()
