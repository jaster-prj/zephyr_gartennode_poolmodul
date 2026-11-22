cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

macro(app_set_runner_args)
  board_runner_args(canopen "--node-id=${CONFIG_CANOPEN_NODE_ID}")
endmacro()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gartennode_Poolmodul)
FILE(GLOB app_sources src/*.c)
if(NOT CONFIG_BOARD_NATIVE_SIM)
  list(FILTER app_sources EXCLUDE REGEX "/native_sim_ds18b20\\.c$")
endif()
target_sources(app PRIVATE ${app_sources} objdict/CO_OD.c)
zephyr_include_directories(objdict src)
