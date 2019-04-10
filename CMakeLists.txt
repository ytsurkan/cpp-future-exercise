cmake_minimum_required(VERSION 2.8)

set(PROJECT_NAME CppFuture)

project(${PROJECT_NAME})
set (CMAKE_CXX_STANDARD 17)

if (NOT EXISTS ${PROJECT_SOURCE_DIR}/lib/catch2)
   message(FATAL_ERROR "Put single header version of catch2 library to: ${PROJECT_SOURCE_DIR}/lib")
endif()

include_directories(lib include)

add_subdirectory(test)
