/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef RHI_MATRIX_STACK_H
#define RHI_MATRIX_STACK_H

#include <cassert>
#include <stack>

#include "System/Matrix44f.h"

namespace RHI {

/**
 * CPU-side matrix stack that replaces FFP glPushMatrix/glPopMatrix.
 *
 * Usage:
 *   RHI::MatrixStack modelView;               // starts with identity
 *   {
 *       RHI::ScopedMatrixPush guard(modelView);
 *       modelView.Translate(x, y, z);
 *       modelView.RotateY(angle);              // angle in radians
 *       shader->SetUniformMatrix4fv("modelMatrix", false, modelView.Top());
 *       // ... draw ...
 *   } // automatically pops
 *
 * No GL dependency. Callers converting from glRotatef(degrees,...) should
 * multiply by math::DEG_TO_RAD (from System/MathConstants.h).
 *
 * No built-in Ortho/Frustum — use LoadMatrix(CMatrix44f::OrthoProj(...)).
 */
class MatrixStack {
public:
	MatrixStack() {
		stack.push(CMatrix44f::Identity());
	}

	explicit MatrixStack(const CMatrix44f& initial) {
		stack.push(initial);
	}

	/// Duplicate the top matrix (like glPushMatrix)
	MatrixStack& Push() {
		stack.push(stack.top());
		return *this;
	}

	/// Remove the top matrix (like glPopMatrix). Must not pop the last entry.
	MatrixStack& Pop() {
		assert(stack.size() > 1 && "MatrixStack::Pop() called on stack with only one entry");
		stack.pop();
		return *this;
	}

	/// Access the current (top) matrix
	CMatrix44f& Top() { return stack.top(); }
	const CMatrix44f& Top() const { return stack.top(); }

	/// Reset top matrix to identity (like glLoadIdentity)
	MatrixStack& LoadIdentity() {
		stack.top().LoadIdentity();
		return *this;
	}

	/// Replace top matrix (like glLoadMatrixf)
	MatrixStack& LoadMatrix(const CMatrix44f& mat) {
		stack.top() = mat;
		return *this;
	}

	/// Post-multiply top matrix (like glMultMatrixf)
	MatrixStack& MultMatrix(const CMatrix44f& mat) {
		stack.top() *= mat;
		return *this;
	}

	/// Translate (like glTranslatef)
	MatrixStack& Translate(float x, float y, float z) {
		stack.top().Translate(x, y, z);
		return *this;
	}

	MatrixStack& Translate(const float3& pos) {
		stack.top().Translate(pos);
		return *this;
	}

	/// Uniform scale
	MatrixStack& Scale(float s) {
		stack.top().Scale(s);
		return *this;
	}

	/// Non-uniform scale (like glScalef)
	MatrixStack& Scale(float x, float y, float z) {
		stack.top().Scale(x, y, z);
		return *this;
	}

	MatrixStack& Scale(const float3& scales) {
		stack.top().Scale(scales);
		return *this;
	}

	/// Rotate around X axis. Angle in radians (like glRotatef but radians).
	MatrixStack& RotateX(float angle) {
		stack.top().RotateX(angle);
		return *this;
	}

	/// Rotate around Y axis. Angle in radians.
	MatrixStack& RotateY(float angle) {
		stack.top().RotateY(angle);
		return *this;
	}

	/// Rotate around Z axis. Angle in radians.
	MatrixStack& RotateZ(float angle) {
		stack.top().RotateZ(angle);
		return *this;
	}

	/// Rotate around arbitrary axis. Angle in radians, axis must be normalized.
	MatrixStack& Rotate(float angle, const float3& axis) {
		stack.top().Rotate(angle, axis);
		return *this;
	}

	/// Current stack depth (1 = just the base matrix)
	size_t Depth() const { return stack.size(); }

private:
	std::stack<CMatrix44f> stack;
};


/**
 * RAII guard that pushes on construction and pops on destruction.
 *
 *   RHI::MatrixStack mv;
 *   {
 *       RHI::ScopedMatrixPush guard(mv);
 *       mv.Translate(1, 2, 3);
 *       // ...
 *   } // pops here
 */
class ScopedMatrixPush {
public:
	explicit ScopedMatrixPush(MatrixStack& stack) : stack(stack) {
		stack.Push();
	}

	~ScopedMatrixPush() {
		stack.Pop();
	}

	ScopedMatrixPush(const ScopedMatrixPush&) = delete;
	ScopedMatrixPush& operator=(const ScopedMatrixPush&) = delete;

private:
	MatrixStack& stack;
};

} // namespace RHI

#endif // RHI_MATRIX_STACK_H
