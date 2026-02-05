# Metal backend implementation for RHI (macOS only)

if(NOT APPLE)
	message(FATAL_ERROR "Metal backend is only available on macOS")
endif()

# Find required frameworks
find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

set(MTL_RHI_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/MTLDevice.mm
	${CMAKE_CURRENT_SOURCE_DIR}/MTLContext.mm
	${CMAKE_CURRENT_SOURCE_DIR}/MTLBuffer.mm
	${CMAKE_CURRENT_SOURCE_DIR}/MTLTexture.mm
	${CMAKE_CURRENT_SOURCE_DIR}/MTLShader.mm
	${CMAKE_CURRENT_SOURCE_DIR}/MTLFramebuffer.mm
	${CMAKE_CURRENT_SOURCE_DIR}/MTLPipeline.mm
)

set(MTL_RHI_HEADERS
	${CMAKE_CURRENT_SOURCE_DIR}/MTLDevice.h
	${CMAKE_CURRENT_SOURCE_DIR}/MTLContext.h
	${CMAKE_CURRENT_SOURCE_DIR}/MTLBuffer.h
	${CMAKE_CURRENT_SOURCE_DIR}/MTLTexture.h
	${CMAKE_CURRENT_SOURCE_DIR}/MTLShader.h
	${CMAKE_CURRENT_SOURCE_DIR}/MTLFramebuffer.h
	${CMAKE_CURRENT_SOURCE_DIR}/MTLPipeline.h
)

# Objective-C++ compile flags for .mm files
set_source_files_properties(${MTL_RHI_SOURCES} PROPERTIES
	COMPILE_FLAGS "-fobjc-arc"
)

# Export Metal sources separately (not mixed into RHI_SOURCES)
# so headless builds can exclude them
set(RHI_METAL_SOURCES ${MTL_RHI_SOURCES} PARENT_SCOPE)
set(RHI_METAL_HEADERS ${MTL_RHI_HEADERS} PARENT_SCOPE)

# Framework libraries to link
set(RHI_FRAMEWORKS
	${METAL_FRAMEWORK}
	${QUARTZCORE_FRAMEWORK}
	${FOUNDATION_FRAMEWORK}
	PARENT_SCOPE
)
