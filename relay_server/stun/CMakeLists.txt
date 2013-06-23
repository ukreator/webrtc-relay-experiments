cmake_minimum_required(VERSION 2.8)
project(libstun)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/stun
	${CMAKE_CURRENT_SOURCE_DIR}/stun/win32
)

file(GLOB STUN_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    stun/*.c
    stun/*.h
	stun/usages/*.c
	stun/usages/*.h
)

add_definitions(-DHAVE_CONFIG_H -DNOMINMAX)

add_library(libstun STATIC ${STUN_SOURCES})
set_target_properties(libstun PROPERTIES LINKER_LANGUAGE CXX)