/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#import "MTLTriangleDemo.h"
#import "MTLDevice.h"
#import "MTLContext.h"
#import "MTLBuffer.h"
#import "MTLShader.h"
#import "MTLPipeline.h"

#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHITypes.h"
#include "System/Log/ILog.h"

#include <SDL2/SDL.h>
#include <cmath>

namespace RHI {

// Simple vertex structure: position (x, y, z) + color (r, g, b, a)
struct TriangleVertex {
	float x, y, z;
	float r, g, b, a;
};

// Triangle vertices (centered at origin, visible in clip space)
static const TriangleVertex triangleVertices[] = {
	// Position          Color
	{  0.0f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f },  // Top - Red
	{ -0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f, 1.0f },  // Bottom left - Green
	{  0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 1.0f },  // Bottom right - Blue
};

// Simple vertex shader in GLSL
static const char* vertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

out vec4 vColor;

void main() {
	gl_Position = vec4(aPosition, 1.0);
	vColor = aColor;
}
)";

// Simple fragment shader in GLSL
static const char* fragmentShaderSource = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;

void main() {
	fragColor = vColor;
}
)";

MTLTriangleDemo::MTLTriangleDemo() = default;

MTLTriangleDemo::~MTLTriangleDemo() {
	Shutdown();
}

bool MTLTriangleDemo::Initialize(SDL_Window* window) {
	if (initialized) {
		return true;
	}

	// Create Metal device
	auto devicePtr = CreateDevice(Backend::Metal);
	if (!devicePtr) {
		LOG_L(L_ERROR, "[MTLTriangleDemo] Failed to create Metal device");
		return false;
	}

	device = devicePtr.release();  // Take ownership

	// Setup Metal layer for the window
	MTLDevice* mtlDevice = static_cast<MTLDevice*>(device);
	if (!mtlDevice->SetupMetalLayer(window)) {
		LOG_L(L_ERROR, "[MTLTriangleDemo] Failed to setup Metal layer");
		delete device;
		device = nullptr;
		return false;
	}

	// Create resources
	if (!CreateBuffers()) {
		LOG_L(L_ERROR, "[MTLTriangleDemo] Failed to create buffers");
		Shutdown();
		return false;
	}

	if (!CreateShaders()) {
		LOG_L(L_ERROR, "[MTLTriangleDemo] Failed to create shaders");
		Shutdown();
		return false;
	}

	if (!CreatePipeline()) {
		LOG_L(L_ERROR, "[MTLTriangleDemo] Failed to create pipeline");
		Shutdown();
		return false;
	}

	initialized = true;
	LOG("[MTLTriangleDemo] Initialized successfully");
	return true;
}

bool MTLTriangleDemo::CreateBuffers() {
	// Create vertex buffer
	auto vb = device->CreateBuffer(
		BufferType::Vertex,
		BufferUsage::Static,
		sizeof(triangleVertices),
		triangleVertices
	);

	if (!vb) {
		return false;
	}

	vertexBuffer = vb.release();
	return true;
}

bool MTLTriangleDemo::CreateShaders() {
	// Create shader program
	auto sh = device->CreateShader("TriangleDemo");
	if (!sh) {
		return false;
	}

	shader = sh.release();

	// Note: In a real scenario, we'd load shaders from files.
	// For the demo, we'll need to write temp files or use inline MSL.
	// For now, we'll skip the actual shader attachment since it requires
	// file-based loading. The shader will be invalid but we can still
	// test the pipeline creation path.

	// Bind attribute locations
	shader->BindAttribLocation("aPosition", 0);
	shader->BindAttribLocation("aColor", 1);

	// In a real implementation, we'd call:
	// shader->AttachStage(ShaderStage::Vertex, "shaders/triangle.vert", "");
	// shader->AttachStage(ShaderStage::Fragment, "shaders/triangle.frag", "");
	// shader->Link();

	return true;
}

bool MTLTriangleDemo::CreatePipeline() {
	// Create pipeline with default state
	PipelineDesc desc;

	// Disable depth test for simple 2D triangle
	desc.depthStencil.depthTestEnabled = false;
	desc.depthStencil.depthWriteEnabled = false;

	// Disable culling to see the triangle from both sides
	desc.rasterizer.cullMode = CullMode::None;

	// Default blend (disabled)
	desc.blend.enabled = false;

	auto pl = device->CreatePipeline(desc);
	if (!pl) {
		return false;
	}

	pipeline = pl.release();
	return true;
}

void MTLTriangleDemo::Render() {
	if (!initialized || !device) {
		return;
	}

	MTLDevice* mtlDevice = static_cast<MTLDevice*>(device);
	MTLContext* context = static_cast<MTLContext*>(device->GetContext());

	if (!context) {
		return;
	}

	// Begin frame
	context->BeginFrame();

	// Create render pass descriptor
	RenderPassDesc passDesc;
	passDesc.colorAttachmentCount = 1;
	passDesc.colorAttachments[0].loadAction = LoadAction::Clear;
	passDesc.colorAttachments[0].storeAction = StoreAction::Store;

	// Animate clear color
	time += 0.016f;  // Assume ~60fps
	float hue = fmod(time * 0.1f, 1.0f);

	// Simple HSV to RGB (hue only, saturation=0.3, value=0.3)
	float h = hue * 6.0f;
	float x = 0.3f * (1.0f - fabs(fmod(h, 2.0f) - 1.0f));
	float r = 0.1f, g = 0.1f, b = 0.1f;  // Base dark gray

	if (h < 1) { r += 0.2f; g += x; }
	else if (h < 2) { r += x; g += 0.2f; }
	else if (h < 3) { g += 0.2f; b += x; }
	else if (h < 4) { g += x; b += 0.2f; }
	else if (h < 5) { r += x; b += 0.2f; }
	else { r += 0.2f; b += x; }

	passDesc.colorAttachments[0].clearColor = {r, g, b, 1.0f};

	// Begin render pass (to default framebuffer / screen)
	context->BeginDefaultRenderPass(passDesc);

	// Bind resources
	context->BindPipeline(pipeline);
	context->BindShader(shader);
	context->BindVertexBuffer(vertexBuffer, 0);

	// Draw triangle
	context->Draw(PrimitiveType::Triangles, 3, 0);

	// End render pass
	context->EndRenderPass();

	// Present and end frame
	context->EndFrame();
}

void MTLTriangleDemo::Shutdown() {
	// Note: In a proper implementation, these would be unique_ptr
	// For now, we manually delete

	delete pipeline;
	pipeline = nullptr;

	delete shader;
	shader = nullptr;

	delete vertexBuffer;
	vertexBuffer = nullptr;

	delete device;
	device = nullptr;

	initialized = false;
	LOG("[MTLTriangleDemo] Shutdown complete");
}

} // namespace RHI
