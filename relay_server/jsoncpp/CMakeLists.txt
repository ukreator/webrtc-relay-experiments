cmake_minimum_required(VERSION 2.8)
project(jsoncpp)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

file(GLOB JSON_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    include/json/*.h
    src/json_reader.cpp
    src/json_value.cpp
    src/json_writer.cpp
)

add_library(jsoncpp STATIC ${JSON_SOURCES})
set_target_properties(jsoncpp PROPERTIES LINKER_LANGUAGE CXX)
