cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 23)

if(NOT AGBPLAY_FETCH_DEPENDENCIES)
    find_package(PkgConfig REQUIRED)
    find_package(nlohmann_json REQUIRED)
    find_package(Boost CONFIG REQUIRED)
    find_package(fmt REQUIRED)
    pkg_check_modules(libzip REQUIRED IMPORTED_TARGET libzip)
    set(AGBPLAY_ZIP PkgConfig::libzip)
    pkg_check_modules(sndfile REQUIRED IMPORTED_TARGET sndfile)
    find_package(ZLIB REQUIRED)
    set(AGBPLAY_SNDFILE PkgConfig::sndfile)
else()
    set(AGBPLAY_ZIP libzip::zip)
    set(AGBPLAY_SNDFILE SndFile::sndfile)
endif()
if(ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Core)
endif()

# configure heaer
find_package(Git REQUIRED)
execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --always --tags --dirty
    COMMAND_ERROR_IS_FATAL ANY
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(TIMESTAMP BUILD_YEAR "%Y")
configure_file(Version.hpp.in Version.hpp @ONLY)

file(GLOB_RECURSE AGBPLAY_SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
set(AGBPLAY_SOURCES_AVX2 ${AGBPLAY_SOURCES})
list(FILTER AGBPLAY_SOURCES_AVX2 INCLUDE REGEX ".*AVX2\\.cpp")

if(AGBPLAY_ENABLE_AVX2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
    set(AGBPLAY_HAVE_AVX2 ON)
else()
    list(REMOVE_ITEM AGBPLAY_SOURCES ${AGBPLAY_SOURCES_AVX2})
endif()
add_library(agbplay SHARED ${AGBPLAY_SOURCES})
if(AGBPLAY_HAVE_AVX2)
    target_compile_definitions(agbplay PRIVATE AGBPLAY_HAVE_AVX2=1)
    set_source_files_properties(${AGBPLAY_SOURCES_AVX2} PROPERTIES COMPILE_OPTIONS -mavx2)
endif()
if(ANDROID)
    target_link_libraries(agbplay PRIVATE Qt6::Core)
endif()

target_compile_options(agbplay PRIVATE -Wall -Wextra -Wconversion)




target_include_directories(agbplay PUBLIC "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(agbplay
    PUBLIC
    fmt::fmt
    nlohmann_json::nlohmann_json
    PRIVATE
    Boost::boost
    ${AGBPLAY_SNDFILE}
    ${AGBPLAY_ZIP}
    ZLIB::ZLIB
    ${AGBPLAY_ADDITIONAL_LIBS}
)
