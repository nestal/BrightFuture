cmake_minimum_required(VERSION 3.8)
project(BrightFuture)

set(CMAKE_CXX_STANDARD 14)

find_package(Threads REQUIRED)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(SOURCE_FILES test/main.cpp include/BridgeFuture.hh test/catch.hpp test/LocalTest.cc)
add_executable(BrightFuture ${SOURCE_FILES})
target_link_libraries(BrightFuture Threads::Threads)
