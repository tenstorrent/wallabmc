# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(atlantis-bmc)

zephyr_linker_sources(SECTIONS sections-rom.ld)

target_sources(app PRIVATE
	src/dhcp.c
	src/jtag.c
	src/main.c
	src/power.c
)

target_sources_ifdef(CONFIG_REDFISH app PRIVATE
	src/redfish.c
)

if(CONFIG_REDFISH_CRYPTO)
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
endif()
