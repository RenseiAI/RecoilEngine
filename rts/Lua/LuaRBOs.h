/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef LUA_RBOS_H
#define LUA_RBOS_H

/**
 * Lua Render Buffer Objects (RBO) Management
 *
 * RBOs are used as depth/stencil attachments for FBOs when the attachment
 * doesn't need to be sampled as a texture.
 *
 * RHI Migration Notes:
 * - RBOs are an OpenGL concept without direct Metal equivalent
 * - On Metal, use texture attachments with memoryless storage mode
 * - The RHI IRHIFramebuffer::AttachRenderbuffer() handles this internally
 * - For now, RBOs continue to use GL EXT renderbuffer functions
 */

#include <vector>

#include "Rendering/GL/myGL.h"


struct lua_State;


class LuaRBOs {
	public:
		LuaRBOs() { rbos.reserve(8); }
		~LuaRBOs();

		void Clear() { rbos.clear(); }

		static bool PushEntries(lua_State* L);

		struct RBO;
		static const RBO* GetLuaRBO(lua_State* L, int index);

	public:
		struct RBO {
			void Init();
			void Free(lua_State* L);

			GLuint index = -1u; // into LuaRBOs::rbos
			GLuint id    = 0;

			GLenum target = 0;
			GLenum format = 0;

			GLsizei xsize   = 0;
			GLsizei ysize   = 0;
			GLsizei samples = 0;
		};

	private:
		std::vector<RBO*> rbos;

	private: // helpers
		static bool CreateMetatable(lua_State* L);

	private: // metatable methods
		static int meta_gc(lua_State* L);
		static int meta_index(lua_State* L);
		static int meta_newindex(lua_State* L);

	private:
		static int CreateRBO(lua_State* L);
		static int DeleteRBO(lua_State* L);
};


#endif /* LUA_RBOS_H */
