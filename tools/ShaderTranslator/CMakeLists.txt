# ShaderTranslator - Offline GLSL to Metal shader compiler
cmake_minimum_required(VERSION 3.16)

project(ShaderTranslator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find glslang and SPIRV-Cross from parent build
# These should already be built by the main CMake

add_executable(ShaderTranslator ShaderTranslator.cpp)

target_include_directories(ShaderTranslator PRIVATE
	${CMAKE_SOURCE_DIR}/rts/lib/glslang
	${CMAKE_SOURCE_DIR}/rts/lib/SPIRV-Cross
)

target_link_libraries(ShaderTranslator PRIVATE
	glslang
	glslang-default-resource-limits
	SPIRV
	spirv-cross-msl
	spirv-cross-core
	spirv-cross-glsl
)
