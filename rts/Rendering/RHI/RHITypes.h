/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_TYPES_H
#define RHI_TYPES_H

/**
 * RHI Type Definitions
 *
 * Maps OpenGL concepts to backend-agnostic enums and structs:
 *   GL_ARRAY_BUFFER / GL_ELEMENT_ARRAY_BUFFER  ->  RHIBufferType::Vertex / Index
 *   GL_STREAM_DRAW / GL_STATIC_DRAW            ->  RHIBufferUsage::Stream / Static
 *   GL_TEXTURE_2D / GL_TEXTURE_CUBE_MAP        ->  RHITextureType::Texture2D / TextureCube
 *   glEnable(GL_DEPTH_TEST) + glDepthFunc()    ->  RHIDepthStencilState
 *   glEnable(GL_BLEND) + glBlendFunc()         ->  RHIBlendState
 *   glEnable(GL_CULL_FACE) + glCullFace()      ->  RHICullMode
 *   GL pipeline state (mutable)                ->  RHIPipelineDesc (immutable descriptor)
 *   FBO bind/attach                            ->  RHIRenderPassDesc
 *
 * FFP Matrix Stack Migration Strategy:
 * ------------------------------------
 * The fixed-function pipeline (FFP) matrix stack (glMatrixMode, glPushMatrix,
 * glPopMatrix, glLoadMatrixf, glMultMatrixf, glTranslatef, glRotatef, glScalef,
 * glOrtho, glFrustum) has NO direct RHI equivalent. Instead:
 *
 * 1. Use CMatrix44f (from System/Matrix44f.h) for all matrix computations
 * 2. Pass matrices to shaders via uniform buffers or SetUniformMatrix4fv
 * 3. The GL4 path already uses uniform-based matrices; the legacy FFP path
 *    should be migrated to match
 *
 * Example migration:
 *   // Before (FFP):
 *   glPushMatrix();
 *   glTranslatef(x, y, z);
 *   glRotatef(angle, 0, 1, 0);
 *   // ... draw ...
 *   glPopMatrix();
 *
 *   // After (RHI):
 *   CMatrix44f savedMatrix = currentMatrix;
 *   currentMatrix.Translate(x, y, z);
 *   currentMatrix.RotateY(angle);
 *   shader->SetUniformMatrix4fv("modelMatrix", false, currentMatrix);
 *   // ... draw ...
 *   currentMatrix = savedMatrix;
 *   shader->SetUniformMatrix4fv("modelMatrix", false, currentMatrix);
 *
 * For common transform patterns, use RHI::MatrixStack (see MatrixStack.h)
 */

#include <cstdint>
#include <string>
#include <vector>

// macOS system headers may define these as macros (from NSObjCRuntime.h, X11, etc.)
#ifdef Always
#undef Always
#endif
#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif

namespace RHI {

enum class Backend : uint8_t {
	OpenGL,
	Metal
};

// --- Buffer map flags (bitfield) ---

enum class MapFlags : uint32_t {
	Read             = 1 << 0,
	Write            = 1 << 1,
	Persistent       = 1 << 2,
	Coherent         = 1 << 3,
	InvalidateBuffer = 1 << 4,
	InvalidateRange  = 1 << 5,
	FlushExplicit    = 1 << 6,
	Unsynchronized   = 1 << 7,
};

inline MapFlags operator|(MapFlags a, MapFlags b) {
	return static_cast<MapFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline MapFlags operator&(MapFlags a, MapFlags b) {
	return static_cast<MapFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasFlag(MapFlags flags, MapFlags test) {
	return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

// --- Fence sync ---

using FenceHandle = void*;

// --- GPU/driver version info ---

struct VersionInfo {
	std::string vendor;
	std::string renderer;
	std::string version;
	std::string shadingLanguageVersion;
};

// --- Buffer ---

enum class BufferType : uint8_t {
	Vertex,       // GL_ARRAY_BUFFER
	Index,        // GL_ELEMENT_ARRAY_BUFFER
	Uniform,      // GL_UNIFORM_BUFFER
	Storage,      // GL_SHADER_STORAGE_BUFFER
	PixelPack,    // GL_PIXEL_PACK_BUFFER
	PixelUnpack   // GL_PIXEL_UNPACK_BUFFER
};

enum class BufferUsage : uint8_t {
	Static,   // GL_STATIC_DRAW  - written once, read many
	Dynamic,  // GL_DYNAMIC_DRAW - written frequently, read many
	Stream    // GL_STREAM_DRAW  - written every frame
};

// --- Texture ---

enum class TextureFormat : uint8_t {
	RGBA8,
	RGB8,
	RG8,
	R8,
	RGBA16F,
	RGB16F,
	RG16F,
	R16F,
	RGBA32F,
	RGB32F,
	RG32F,
	R32F,
	R32I,
	Depth16,
	Depth24,
	Depth32F,
	Depth24Stencil8,
	Depth32FStencil8,
	SRGB8Alpha8,
	CompressedDXT1,
	CompressedDXT5
};

enum class TextureType : uint8_t {
	Texture1D,
	Texture2D,
	Texture3D,
	Texture1DArray,
	Texture2DArray,
	TextureCube,
	TextureRect,
	TextureBuffer,
	Texture2DMS
};

enum class TextureFilter : uint8_t {
	Nearest,
	Linear,
	NearestMipmapNearest,
	LinearMipmapNearest,
	NearestMipmapLinear,
	LinearMipmapLinear
};

enum class TextureWrap : uint8_t {
	Repeat,
	ClampToEdge,
	ClampToBorder,
	MirroredRepeat
};

/// Texture swizzle component sources (for SetSwizzle)
enum class SwizzleComponent : uint8_t {
	Red   = 0,
	Green = 1,
	Blue  = 2,
	Alpha = 3,
	Zero  = 4,
	One   = 5
};

/// Cubemap face indices (matches GL_TEXTURE_CUBE_MAP_POSITIVE_X offset order)
enum class CubeFace : uint8_t {
	PositiveX = 0,
	NegativeX = 1,
	PositiveY = 2,
	NegativeY = 3,
	PositiveZ = 4,
	NegativeZ = 5
};

// --- Shader ---

enum class ShaderStage : uint8_t {
	Vertex,
	Fragment,
	Geometry,
	Compute
};

// --- Pipeline state ---

enum class BlendFactor : uint8_t {
	Zero,
	One,
	SrcColor,
	OneMinusSrcColor,
	DstColor,
	OneMinusDstColor,
	SrcAlpha,
	OneMinusSrcAlpha,
	DstAlpha,
	OneMinusDstAlpha,
	ConstantColor,
	OneMinusConstantColor,
	ConstantAlpha,
	OneMinusConstantAlpha,
	SrcAlphaSaturate
};

enum class BlendOp : uint8_t {
	Add,
	Subtract,
	ReverseSubtract,
	Min,
	Max
};

enum class CompareFunc : uint8_t {
	Never,
	Less,
	LessEqual,
	Equal,
	NotEqual,
	GreaterEqual,
	Greater,
	Always
};

enum class StencilOp : uint8_t {
	Keep,
	Zero,
	Replace,
	IncrClamp,
	DecrClamp,
	Invert,
	IncrWrap,
	DecrWrap
};

enum class CullMode : uint8_t {
	None,
	Front,
	Back
};

enum class FrontFace : uint8_t {
	CounterClockwise,  // GL_CCW (OpenGL default)
	Clockwise          // GL_CW
};

enum class PrimitiveType : uint8_t {
	Points,
	Lines,
	LineStrip,
	Triangles,
	TriangleStrip,
	TriangleFan
};

enum class PolygonMode : uint8_t {
	Fill,
	Line,
	Point
};

enum class IndexType : uint8_t {
	UInt16,
	UInt32
};

// --- Vertex layout ---

enum class VertexFormat : uint8_t {
	Float1,
	Float2,
	Float3,
	Float4,
	UByte4,
	UByte4Norm,
	Short2,
	Short2Norm,
	Short4,
	Short4Norm,
	Int1,
	Int2,
	Int3,
	Int4
};

struct VertexAttribute {
	uint32_t     location;
	uint32_t     offset;
	VertexFormat  format;
};

struct VertexLayout {
	VertexAttribute* attributes;
	uint32_t         attributeCount;
	uint32_t         stride;
};

// --- Blend state ---

struct BlendState {
	bool        enabled      = false;
	BlendFactor srcColor     = BlendFactor::One;
	BlendFactor dstColor     = BlendFactor::Zero;
	BlendOp     colorOp      = BlendOp::Add;
	BlendFactor srcAlpha     = BlendFactor::One;
	BlendFactor dstAlpha     = BlendFactor::Zero;
	BlendOp     alphaOp      = BlendOp::Add;
	bool        colorMask[4] = {true, true, true, true};
	float       blendColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

// --- Depth/Stencil state ---

struct DepthStencilState {
	bool        depthTestEnabled  = true;
	bool        depthWriteEnabled = true;
	CompareFunc depthFunc         = CompareFunc::Less;
	bool        stencilEnabled    = false;
	CompareFunc stencilFunc       = CompareFunc::Always;
	uint32_t    stencilRef        = 0;
	uint32_t    stencilReadMask   = 0xFF;
	uint32_t    stencilWriteMask  = 0xFF;
	StencilOp   stencilFailOp     = StencilOp::Keep;
	StencilOp   stencilDepthFailOp = StencilOp::Keep;
	StencilOp   stencilPassOp     = StencilOp::Keep;
};

// --- Rasterizer state ---

struct RasterizerState {
	CullMode    cullMode    = CullMode::Back;
	FrontFace   frontFace   = FrontFace::CounterClockwise;
	PolygonMode polygonMode = PolygonMode::Fill;
	bool        scissorEnabled    = false;
	bool        depthClampEnabled = false;
	bool        polygonOffsetEnabled = false;  // GL_POLYGON_OFFSET_FILL/LINE/POINT
	float       polygonOffsetFactor = 0.0f;
	float       polygonOffsetUnits  = 0.0f;
	float       lineWidth           = 1.0f;
	float       pointSize           = 1.0f;    // GL_PROGRAM_POINT_SIZE when > 0
};

// --- Pipeline descriptor ---

struct PipelineDesc {
	BlendState        blend;
	DepthStencilState depthStencil;
	RasterizerState   rasterizer;
	// Shader and vertex layout are set separately when binding
};

// --- Render pass ---

enum class LoadAction : uint8_t {
	Load,      // preserve existing contents
	Clear,     // clear to a specified value
	DontCare   // contents undefined (performance hint)
};

enum class StoreAction : uint8_t {
	Store,     // write results to attachment
	DontCare   // contents may be discarded
};

struct ClearColor {
	float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
};

struct ColorAttachment {
	LoadAction  loadAction  = LoadAction::Load;
	StoreAction storeAction = StoreAction::Store;
	ClearColor  clearColor;
	// Texture handle set by backend
};

struct DepthAttachment {
	LoadAction  loadAction  = LoadAction::Load;
	StoreAction storeAction = StoreAction::Store;
	float       clearDepth  = 1.0f;
};

struct RenderPassDesc {
	ColorAttachment  colorAttachments[8];
	uint32_t         colorAttachmentCount = 0;
	DepthAttachment  depthAttachment;
	bool             hasDepth = false;
};

// --- Viewport / Scissor ---

struct Viewport {
	float x      = 0.0f;
	float y      = 0.0f;
	float width  = 0.0f;
	float height = 0.0f;
	float minDepth = 0.0f;
	float maxDepth = 1.0f;
};

struct ScissorRect {
	int32_t  x      = 0;
	int32_t  y      = 0;
	uint32_t width  = 0;
	uint32_t height = 0;
};

} // namespace RHI

#endif // RHI_TYPES_H
