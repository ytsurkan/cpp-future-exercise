cmake_minimum_required(VERSION 2.8)

set(TEST_SOURCES
    main.cpp
    uniquefunctiontest.cpp
    futuretest.cpp)

add_executable(${PROJECT_NAME} ${TEST_SOURCES} ${HEADERS})
target_link_libraries(${PROJECT_NAME} pthread)
