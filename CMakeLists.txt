cmake_minimum_required(VERSION 3.8)
project(BrightFuture)

set(CMAKE_CXX_STANDARD 14)

find_package(Threads REQUIRED)
find_package(Doxygen)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCE_FILES test/main.cpp BrightFuture.hh test/catch.hpp test/LocalTest.cc)
add_executable(BrightFuture ${SOURCE_FILES})
target_link_libraries(BrightFuture Threads::Threads)

# documentations
if (DOXYGEN_FOUND)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
	add_custom_target(doc
		${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Generating API documentation with Doxygen" VERBATIM
	)
endif()