/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_BUFFER_H
#define RHI_BUFFER_H

/**
 * RHI Buffer Interface
 *
 * Maps VBO operations to backend-agnostic interface:
 *   VBO::Bind() / VBO::Unbind()            ->  IRHIBuffer::Bind() / Unbind()
 *   VBO::New(size, usage, data)            ->  IRHIDevice::CreateBuffer() or Resize()
 *   VBO::MapBuffer(offset, size, access)   ->  IRHIBuffer::Map(offset, size)
 *   VBO::UnmapBuffer()                     ->  IRHIBuffer::Unmap()
 *   VBO::SetBufferSubData(off, sz, data)   ->  IRHIBuffer::Upload(data, offset, size)
 *   VBO::BindBufferRange(index, off, sz)   ->  IRHIBuffer::BindRange(index, offset, size)
 *
 * Metal uses storageModeShared with direct CPU pointers instead of
 * glMapBufferRange. The Map/Unmap interface accommodates both models.
 */

#include <cstddef>
#include <cstdint>
#include "RHITypes.h"

namespace RHI {

class IRHIBuffer {
public:
	virtual ~IRHIBuffer() = default;

	// --- Binding ---
	virtual void Bind() = 0;
	virtual void Unbind() = 0;
	virtual void BindRange(uint32_t index, size_t offset, size_t size) = 0;

	// --- Data transfer ---
	virtual void* Map(size_t offset, size_t size, bool readOnly = false) = 0;
	virtual void* MapAll(bool readOnly = false) = 0;
	virtual void Unmap() = 0;
	virtual void Upload(const void* data, size_t offset, size_t size) = 0;

	// --- Resize / Invalidate ---
	virtual void Resize(size_t newSize) = 0;
	virtual void Invalidate() = 0;

	// --- Queries ---
	virtual size_t GetSize() const = 0;
	virtual BufferType GetType() const = 0;
	virtual BufferUsage GetUsage() const = 0;
	virtual bool IsMapped() const = 0;
	virtual uint32_t GetNativeHandle() const = 0;
};

} // namespace RHI

#endif // RHI_BUFFER_H
