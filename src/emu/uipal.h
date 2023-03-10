/*********************************************************************

    uipal.h

    Palette functions for the user interface.

    Copyright Nicola Salmoria and the MAME Team.
    Visit http://mamedev.org for licensing and usage restrictions.

*********************************************************************/

#ifndef __PALETTE_DEFINE__
#define __PALETTE_DEFINE__

enum
{
#ifdef UI_COLOR_DISPLAY
	SYSTEM_COLOR_BACKGROUND,
	CURSOR_SELECTED_TEXT,
	CURSOR_SELECTED_BG,
	CURSOR_HOVER_TEXT,
	CURSOR_HOVER_BG,
	BUTTON_COLOR_RED,
	BUTTON_COLOR_YELLOW,
	BUTTON_COLOR_GREEN,
	BUTTON_COLOR_BLUE,
	BUTTON_COLOR_PURPLE,
	BUTTON_COLOR_PINK,
	BUTTON_COLOR_AQUA,
	BUTTON_COLOR_SILVER,
	BUTTON_COLOR_NAVY,
	BUTTON_COLOR_LIME,
#endif /* UI_COLOR_DISPLAY */
	MAX_COLORTABLE
};
#endif
