# cxxopts.cmake
add_library(nlohmann INTERFACE)

target_include_directories(nlohmann SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann")

add_library(emux::nlohmann ALIAS nlohmann)
