/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef GL_VAO_H
#define GL_VAO_H

#include <cstdint>
#include <utility>

#include "Rendering/RHI/RHITypes.h"

/**
 * RHI Migration Note:
 *   Metal has no VAO equivalent; vertex layout is part of the pipeline
 *   descriptor. Use FormatToRHI() to convert GL vertex attribute types
 *   to RHI::VertexFormat for building RHI::VertexLayout descriptors.
 */
class VAO {
public:
	static bool IsSupported();
public:
	VAO() = default;
	VAO(const VAO& v) = delete;
	VAO(VAO&& v) noexcept { *this = std::move(v); }
	~VAO() { Delete(); }

	VAO& operator = (const VAO& v) = delete;
	VAO& operator = (VAO&& v) noexcept { std::swap(id, v.id); return *this; }

	uint32_t GetId() const { Generate(); return GetIdRaw(); }
	uint32_t GetIdRaw() const { return id; }

	void Generate() const;

	void Delete() const;
	void Bind() const;
	void Unbind() const;

	/// Convert GL vertex attribute type + component count to RHI::VertexFormat.
	static RHI::VertexFormat FormatToRHI(uint32_t glType, int count, bool normalize) {
		switch (glType) {
		case 0x1406: // GL_FLOAT
			switch (count) {
			case 1: return RHI::VertexFormat::Float1;
			case 2: return RHI::VertexFormat::Float2;
			case 3: return RHI::VertexFormat::Float3;
			case 4: return RHI::VertexFormat::Float4;
			}
			break;
		case 0x1401: // GL_UNSIGNED_BYTE
			return normalize ? RHI::VertexFormat::UByte4Norm : RHI::VertexFormat::UByte4;
		case 0x1402: // GL_SHORT
			switch (count) {
			case 2: return normalize ? RHI::VertexFormat::Short2Norm : RHI::VertexFormat::Short2;
			case 4: return normalize ? RHI::VertexFormat::Short4Norm : RHI::VertexFormat::Short4;
			}
			break;
		case 0x1404: // GL_INT
			switch (count) {
			case 1: return RHI::VertexFormat::Int1;
			case 2: return RHI::VertexFormat::Int2;
			case 3: return RHI::VertexFormat::Int3;
			case 4: return RHI::VertexFormat::Int4;
			}
			break;
		}
		return RHI::VertexFormat::Float4; // fallback
	}

private:
	mutable uint32_t id = 0;
};

#endif

