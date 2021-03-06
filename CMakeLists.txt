project(MotionTracker CXX C)
cmake_minimum_required(VERSION 2.6)
cmake_policy(VERSION 2.6)
set(PROJECT_VERSION "1.0")

option(BUILD_EXAMPLES "Build the example programs." ON)
option(BUILD_DEPRECATED "Build deprecated example programs." OFF)
option(BUILD_PLUGIN "Build the Blender plugin and Python module." ON)
option(BUILD_APIDOC "Build the API documentation with Doxygen." ON)
option(BUILD_SHARED_LIBS "Build shared/dynamic library instead of a static one." OFF) # Default to static

# Avoid source tree pollution
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not permitted. Make a separate folder for building:\nmkdir build; cd build; cmake ..\nBefore that, remove the files already created:\nrm -rf CMakeCache.txt CMakeFiles")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

# Add a sensible build type default and warning because empty means no optimization and no debug info.
if(NOT CMAKE_BUILD_TYPE)
	message("WARNING: CMAKE_BUILD_TYPE is not defined!\n         Defaulting to CMAKE_BUILD_TYPE=RelWithDebInfo. Use ccmake to set a proper value.")
	SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

# Set default compile flags for GCC
if(CMAKE_COMPILER_IS_GNUCXX)
	message(STATUS "GCC detected, adding compile flags")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wall -Wextra -Wno-missing-field-initializers")
	# -pthread is unrecognized on Windows and ansi standard causes errors
	if(NOT WIN32)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++98 -pthread")
	endif(NOT WIN32)
endif()


# Doxygen API documentation
if(BUILD_APIDOC)
	find_package(Doxygen)
	if(DOXYGEN_FOUND)
		configure_file(${CMAKE_SOURCE_DIR}/Doxyfile.in ${CMAKE_BINARY_DIR}/Doxyfile @ONLY)
		add_custom_command(
			OUTPUT ${CMAKE_BINARY_DIR}/doxygen
			COMMAND ${DOXYGEN_EXECUTABLE}
			ARGS ${CMAKE_BINARY_DIR}/Doxyfile
			DEPENDS ${CMAKE_SOURCE_DIR}/include/*.hh ${CMAKE_SOURCE_DIR}/examples/*.cc ${CMAKE_SOURCE_DIR}/Doxyfile.in
			COMMENT "Generating API documentation with Doxygen" VERBATIM
		)
		add_custom_target(doc ALL
			DEPENDS ${CMAKE_BINARY_DIR}/doxygen)
	else()
		message(WARNING "Doxygen not found, disabling API documentation generation.")
		set(BUILD_APIDOC OFF)
	endif()
endif()


# Libraries

# Python
if(BUILD_PLUGIN)
	find_package(PythonLibs REQUIRED)
	include_directories(${PYTHON_INCLUDE_DIRS})
	list(APPEND LIBS ${PYTHON_LIBRARIES})
	list(APPEND Boosts "python")
endif()

# Boost
list(APPEND Boosts "thread")
find_package(Boost 1.36 REQUIRED COMPONENTS ${Boosts})
include_directories(${Boost_INCLUDE_DIRS})
list(APPEND LIBS ${Boost_LIBRARIES})

# Find all the libs that don't require extra parameters
foreach(lib OpenCV)
	find_package(${lib} REQUIRED)
	add_definitions(${${lib}_DEFINITIONS})
	include_directories(${${lib}_INCLUDE_DIRS})
	list(APPEND LIBS ${${lib}_LIBRARIES})
endforeach(lib)
list(APPEND LIBS ${OpenCV_LIBS})

# On POSIX systems librt is needed for high resolution timer
if(UNIX)
	list(APPEND LIBS "rt")
endif(UNIX)

# We want the binaries to be easily accessible
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})

set(INCDIR "${CMAKE_SOURCE_DIR}/include")

add_subdirectory(include)
add_subdirectory(src)

list(APPEND LIBS MotionTracker)

if(BUILD_EXAMPLES)
	add_subdirectory(examples)
endif(BUILD_EXAMPLES)
if(BUILD_PLUGIN)
	add_subdirectory(blender)
endif(BUILD_PLUGIN)
