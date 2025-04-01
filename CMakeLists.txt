cmake_minimum_required(VERSION 3.10)

project(Discord-Bot)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(PROGRAM_NAME Discord-Bot)
set(PROGRAM_SOURCE_DIR .)
set(PROGRAM_INCLUDE_DIR .)
set(PROGRAM_COPY_FILES TOKEN)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(${PROGRAM_NAME}
    ${PROGRAM_SOURCE_DIR}/main.cpp
)

find_path(DPP_INCLUDE_DIR dpp/dpp.h)
find_path(FMT_INCLUDE_DIR fmt/format.h)

find_library(DPP_LIBRARY dpp)
find_library(FMT_LIBRARY fmt)

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${PROGRAM_INCLUDE_DIR}
    ${DPP_INCLUDE_DIR}
    ${FMT_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME}
    ${DPP_LIBRARY}
    ${FMT_LIBRARY}
)

target_compile_options(${PROJECT_NAME} PUBLIC
    -g -Wno-format -Wno-psabi
)

configure_file(${PROGRAM_COPY_FILES} ${PROGRAM_COPY_FILES} COPYONLY)