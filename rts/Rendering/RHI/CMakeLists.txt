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
if(OPENGL_FOUND OR HEADLESS_SYSTEM)
	add_subdirectory(OpenGL)
endif()

# Metal backend (macOS only)
if(APPLE)
	add_subdirectory(Metal)
endif()

# Shader compilation pipeline (macOS only, for GLSL->MSL)
if(APPLE)
	list(APPEND RHI_SOURCES
		${CMAKE_CURRENT_SOURCE_DIR}/ShaderCompiler.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/ShaderReflection.cpp
	)
	list(APPEND RHI_HEADERS
		${CMAKE_CURRENT_SOURCE_DIR}/ShaderCompiler.h
		${CMAKE_CURRENT_SOURCE_DIR}/ShaderReflection.h
	)
endif()

set(RHI_SOURCES ${RHI_SOURCES} PARENT_SCOPE)
set(RHI_HEADERS ${RHI_HEADERS} PARENT_SCOPE)
if(RHI_FRAMEWORKS)
	set(RHI_FRAMEWORKS ${RHI_FRAMEWORKS} PARENT_SCOPE)
endif()
