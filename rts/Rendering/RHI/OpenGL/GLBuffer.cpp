/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "GLBuffer.h"

namespace RHI {

GLenum GLBuffer::ToGLTarget(BufferType type) {
	switch (type) {
		case BufferType::Vertex:      return GL_ARRAY_BUFFER;
		case BufferType::Index:       return GL_ELEMENT_ARRAY_BUFFER;
		case BufferType::Uniform:     return GL_UNIFORM_BUFFER;
		case BufferType::Storage:     return GL_SHADER_STORAGE_BUFFER;
		case BufferType::PixelPack:   return GL_PIXEL_PACK_BUFFER;
		case BufferType::PixelUnpack: return GL_PIXEL_UNPACK_BUFFER;
	}
	return GL_ARRAY_BUFFER;
}

GLenum GLBuffer::ToGLUsage(BufferUsage usage) {
	switch (usage) {
		case BufferUsage::Static:  return GL_STATIC_DRAW;
		case BufferUsage::Dynamic: return GL_DYNAMIC_DRAW;
		case BufferUsage::Stream:  return GL_STREAM_DRAW;
	}
	return GL_STREAM_DRAW;
}

GLBuffer::GLBuffer(BufferType type, BufferUsage usage, size_t size, const void* initialData)
	: vbo(ToGLTarget(type), false)
	, bufType(type)
	, bufUsage(usage)
{
	vbo.Bind();
	vbo.New(size, ToGLUsage(usage), initialData);
	vbo.Unbind();
}

GLBuffer::~GLBuffer() {
	vbo.Release();
}

void GLBuffer::BindRange(uint32_t index, size_t offset, size_t size) {
	vbo.BindBufferRange(ToGLTarget(bufType), index, static_cast<GLuint>(offset), static_cast<GLsizeiptr>(size));
}

void* GLBuffer::Map(size_t offset, size_t size, bool readOnly) {
	vbo.Bind();
	return vbo.MapBuffer(offset, size, readOnly ? GL_READ_ONLY : GL_WRITE_ONLY);
}

void* GLBuffer::MapAll(bool readOnly) {
	vbo.Bind();
	return vbo.MapBuffer(readOnly ? GL_READ_ONLY : GL_WRITE_ONLY);
}

void GLBuffer::Unmap() {
	vbo.UnmapBuffer();
}

void* GLBuffer::MapWithFlags(size_t offset, size_t size, MapFlags flags) {
	GLbitfield glFlags = 0;
	if (HasFlag(flags, MapFlags::Read))             glFlags |= GL_MAP_READ_BIT;
	if (HasFlag(flags, MapFlags::Write))            glFlags |= GL_MAP_WRITE_BIT;
	if (HasFlag(flags, MapFlags::Persistent))       glFlags |= GL_MAP_PERSISTENT_BIT;
	if (HasFlag(flags, MapFlags::Coherent))         glFlags |= GL_MAP_COHERENT_BIT;
	if (HasFlag(flags, MapFlags::InvalidateBuffer)) glFlags |= GL_MAP_INVALIDATE_BUFFER_BIT;
	if (HasFlag(flags, MapFlags::InvalidateRange))  glFlags |= GL_MAP_INVALIDATE_RANGE_BIT;
	if (HasFlag(flags, MapFlags::FlushExplicit))    glFlags |= GL_MAP_FLUSH_EXPLICIT_BIT;
	if (HasFlag(flags, MapFlags::Unsynchronized))   glFlags |= GL_MAP_UNSYNCHRONIZED_BIT;

	vbo.Bind();
	return glMapBufferRange(ToGLTarget(bufType), static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), glFlags);
}

void GLBuffer::FlushMappedRange(size_t offset, size_t size) {
	vbo.Bind();
	glFlushMappedBufferRange(ToGLTarget(bufType), static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size));
}

void GLBuffer::Upload(const void* data, size_t offset, size_t size) {
	vbo.Bind();
	vbo.SetBufferSubData(static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
	vbo.Unbind();
}

void GLBuffer::Resize(size_t newSize) {
	vbo.Bind();
	vbo.Resize(newSize, ToGLUsage(bufUsage));
	vbo.Unbind();
}

void GLBuffer::Invalidate() {
	vbo.Bind();
	vbo.Invalidate();
	vbo.Unbind();
}

} // namespace RHI
