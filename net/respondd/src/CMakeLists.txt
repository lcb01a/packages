cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

project(respondd C)

find_package(JSON_C REQUIRED)

set_property(DIRECTORY PROPERTY COMPILE_DEFINITIONS _GNU_SOURCE)

add_executable(respondd respondd.c)
set_property(TARGET respondd PROPERTY COMPILE_FLAGS "-Wall -std=c99 -fno-strict-aliasing ${JSON_C_CFLAGS_OTHER}")
set_property(TARGET respondd PROPERTY LINK_FLAGS "${JSON_C_LDFLAGS_OTHER}")
set_property(TARGET respondd APPEND PROPERTY INCLUDE_DIRECTORIES ${JSON_C_INCLUDE_DIR})
target_link_libraries(respondd ${JSON_C_LIBRARIES} dl)

install(TARGETS respondd RUNTIME DESTINATION bin)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/respondd.h DESTINATION include)
