cmake_minimum_required(VERSION 3.14)
project(streflop-float-test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- STREFLOP mode selection ---
# Override with -DSTREFLOP_MODE=SSE|NEON|SOFT (default: auto-detect)
if(NOT DEFINED STREFLOP_MODE)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|armv8")
		set(STREFLOP_MODE "SOFT")
	else()
		set(STREFLOP_MODE "SSE")
	endif()
endif()

string(TOUPPER "${STREFLOP_MODE}" STREFLOP_MODE)

if(NOT STREFLOP_MODE MATCHES "^(SSE|NEON|SOFT|X87)$")
	message(FATAL_ERROR "Invalid STREFLOP_MODE '${STREFLOP_MODE}'. Must be SSE, NEON, SOFT, or X87.")
endif()

message(STATUS "STREFLOP mode: ${STREFLOP_MODE}")
message(STATUS "Architecture:  ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Compiler:      ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# --- Locate streflop source ---
set(STREFLOP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../rts/lib/streflop")
if(NOT EXISTS "${STREFLOP_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "Cannot find streflop at ${STREFLOP_DIR}")
endif()

# --- Build streflop library from source ---
# We add it as a subdirectory, then override the auto-detected mode.
add_subdirectory("${STREFLOP_DIR}" "${CMAKE_BINARY_DIR}/streflop")

# Remove any auto-set STREFLOP_* mode definitions from the streflop target
foreach(_prop COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
	get_target_property(_defs streflop ${_prop})
	if(_defs)
		list(FILTER _defs EXCLUDE REGEX "^STREFLOP_")
		set_target_properties(streflop PROPERTIES ${_prop} "${_defs}")
	endif()
endforeach()

# Set our desired mode on the streflop library (PUBLIC propagates to linkers)
target_compile_definitions(streflop PUBLIC "STREFLOP_${STREFLOP_MODE}")

# Suppress upstream SoftFloat operator-precedence warnings (pre-existing, harmless)
target_compile_options(streflop PRIVATE -Wno-parentheses)

# --- STREFLOP_SOFT: add SoftFloat sources (not in the base CMakeLists) ---
if(STREFLOP_MODE STREQUAL "SOFT")
	message(STATUS "Adding SoftFloat sources for SOFT mode")

	# softfloat core implementation
	target_sources(streflop PRIVATE "${STREFLOP_DIR}/softfloat/softfloat.cpp")
	set_source_files_properties("${STREFLOP_DIR}/softfloat/softfloat.cpp"
		PROPERTIES COMPILE_FLAGS "-w -O3")

	# SoftFloatWrapper: must be compiled once per precision (N_SPECIALIZED=32, 64, 96)
	# N=96 (Extended) is needed because SMath.cpp/Random.cpp reference Extended type globals.
	foreach(_n 32 64 96)
		set(_wrapper_src "${CMAKE_BINARY_DIR}/soft_wrapper_${_n}.cpp")
		file(WRITE "${_wrapper_src}"
			"#define N_SPECIALIZED ${_n}\n"
			"#include \"${STREFLOP_DIR}/SoftFloatWrapper.cpp\"\n")
		target_sources(streflop PRIVATE "${_wrapper_src}")
		set_source_files_properties("${_wrapper_src}"
			PROPERTIES COMPILE_FLAGS "-w -O3 -I${STREFLOP_DIR}")
	endforeach()
endif()

# --- Compiler flags ---
# Prevent FMA fusion — critical for cross-arch reproducibility
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	add_compile_options(-ffp-contract=off)
endif()

# --- Test binary ---
add_executable(streflop-float-test streflop_float_test.cpp)
target_link_libraries(streflop-float-test PRIVATE streflop)
target_include_directories(streflop-float-test PRIVATE "${STREFLOP_DIR}")

# Pass mode and compiler info as defines for runtime reporting
target_compile_definitions(streflop-float-test PRIVATE
	"SFTEST_MODE=\"STREFLOP_${STREFLOP_MODE}\""
	"SFTEST_COMPILER=\"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\""
)
