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
	find_path(GLSLANG_INCLUDE_DIR glslang/Public/ShaderLang.h)
	if(GLSLANG_INCLUDE_DIR)
		list(APPEND RHI_SOURCES
			${CMAKE_CURRENT_SOURCE_DIR}/ShaderCompiler.cpp
			${CMAKE_CURRENT_SOURCE_DIR}/ShaderReflection.cpp
		)
		list(APPEND RHI_HEADERS
			${CMAKE_CURRENT_SOURCE_DIR}/ShaderCompiler.h
			${CMAKE_CURRENT_SOURCE_DIR}/ShaderReflection.h
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
