/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef MTL_TRIANGLE_DEMO_H
#define MTL_TRIANGLE_DEMO_H

/**
 * Metal Triangle Demo
 *
 * A simple demonstration that renders a colored triangle using the
 * Metal RHI backend. This validates:
 *   - Device creation and Metal layer setup
 *   - Buffer creation (vertex buffer)
 *   - Shader compilation (GLSL -> MSL)
 *   - Pipeline state creation
 *   - Render pass execution
 *   - Frame presentation
 *
 * Usage:
 *   MTLTriangleDemo demo;
 *   if (demo.Initialize(window)) {
 *       while (running) {
 *           demo.Render();
 *       }
 *       demo.Shutdown();
 *   }
 */

struct SDL_Window;

namespace RHI {

class IRHIDevice;
class IRHIBuffer;
class IRHIShader;
class IRHIPipeline;

class MTLTriangleDemo {
public:
	MTLTriangleDemo();
	~MTLTriangleDemo();

	// Prevent copying
	MTLTriangleDemo(const MTLTriangleDemo&) = delete;
	MTLTriangleDemo& operator=(const MTLTriangleDemo&) = delete;

	/// Initialize the demo with an SDL window.
	/// Returns true on success.
	bool Initialize(SDL_Window* window);

	/// Render one frame.
	void Render();

	/// Clean up resources.
	void Shutdown();

	/// Check if the demo is ready to render.
	bool IsReady() const { return initialized; }

private:
	bool CreateShaders();
	bool CreateBuffers();
	bool CreatePipeline();

	IRHIDevice*   device   = nullptr;
	IRHIBuffer*   vertexBuffer = nullptr;
	IRHIShader*   shader   = nullptr;
	IRHIPipeline* pipeline = nullptr;

	bool initialized = false;
	float time = 0.0f;
};

} // namespace RHI

#endif // MTL_TRIANGLE_DEMO_H
