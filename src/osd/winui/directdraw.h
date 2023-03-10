/***************************************************************************

  M.A.M.E.UI  -  Multiple Arcade Machine Emulator with User Interface
  Win32 Portions Copyright (C) 1997-2003 Michael Soderstrom and Chris Kirmse,
  Copyright (C) 2003-2007 Chris Kirmse and the MAME32/MAMEUI team.

  This file is part of MAMEUI, and may only be used, modified and
  distributed under the terms of the MAME license, in "readme.txt".
  By continuing to use, modify or distribute this file you indicate
  that you have read the license and understand and accept it fully.

 ***************************************************************************/

#ifndef DIRECTDRAW_H
#define DIRECTDRAW_H

#include <ddraw.h>

#define MAXMODES    256 /* Maximum number of DirectDraw Display modes. */

/* Display mode node */
struct tDisplayMode
{
	DWORD m_dwWidth;
	DWORD m_dwHeight;
	DWORD m_dwBPP;
	DWORD m_dwRefresh;
};

/* EnumDisplayMode Context */
struct tDisplayModes
{
	struct tDisplayMode m_Modes[MAXMODES];
	int                 m_nNumModes;
};

extern BOOL DirectDraw_Initialize(void);
extern void DirectDraw_Close(void);

extern struct tDisplayModes* DirectDraw_GetDisplayModes(void);
extern int            DirectDraw_GetNumDisplays(void);
extern BOOL           DirectDraw_HasHWStretch(void);
extern BOOL           DirectDraw_HasRefresh(void);
extern const char*    DirectDraw_GetDisplayName(int num_display);
extern const char*    DirectDraw_GetDisplayDriver(int num_display);

#endif
