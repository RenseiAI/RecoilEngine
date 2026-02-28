/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef WINDOWS_H_INCLUDED
#define WINDOWS_H_INCLUDED

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef VC_EXTRALEAN
		// Exclude rarely-used stuff from Windows headers
		#define VC_EXTRALEAN
	#endif

	// do not include <cmath> or <math.h> before this, it'll cause ambiguous call er
	#include "lib/streflop/streflop_cond.h"

	#ifdef HEADLESS
		// workaround for mingw64 bug which leads to undefined reference to _imp__gl*
		#define _GDI32_
	#endif

	#include <windows.h>

		#undef  PlaySound
		#define PlaySound  use_PlaySample_instead_of_PlaySound
		#define W_OK 2
		#define R_OK 4

		#undef CreateDirectory
		#undef DeleteFile
		#undef SendMessage
		#undef GetCharWidth
		#undef far
		#undef near
		#undef FAR
		#undef NEAR

		// std min&max are used instead of the macros
		#ifdef min
			#undef min
			#undef max
		#endif

		// MinGW wingdi.h defines PI/TWOPI/HALFPI macros that conflict with
		// math:: namespace constants. Must undef all three.
		#ifdef PI
			#undef PI
		#endif
		#ifdef TWOPI
			#undef TWOPI
		#endif
		#ifdef HALFPI
			#undef HALFPI
		#endif

		// MinGW dlgs.h defines rad1..rad16 as dialog control IDs (0x420..0x42f)
		// which conflict with local variable names in rendering code.
		#ifdef rad1
			#undef rad1
		#endif
		#ifdef rad2
			#undef rad2
		#endif

#endif // _WIN32

#endif // WINDOWS_H_INCLUDED
