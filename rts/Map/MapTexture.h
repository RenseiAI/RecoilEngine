/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef MAPTEXTURE_H
#define MAPTEXTURE_H

#include "System/type2.h"
#include <memory>

namespace RHI {
	class IRHITexture;
}

struct MapTextureData {
	MapTextureData(): id(0), type(0), num(0), size(0, 0) {}

	unsigned int id;   // OpenGL id
	unsigned int type; // e.g. MAP_SSMF_*
	unsigned int num;  // for MAP_SSMF_SPLAT_NORMAL_TEX

	int2 size;
};

struct MapTexture {
public:
	enum {
		RAW_TEX_IDX = 0,
		LUA_TEX_IDX = 1,
	};

	MapTexture();
	~MapTexture();

	bool HasLuaTex() const { return (texIDs[LUA_TEX_IDX] != 0); }

	// only needed for glGenTextures(...)
	unsigned int* GetIDPtr() { return &texIDs[RAW_TEX_IDX]; }
	unsigned int GetID() const { return texIDs[HasLuaTex()]; }

	int2 GetSize() const { return texDims[HasLuaTex()]; }
	int2 GetRawSize() const { return texDims[RAW_TEX_IDX]; }
	int2 GetLuaSize() const { return texDims[LUA_TEX_IDX]; }

	void SetSize(const int2 size) { texDims[HasLuaTex()] = size; }
	void SetRawSize(const int2 size) { texDims[RAW_TEX_IDX] = size; }
	void SetLuaSize(const int2 size) { texDims[LUA_TEX_IDX] = size; }

	void SetRawTexID(unsigned int rawTexID) { texIDs[RAW_TEX_IDX] = rawTexID; }
	void SetLuaTexID(unsigned int luaTexID) { texIDs[LUA_TEX_IDX] = luaTexID; }

	void SetLuaTexture(const MapTextureData& texData) {
		texIDs[LUA_TEX_IDX] = texData.id;
		texDims[LUA_TEX_IDX] = texData.size;
	}

	// --- RHI texture accessors (incremental migration support) ---

	/// Get the RHI texture for the active slot (RAW or LUA)
	RHI::IRHITexture* GetRHITexture() const { return rhiTextures[HasLuaTex()].get(); }

	/// Get the RHI texture for the raw slot
	RHI::IRHITexture* GetRawRHITexture() const { return rhiTextures[RAW_TEX_IDX].get(); }

	/// Get the RHI texture for the Lua slot
	RHI::IRHITexture* GetLuaRHITexture() const { return rhiTextures[LUA_TEX_IDX].get(); }

	/// Set the RHI texture for the raw slot (takes ownership)
	void SetRawRHITexture(std::unique_ptr<RHI::IRHITexture> tex);

	/// Set the RHI texture for the Lua slot (takes ownership)
	void SetLuaRHITexture(std::unique_ptr<RHI::IRHITexture> tex);

private:
	// [RAW_TEX_IDX] := original, [LUA_TEX_IDX] := Lua-supplied
	unsigned int texIDs[2];

	int2 texDims[2];

	// RHI texture wrappers (parallel to texIDs for incremental migration)
	std::unique_ptr<RHI::IRHITexture> rhiTextures[2];
};

#endif

