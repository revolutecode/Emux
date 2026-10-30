cmake_minimum_required(VERSION 3.20)

include(cmake/Version.cmake)

project(Emux
    VERSION ${EMUX_COMPILER_VERSION_NUMERIC}
    LANGUAGES CXX
)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${EMUX_VERSION_FILE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.hpp.in"
)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


include(cmake/Dependencies.cmake)

find_package(Threads REQUIRED)

file(GLOB_RECURSE EMUX_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

set(EMUX_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${EMUX_GENERATED_INCLUDE_DIR}/Emux/Core")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.hpp.in"
    "${EMUX_GENERATED_INCLUDE_DIR}/Emux/Core/Version.hpp"
    @ONLY
)

add_executable(emux ${EMUX_SOURCES})

target_compile_options(emux PRIVATE $<$<CONFIG:Debug>:-g;-O0>)

target_include_directories(
    emux
    PRIVATE
    "${EMUX_GENERATED_INCLUDE_DIR}"
    include
)

target_link_libraries(
    emux
    PRIVATE
    emux::asmjit
    emux::cxxopts
    emux::nlohmann
    Threads::Threads
)
