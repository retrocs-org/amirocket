/*
 * AmiRocket - colour themes.
 *
 * Every theme except "Workbench" opens AmiRocket on its own public screen
 * (same mode and font as Workbench) with the theme's palette and DrawInfo
 * pens, so ReAction draws all gadgets, frames, title bars and menus in the
 * theme's colours. The screen is called "AMIROCKET" and can be switched to
 * like any other screen (Amiga+M).
 */
#ifndef AR_THEME_H
#define AR_THEME_H

#include <exec/types.h>

/* Palette slots used by every theme (first 16 colours of the screen). */
enum {
    TC_BG = 0,        /* window background */
    TC_TEXT,          /* normal text */
    TC_SHINE,         /* light 3D edge */
    TC_FILL,          /* accent: active title bar, selection, sender names */
    TC_SHADOW,        /* dark 3D edge */
    TC_FILLTEXT,      /* text on the accent */
    TC_HIGHLIGHT,     /* mentions, unread rooms */
    TC_BARBLOCK,      /* screen bar / menu background */
    TC_BARDETAIL,     /* screen bar / menu text */
    TC_BARTRIM,       /* line under the screen bar */
    TC_EXTRA1, TC_EXTRA2, TC_EXTRA3, TC_EXTRA4, TC_EXTRA5, TC_EXTRA6,
    TC_COUNT
};

typedef struct {
    const char *id;            /* stored in the prefs file */
    const char *name;          /* menu label */
    int         workbench;     /* 1: no own screen, use the Workbench colours */
    ULONG       rgb[TC_COUNT]; /* 0xRRGGBB */
} Theme;

int          theme_count(void);
const Theme *theme_get(int i);
int          theme_find(const char *id);   /* index, 0 (Workbench) if unknown */

/* SA_Pens array (NUMDRIPENS entries + ~0) for a themed screen. */
const UWORD *theme_pens(void);
/* Fills an SA_Colors32 table for the theme: needs 2 + 3*TC_COUNT ULONGs. */
void         theme_colors32(const Theme *t, ULONG *table);

#endif
