/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#import "MTLBuffer.h"
#import "MTLDevice.h"

#import <Metal/Metal.h>
#include <cstring>

#include "System/Log/ILog.h"

namespace RHI {

MTLBuffer::MTLBuffer(MTLDevice* device, BufferType type, BufferUsage usage, size_t size, const void* initialData)
	: device(device)
	, bufType(type)
	, bufUsage(usage)
	, bufferSize(size)
{
	if (!device || !device->IsValid()) {
		LOG_L(L_ERROR, "[MTLBuffer] Cannot create buffer: invalid device");
		return;
	}

	// Use shared storage mode on Apple Silicon (unified memory)
	// This allows both CPU and GPU to access the buffer directly
	MTLResourceOptions options = MTLResourceStorageModeShared;

	// Hint based on usage pattern
	switch (usage) {
		case BufferUsage::Static:
			// Data written once, consider CPU write combined for initial upload
			options |= MTLResourceCPUCacheModeWriteCombined;
			break;
		case BufferUsage::Dynamic:
		case BufferUsage::Stream:
			// Frequently updated, default cache mode for better CPU access
			options |= MTLResourceCPUCacheModeDefaultCache;
			break;
	}

	// Create the buffer
	if (initialData && size > 0) {
		mtlBuffer = [device->GetMTLDevice() newBufferWithBytes:initialData
		                                                length:size
		                                               options:options];
	} else {
		mtlBuffer = [device->GetMTLDevice() newBufferWithLength:size
		                                                options:options];
	}

	if (!mtlBuffer) {
		LOG_L(L_ERROR, "[MTLBuffer] Failed to create Metal buffer of size %zu", size);
		return;
	}

	// Set a label for debugging
	const char* typeStr = "Unknown";
	switch (type) {
		case BufferType::Vertex:     typeStr = "Vertex"; break;
		case BufferType::Index:      typeStr = "Index"; break;
		case BufferType::Uniform:    typeStr = "Uniform"; break;
		case BufferType::Storage:    typeStr = "Storage"; break;
		case BufferType::PixelPack:  typeStr = "PixelPack"; break;
		case BufferType::PixelUnpack: typeStr = "PixelUnpack"; break;
	}
	mtlBuffer.label = [NSString stringWithFormat:@"RHI %s Buffer (%zu bytes)", typeStr, size];
}

MTLBuffer::~MTLBuffer() {
	mtlBuffer = nil;
}

void MTLBuffer::Bind() {
	// Metal doesn't have global binding state.
	// Buffers are bound directly to encoder slots.
	// This is a no-op for compatibility.
}

void MTLBuffer::Unbind() {
	// No-op for Metal
}

void MTLBuffer::BindRange(uint32_t index, size_t offset, size_t size) {
	// Metal doesn't have global binding state.
	// Uniform buffers are bound via setVertexBuffer:offset:atIndex:
	// This information would be used by the context when encoding commands.
	(void)index;
	(void)offset;
	(void)size;
}

void* MTLBuffer::Map(size_t offset, size_t size, bool readOnly) {
	(void)readOnly;  // Metal shared mode allows both read and write

	if (!mtlBuffer) {
		LOG_L(L_ERROR, "[MTLBuffer] Cannot map: buffer not created");
		return nullptr;
	}

	if (offset + size > bufferSize) {
		LOG_L(L_ERROR, "[MTLBuffer] Map range out of bounds: offset=%zu, size=%zu, bufferSize=%zu",
		      offset, size, bufferSize);
		return nullptr;
	}

	mapped = true;
	mappedOffset = offset;
	mappedSize = size;

	// Return pointer to the requested offset
	return static_cast<uint8_t*>([mtlBuffer contents]) + offset;
}

void* MTLBuffer::MapAll(bool readOnly) {
	return Map(0, bufferSize, readOnly);
}

void MTLBuffer::Unmap() {
	if (!mapped) {
		return;
	}

	// On Apple Silicon with shared storage mode, no explicit flush is needed.
	// The CPU writes are automatically visible to the GPU.
	// For discrete GPUs (Intel Macs), you might need didModifyRange:
	// [mtlBuffer didModifyRange:NSMakeRange(mappedOffset, mappedSize)];

	mapped = false;
	mappedOffset = 0;
	mappedSize = 0;
}

void MTLBuffer::Upload(const void* data, size_t offset, size_t size) {
	if (!mtlBuffer) {
		LOG_L(L_ERROR, "[MTLBuffer] Cannot upload: buffer not created");
		return;
	}

	if (!data) {
		LOG_L(L_ERROR, "[MTLBuffer] Cannot upload: null data");
		return;
	}

	if (offset + size > bufferSize) {
		LOG_L(L_ERROR, "[MTLBuffer] Upload range out of bounds: offset=%zu, size=%zu, bufferSize=%zu",
		      offset, size, bufferSize);
		return;
	}

	// Direct memcpy to buffer contents (shared memory)
	uint8_t* dst = static_cast<uint8_t*>([mtlBuffer contents]) + offset;
	std::memcpy(dst, data, size);
}

void MTLBuffer::Resize(size_t newSize) {
	if (newSize == bufferSize) {
		return;
	}

	if (!device || !device->IsValid()) {
		LOG_L(L_ERROR, "[MTLBuffer] Cannot resize: invalid device");
		return;
	}

	// Create new buffer
	MTLResourceOptions options = MTLResourceStorageModeShared;
	switch (bufUsage) {
		case BufferUsage::Static:
			options |= MTLResourceCPUCacheModeWriteCombined;
			break;
		case BufferUsage::Dynamic:
		case BufferUsage::Stream:
			options |= MTLResourceCPUCacheModeDefaultCache;
			break;
	}

	id<MTLBuffer> newBuffer = [device->GetMTLDevice() newBufferWithLength:newSize
	                                                              options:options];
	if (!newBuffer) {
		LOG_L(L_ERROR, "[MTLBuffer] Failed to resize buffer to %zu bytes", newSize);
		return;
	}

	// Copy existing data if the new buffer is larger
	if (mtlBuffer && newSize > 0 && bufferSize > 0) {
		size_t copySize = std::min(bufferSize, newSize);
		std::memcpy([newBuffer contents], [mtlBuffer contents], copySize);
	}

	// Replace the buffer
	mtlBuffer = newBuffer;
	bufferSize = newSize;
	mapped = false;

	// Update label
	const char* typeStr = "Unknown";
	switch (bufType) {
		case BufferType::Vertex:     typeStr = "Vertex"; break;
		case BufferType::Index:      typeStr = "Index"; break;
		case BufferType::Uniform:    typeStr = "Uniform"; break;
		case BufferType::Storage:    typeStr = "Storage"; break;
		case BufferType::PixelPack:  typeStr = "PixelPack"; break;
		case BufferType::PixelUnpack: typeStr = "PixelUnpack"; break;
	}
	mtlBuffer.label = [NSString stringWithFormat:@"RHI %s Buffer (%zu bytes)", typeStr, newSize];
}

void MTLBuffer::Invalidate() {
	// In Metal with shared storage mode, there's no concept of buffer orphaning
	// like in OpenGL. The buffer contents are simply overwritten.
	// This is effectively a no-op, but could be used to hint that the
	// previous contents are no longer needed.
}

} // namespace RHI
