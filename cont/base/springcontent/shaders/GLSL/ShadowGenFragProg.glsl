#if (GL_FRAGMENT_PRECISION_HIGH == 1)
// ancient GL3 ATI drivers confuse GLSL for GLSL-ES and require this
precision highp float;
#else
precision mediump float;
#endif

#if (GL_ARB_conservative_depth == 1 && SUPPORT_DEPTH_LAYOUT == 1)
	layout(depth_unchanged) out float gl_FragDepth;
#endif

uniform sampler2D alphaMaskTex;
uniform vec2 alphaParams;
uniform vec4 alphaCtrl = vec4(0.0, 0.0, 0.0, 1.0); //always pass

bool AlphaDiscard(float a) {
	float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
	float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
	return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

out vec4 fragColor;

void main() {
	// Guard: skip texture sample when alphaCtrl is default "always pass" (w=1.0).
	// This shader is shared by MODEL, MAP, and PROJECTILE shadow programs.
	// Only MODEL sets alphaCtrl; others use the default and skip this block.
	if (alphaCtrl.w < 1.0) {
		if (AlphaDiscard(texture2D(alphaMaskTex, gl_TexCoord[0].st).a))
			discard;
	}
}
