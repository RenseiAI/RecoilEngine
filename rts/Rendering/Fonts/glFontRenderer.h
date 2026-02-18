#pragma once

#include <memory>

#include "Rendering/GL/VertexArrayTypes.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/RHI/RHITexture.h"
#include "System/Matrix44f.h"

class CglFont;
class CFontTexture;
namespace Shader { struct IProgramObject; }
class CglFontRenderer {
public:
	virtual ~CglFontRenderer() = default;

	virtual void AddQuadTrianglesPB(VA_TYPE_TC&& tl, VA_TYPE_TC&& tr, VA_TYPE_TC&& br, VA_TYPE_TC&& bl) = 0;
	virtual void AddQuadTrianglesOB(VA_TYPE_TC&& tl, VA_TYPE_TC&& tr, VA_TYPE_TC&& br, VA_TYPE_TC&& bl) = 0;
	virtual void DrawTraingleElements() = 0;
	virtual void SetWorldTransform(const CMatrix44f& mvp) {}
	virtual void ClearWorldTransform() {}
	virtual void HandleTextureUpdate(CFontTexture& font, bool onlyUpload) = 0;
	virtual void PushGLState(const CglFont& font) = 0;
	virtual void PopGLState(const CglFont& font) = 0;

	virtual bool IsLegacy() const = 0;
	virtual bool IsValid() const = 0;
	virtual void GetStats(std::array<size_t, 8>& stats) const = 0;

	void SetUserDefinedBlending(bool enableUserDefinedBlending) { userDefinedBlending = enableUserDefinedBlending; };

	static std::unique_ptr<CglFontRenderer> CreateInstance();
	static void DeleteInstance(std::unique_ptr<CglFontRenderer>& instance);
protected:
	Shader::IProgramObject* prevBoundProgram = nullptr;
	bool userDefinedBlending = false;

	// should be enough to hold all data for a given frame
	static constexpr size_t NUM_BUFFER_ELEMS = (1 << 14);
	static constexpr size_t NUM_TRI_BUFFER_VERTS = (4 * NUM_BUFFER_ELEMS);
	static constexpr size_t NUM_TRI_BUFFER_ELEMS = (6 * NUM_BUFFER_ELEMS);
};

class CglShaderFontRenderer final: public CglFontRenderer {
public:
	CglShaderFontRenderer();
	~CglShaderFontRenderer() override;

	void AddQuadTrianglesPB(VA_TYPE_TC&& tl, VA_TYPE_TC&& tr, VA_TYPE_TC&& br, VA_TYPE_TC&& bl) override;
	void AddQuadTrianglesOB(VA_TYPE_TC&& tl, VA_TYPE_TC&& tr, VA_TYPE_TC&& br, VA_TYPE_TC&& bl) override;
	void DrawTraingleElements() override;
	void SetWorldTransform(const CMatrix44f& mvp) override;
	void ClearWorldTransform() override;
	void HandleTextureUpdate(CFontTexture& font, bool onlyUpload) override;
	void PushGLState(const CglFont& font) override;
	void PopGLState(const CglFont& font) override;

	bool IsLegacy() const override { return false; }
	bool IsValid() const override { return fontShader->IsValid(); }
	void GetStats(std::array<size_t, 8>& stats) const override;
private:
	TypedRenderBuffer<VA_TYPE_TC> primaryBufferTC;
	TypedRenderBuffer<VA_TYPE_TC> outlineBufferTC;

	CMatrix44f worldTransform;
	bool hasWorldTransform = false;

	static inline size_t fontShaderRefs = 0;
	static inline std::unique_ptr<Shader::IProgramObject> fontShader = nullptr;
	static inline size_t fontShaderColorRefs = 0;
	static inline std::unique_ptr<Shader::IProgramObject> fontShaderColor = nullptr;
};

class CglNullFontRenderer final : public CglFontRenderer {
	// Inherited via CglFontRenderer
	void AddQuadTrianglesPB(VA_TYPE_TC&& tl, VA_TYPE_TC&& tr, VA_TYPE_TC&& br, VA_TYPE_TC&& bl) override {}
	void AddQuadTrianglesOB(VA_TYPE_TC&& tl, VA_TYPE_TC&& tr, VA_TYPE_TC&& br, VA_TYPE_TC&& bl) override {}
	void DrawTraingleElements() override {}
	void HandleTextureUpdate(CFontTexture& font, bool onlyUpload) override {}
	void PushGLState(const CglFont& font) override {}
	void PopGLState(const CglFont& font) override {}
	bool IsLegacy() const override { return true; }
	bool IsValid() const override { return true; }
	void GetStats(std::array<size_t, 8>& stats) const override;
};
