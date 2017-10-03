cmake_minimum_required(VERSION 3.8)
project(BrightFuture)

set(CMAKE_CXX_STANDARD 14)

find_package(Threads REQUIRED)

set(SOURCE_FILES main.cpp Async.hh catch.hpp LocalTest.cc)
add_executable(BrightFuture ${SOURCE_FILES})
target_link_libraries(BrightFuture Threads::Threads)