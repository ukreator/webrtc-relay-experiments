cmake_minimum_required(VERSION 2.8)

set(CMAKE_USER_MAKE_RULES_OVERRIDE
    ${CMAKE_CURRENT_SOURCE_DIR}/c_flag_overrides.cmake)
set(CMAKE_USER_MAKE_RULES_OVERRIDE_CXX
    ${CMAKE_CURRENT_SOURCE_DIR}/cxx_flag_overrides.cmake)

project(relay_server)

add_subdirectory(libsrtp)
add_subdirectory(stun)
add_subdirectory(jsoncpp)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)
set(Boost_USE_MULTITHREADED TRUE)

if(NOT MSVC)
   add_definitions(-D_WEBSOCKETPP_CPP11_STL_)
    find_package(Boost COMPONENTS system)
else()
    find_package(Boost COMPONENTS date_time system thread regex chrono)
endif()

include_directories(${Boost_INCLUDE_DIRS})
if(Boost_FOUND STREQUAL "FALSE")
    message(FATAL_ERROR "Boost not found")
endif()


include_directories(.)

include_directories(
    libsrtp/crypto/include
    libsrtp/include)

include_directories(
    stun
    stun/usages
    stun/win32)

include_directories(websocketpp jsoncpp/include)
	

file(GLOB RELAY_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    *.cpp
    *.hpp
)

add_executable(relay_server ${RELAY_SOURCES})
target_link_libraries(relay_server
    libsrtp
    libstun
    jsoncpp
    ${Boost_LIBRARIES}
    ws2_32
    mswsock
)


