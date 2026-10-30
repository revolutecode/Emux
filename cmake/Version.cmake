# Le version.json (fonte única de verdade) antes de project().
# Define EMUX_COMPILER_VERSION, EMUX_COMPILER_VERSION_NUMERIC e EMUX_OBJECT_FORMAT.

set(EMUX_VERSION_FILE "${CMAKE_CURRENT_LIST_DIR}/../version.json")

if(NOT EXISTS "${EMUX_VERSION_FILE}")
    message(FATAL_ERROR "version.json nao encontrado em ${EMUX_VERSION_FILE}")
endif()

file(READ "${EMUX_VERSION_FILE}" EMUX_VERSION_JSON)

string(JSON EMUX_COMPILER_VERSION ERROR_VARIABLE EMUX_VERSION_ERROR GET "${EMUX_VERSION_JSON}" compiler)
if(EMUX_VERSION_ERROR)
    message(FATAL_ERROR "version.json: campo 'compiler' invalido (${EMUX_VERSION_ERROR})")
endif()

string(JSON EMUX_OBJECT_FORMAT ERROR_VARIABLE EMUX_VERSION_ERROR GET "${EMUX_VERSION_JSON}" object_format)
if(EMUX_VERSION_ERROR)
    message(FATAL_ERROR "version.json: campo 'object_format' invalido (${EMUX_VERSION_ERROR})")
endif()

if(NOT EMUX_COMPILER_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "version.json: 'compiler' nao segue SemVer: ${EMUX_COMPILER_VERSION}")
endif()

if(NOT EMUX_OBJECT_FORMAT MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "version.json: 'object_format' deve ser um inteiro >= 1")
endif()

# O CMake nao aceita prerelease/build metadata em project(VERSION ...).
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" EMUX_COMPILER_VERSION_NUMERIC "${EMUX_COMPILER_VERSION}")
