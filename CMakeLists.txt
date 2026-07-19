cmake_minimum_required(VERSION 3.24)
project(clap-validator-plugin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CVP_CLAP_TAG "1.2.10" CACHE STRING "free-audio/clap git tag to build against")
option(CVP_COPY_AFTER_BUILD "Copy the .clap to the user plugin folder after build" OFF)
option(CVP_THREAD_CHECKS "Enable [main-thread]/[audio-thread] runtime assertions" ON)

include(FetchContent)
FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        ${CVP_CLAP_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(clap)

add_library(clap-validator-plugins MODULE
    src/entry.cpp)

target_link_libraries(clap-validator-plugins PRIVATE clap)
target_include_directories(clap-validator-plugins PRIVATE src)
target_compile_definitions(clap-validator-plugins PRIVATE
    $<$<BOOL:${CVP_THREAD_CHECKS}>:CVP_THREAD_CHECKS=1>)

set_target_properties(clap-validator-plugins PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(clap-validator-plugins PRIVATE /W4 /permissive-)
else()
    target_compile_options(clap-validator-plugins PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(cmake/ClapPluginBundle.cmake)
cvp_configure_clap_target(clap-validator-plugins)

include(cmake/CopyToClapFolder.cmake)
cvp_add_copy_target(clap-validator-plugins)

enable_testing()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()
