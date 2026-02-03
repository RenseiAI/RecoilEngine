/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef GL_RHI_BUFFER_H
#define GL_RHI_BUFFER_H

#include "Rendering/RHI/RHIBuffer.h"
#include "Rendering/GL/VBO.h"

namespace RHI {

/// Thin wrapper around VBO, delegating all operations.
class GLBuffer : public IRHIBuffer {
public:
	GLBuffer(BufferType type, BufferUsage usage, size_t size, const void* initialData);
	~GLBuffer() override;

	void Bind() override { vbo.Bind(); }
	void Unbind() override { vbo.Unbind(); }
	void BindRange(uint32_t index, size_t offset, size_t size) override;

	void* Map(size_t offset, size_t size, bool readOnly) override;
	void* MapAll(bool readOnly) override;
	void Unmap() override;
	void Upload(const void* data, size_t offset, size_t size) override;

	void Resize(size_t newSize) override;
	void Invalidate() override;

	size_t GetSize() const override { return vbo.GetSize(); }
	BufferType GetType() const override { return bufType; }
	BufferUsage GetUsage() const override { return bufUsage; }
	bool IsMapped() const override { return vbo.mapped; }
	uint32_t GetNativeHandle() const override { return vbo.GetIdRaw(); }

private:
	static GLenum ToGLTarget(BufferType type);
	static GLenum ToGLUsage(BufferUsage usage);

	VBO vbo;
	BufferType bufType;
	BufferUsage bufUsage;
};

} // namespace RHI

#endif // GL_RHI_BUFFER_H
