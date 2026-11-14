cmake_minimum_required(VERSION 3.20)

include(cmake/Version.cmake)

project(Emux
    VERSION ${EMUX_COMPILER_VERSION_NUMERIC}
    LANGUAGES CXX
)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${EMUX_VERSION_FILE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.hpp.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RuntimeSource.hpp.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/emux_rt.c"
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

# O runtime numerico vive como texto C dentro do emux e e compilado com o gcc
# do utilizador na altura do link.
file(MAKE_DIRECTORY "${EMUX_GENERATED_INCLUDE_DIR}/Emux/Generator")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/runtime/emux_rt.c" EMUX_RUNTIME_SOURCE)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RuntimeSource.hpp.in"
    "${EMUX_GENERATED_INCLUDE_DIR}/Emux/Generator/RuntimeSource.hpp"
    @ONLY
)

add_executable(emux ${EMUX_SOURCES})

target_compile_options(emux PRIVATE $<$<CONFIG:Debug>:-g;-O0>)

# A STD acompanha o executavel: cada build copia std/ para <dir do
# emux.exe>/std, o mesmo layout da distribuicao. O emux resolve os
# @include a partir desse diretorio e dos caminhos do projeto.
add_custom_target(emux-std ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/std"
        "$<TARGET_FILE_DIR:emux>/std"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/std"
        "$<TARGET_FILE_DIR:emux>/stl"
    COMMENT "Copying STD next to emux.exe"
    VERBATIM
)
add_dependencies(emux-std emux)

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
