/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef MTL_RHI_BUFFER_H
#define MTL_RHI_BUFFER_H

/**
 * Metal Buffer Implementation
 *
 * Metal API mapping:
 *   device.makeBuffer(length:options:)     ->  MTLBuffer constructor
 *   buffer.contents()                      ->  Map() / MapAll()
 *   memcpy to buffer.contents()            ->  Upload()
 *
 * Apple Silicon unified memory:
 *   - All buffers use MTLResourceStorageModeShared
 *   - CPU and GPU share the same memory, no explicit sync needed
 *   - buffer.contents() returns a direct pointer (always mapped)
 *   - Map/Unmap are essentially no-ops, just track mapping state
 */

#include "Rendering/RHI/RHIBuffer.h"

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace RHI {

class MTLDevice;

class MTLBuffer : public IRHIBuffer {
public:
	MTLBuffer(MTLDevice* device, BufferType type, BufferUsage usage, size_t size, const void* initialData);
	~MTLBuffer() override;

	// Prevent copying
	MTLBuffer(const MTLBuffer&) = delete;
	MTLBuffer& operator=(const MTLBuffer&) = delete;

	// --- Binding ---
	// Note: Metal doesn't have global buffer binding state like OpenGL.
	// Buffers are set directly on command encoder. These methods track
	// the "current" buffer for compatibility with RHI interface.
	void Bind() override;
	void Unbind() override;
	void BindRange(uint32_t index, size_t offset, size_t size) override;

	// --- Data transfer ---
	void* Map(size_t offset, size_t size, bool readOnly) override;
	void* MapAll(bool readOnly) override;
	void Unmap() override;
	void Upload(const void* data, size_t offset, size_t size) override;
	void* MapWithFlags(size_t offset, size_t size, MapFlags flags) override;
	void FlushMappedRange(size_t offset, size_t size) override;

	// --- Resize / Invalidate ---
	void Resize(size_t newSize) override;
	void Invalidate() override;

	// --- Queries ---
	size_t GetSize() const override { return bufferSize; }
	BufferType GetType() const override { return bufType; }
	BufferUsage GetUsage() const override { return bufUsage; }
	bool IsMapped() const override { return mapped; }
	uint32_t GetNativeHandle() const override { return 0; }  // Metal uses object pointers

#ifdef __OBJC__
	// --- Metal-specific accessors ---
	id<MTLBuffer> GetMTLBuffer() const { return mtlBuffer; }
#endif

private:
#ifdef __OBJC__
	id<MTLBuffer> mtlBuffer = nil;
#else
	void* mtlBuffer = nullptr;
#endif

	MTLDevice*  device;
	BufferType  bufType;
	BufferUsage bufUsage;
	size_t      bufferSize;
	bool        mapped = false;
	size_t      mappedOffset = 0;
	size_t      mappedSize = 0;
};

} // namespace RHI

#endif // MTL_RHI_BUFFER_H
