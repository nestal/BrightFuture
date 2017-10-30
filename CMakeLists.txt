cmake_minimum_required(VERSION 3.8)
project(BrightFuture)

set(CMAKE_CXX_STANDARD 14)

if (WIN32)
	set(Boost_USE_STATIC_LIBS        ON) # only find static libs
	set(Boost_USE_MULTITHREADED      ON)
	set(Boost_USE_STATIC_RUNTIME    OFF)
endif()

find_package(Threads REQUIRED)
find_package(Boost COMPONENTS system)
find_package(Doxygen)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCE_FILES
	test/main.cc
	BrightFuture.hh
	executor/QtGuiExecutor.hh
	test/catch.hpp test/LocalTest.cc
)

set(EXTRA_LIB)
if(Boost_FOUND)
	list(APPEND SOURCE_FILES
		executor/BoostAsioExecutor.hh
		executor/BoostAsioExecutor.cc
	)
	list(APPEND EXTRA_LIB Boost::disable_autolinking Boost::system)
endif()

add_executable(BrightFuture ${SOURCE_FILES})
target_link_libraries(BrightFuture Threads::Threads ${EXTRA_LIB})

# documentations
if (DOXYGEN_FOUND)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
	add_custom_target(${PROJECT_NAME}-doc
		${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Generating API documentation with Doxygen" VERBATIM
	)
endif()
