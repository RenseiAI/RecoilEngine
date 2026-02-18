# RHI (Rendering Hardware Interface) abstraction layer
# Header-only interface definitions + factory implementation

set(RHI_HEADERS
	${CMAKE_CURRENT_SOURCE_DIR}/RHITypes.h
	${CMAKE_CURRENT_SOURCE_DIR}/RHIDevice.h
	${CMAKE_CURRENT_SOURCE_DIR}/RHIContext.h
	${CMAKE_CURRENT_SOURCE_DIR}/RHIBuffer.h
	${CMAKE_CURRENT_SOURCE_DIR}/RHITexture.h
	${CMAKE_CURRENT_SOURCE_DIR}/RHIShader.h
	${CMAKE_CURRENT_SOURCE_DIR}/RHIFramebuffer.h
	${CMAKE_CURRENT_SOURCE_DIR}/RHIPipeline.h
	${CMAKE_CURRENT_SOURCE_DIR}/RHIFactory.h
)

set(RHI_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/RHIFactory.cpp
)

# Add subdirectories for backend implementations
# OpenGL backend is always included (headless uses stub GL functions)
add_subdirectory(OpenGL)

# Metal backend (macOS only)
if(APPLE)
	add_subdirectory(Metal)
endif()

# Shader compilation pipeline (macOS only, for GLSL->MSL)
# Requires glslang and SPIRV-Cross
if(APPLE)
	find_path(GLSLANG_INCLUDE_DIR glslang/Public/ShaderLang.h
		HINTS ${CMAKE_SOURCE_DIR}/rts/lib/glslang)
	if(GLSLANG_INCLUDE_DIR)
		# Shader compiler sources are separate from RHI_SOURCES so headless
		# builds (which don't link glslang/spirv-cross) aren't affected
		set(RHI_SHADER_COMPILER_SOURCES
			${CMAKE_CURRENT_SOURCE_DIR}/ShaderCompiler.cpp
			${CMAKE_CURRENT_SOURCE_DIR}/ShaderReflection.cpp
		)
		list(APPEND RHI_HEADERS
			${CMAKE_CURRENT_SOURCE_DIR}/ShaderCompiler.h
			${CMAKE_CURRENT_SOURCE_DIR}/ShaderReflection.h
		)
		# Shader compiler needs glslang + SPIRV-Cross includes and libraries
		set(RHI_SHADER_COMPILER_INCLUDES
			${CMAKE_SOURCE_DIR}/rts/lib/glslang
			${CMAKE_SOURCE_DIR}/rts/lib/SPIRV-Cross
		)
		set(RHI_SHADER_COMPILER_LIBS
			glslang
			glslang-default-resource-limits
			SPIRV
			spirv-cross-msl
			spirv-cross-core
			spirv-cross-glsl
		)
		message(STATUS "RHI: GLSL->MSL shader compiler enabled (glslang found)")
	else()
		message(STATUS "RHI: GLSL->MSL shader compiler disabled (glslang not found)")
	endif()
endif()

set(RHI_SOURCES ${RHI_SOURCES} PARENT_SCOPE)
set(RHI_HEADERS ${RHI_HEADERS} PARENT_SCOPE)
if(RHI_METAL_SOURCES)
	set(RHI_METAL_SOURCES ${RHI_METAL_SOURCES} PARENT_SCOPE)
endif()
if(RHI_FRAMEWORKS)
	set(RHI_FRAMEWORKS ${RHI_FRAMEWORKS} PARENT_SCOPE)
endif()
if(RHI_SHADER_COMPILER_SOURCES)
	set(RHI_SHADER_COMPILER_SOURCES ${RHI_SHADER_COMPILER_SOURCES} PARENT_SCOPE)
	set(RHI_SHADER_COMPILER_INCLUDES ${RHI_SHADER_COMPILER_INCLUDES} PARENT_SCOPE)
	set(RHI_SHADER_COMPILER_LIBS ${RHI_SHADER_COMPILER_LIBS} PARENT_SCOPE)
endif()
