#include "theme.h"
#include "../core/util.h"

#include <intuition/screens.h>
#include <string.h>

/*
 * Slot order: BG, TEXT, SHINE, FILL, SHADOW, FILLTEXT, HIGHLIGHT,
 *             BARBLOCK, BARDETAIL, BARTRIM, extras (6)
 * Catppuccin, Nord, Dracula, Gruvbox and Solarized values are the
 * projects' published palettes.
 */
static const Theme g_themes[] = {
    { "workbench", "Workbench (system colours)", 1, { 0 } },

    { "latte", "Catppuccin Latte", 0, {
        0xEFF1F5, 0x4C4F69, 0xFFFFFF, 0x1E66F5, 0x9CA0B0, 0xEFF1F5, 0xFE640B,
        0xE6E9EF, 0x4C4F69, 0x8839EF,
        0xDCE0E8, 0xCCD0DA, 0x40A02B, 0xD20F39, 0xDF8E1D, 0x7287FD } },

    { "frappe", "Catppuccin Frapp\xE9", 0, {
        0x303446, 0xC6D0F5, 0x51576D, 0x8CAAEE, 0x232634, 0x232634, 0xEF9F76,
        0x292C3C, 0xC6D0F5, 0xCA9EE6,
        0x414559, 0x737994, 0xA6D189, 0xE78284, 0xE5C890, 0xBABBF1 } },

    { "macchiato", "Catppuccin Macchiato", 0, {
        0x24273A, 0xCAD3F5, 0x494D64, 0x8AADF4, 0x181926, 0x181926, 0xF5A97F,
        0x1E2030, 0xCAD3F5, 0xC6A0F6,
        0x363A4F, 0x6E738D, 0xA6DA95, 0xED8796, 0xEED49F, 0xB7BDF8 } },

    { "mocha", "Catppuccin Mocha", 0, {
        0x1E1E2E, 0xCDD6F4, 0x45475A, 0x89B4FA, 0x11111B, 0x11111B, 0xFAB387,
        0x181825, 0xCDD6F4, 0xCBA6F7,
        0x313244, 0x6C7086, 0xA6E3A1, 0xF38BA8, 0xF9E2AF, 0xB4BEFE } },

    { "nord", "Nord", 0, {
        0x2E3440, 0xD8DEE9, 0x4C566A, 0x88C0D0, 0x242933, 0x2E3440, 0xEBCB8B,
        0x3B4252, 0xECEFF4, 0x81A1C1,
        0x434C5E, 0x5E81AC, 0xA3BE8C, 0xBF616A, 0xD08770, 0xB48EAD } },

    { "dracula", "Dracula", 0, {
        0x282A36, 0xF8F8F2, 0x44475A, 0xBD93F9, 0x191A21, 0x282A36, 0xF1FA8C,
        0x21222C, 0xF8F8F2, 0xFF79C6,
        0x6272A4, 0x8BE9FD, 0x50FA7B, 0xFF5555, 0xFFB86C, 0x343746 } },

    { "gruvbox", "Gruvbox Dark", 0, {
        0x282828, 0xEBDBB2, 0x504945, 0x83A598, 0x1D2021, 0x282828, 0xFABD2F,
        0x3C3836, 0xEBDBB2, 0xFE8019,
        0x665C54, 0x928374, 0xB8BB26, 0xFB4934, 0x8EC07C, 0xD3869B } },

    { "solarized", "Solarized Light", 0, {
        0xFDF6E3, 0x586E75, 0xFFFFFF, 0x268BD2, 0x93A1A1, 0xFDF6E3, 0xCB4B16,
        0xEEE8D5, 0x586E75, 0x2AA198,
        0xEEE8D5, 0x657B83, 0x859900, 0xDC322F, 0xB58900, 0x6C71C4 } },

    { "kick13", "Kickstart 1.3", 0, {
        0x0055AA, 0xFFFFFF, 0xFFFFFF, 0xFF8800, 0x000020, 0x000020, 0xFF8800,
        0xFFFFFF, 0x0055AA, 0x000020,
        0x6688BB, 0x003377, 0x88CC88, 0xCC4444, 0xFFCC66, 0xAAAAAA } },
};

#define N_THEMES ((int)(sizeof(g_themes) / sizeof(g_themes[0])))

int theme_count(void)
{
    return N_THEMES;
}

const Theme *theme_get(int i)
{
    return (i >= 0 && i < N_THEMES) ? &g_themes[i] : &g_themes[0];
}

int theme_find(const char *id)
{
    int i;
    if (!id)
        return 0;
    for (i = 0; i < N_THEMES; i++)
        if (!ar_strcasecmp(g_themes[i].id, id))
            return i;
    return 0;
}

const UWORD *theme_pens(void)
{
    static UWORD pens[NUMDRIPENS + 1];
    pens[DETAILPEN] = TC_BG;
    pens[BLOCKPEN] = TC_TEXT;
    pens[TEXTPEN] = TC_TEXT;
    pens[SHINEPEN] = TC_SHINE;
    pens[SHADOWPEN] = TC_SHADOW;
    pens[FILLPEN] = TC_FILL;
    pens[FILLTEXTPEN] = TC_FILLTEXT;
    pens[BACKGROUNDPEN] = TC_BG;
    pens[HIGHLIGHTTEXTPEN] = TC_HIGHLIGHT;
    pens[BARDETAILPEN] = TC_BARDETAIL;
    pens[BARBLOCKPEN] = TC_BARBLOCK;
    pens[BARTRIMPEN] = TC_BARTRIM;
    pens[BARCONTOURPEN] = TC_BARBLOCK;
    pens[NUMDRIPENS] = (UWORD)~0;
    return pens;
}

void theme_colors32(const Theme *t, ULONG *table)
{
    int i;

    table[0] = ((ULONG)TC_COUNT << 16) | 0;   /* count, first pen */
    for (i = 0; i < TC_COUNT; i++) {
        ULONG c = t->rgb[i];
        table[1 + i * 3 + 0] = ((c >> 16) & 0xFF) * 0x01010101UL;
        table[1 + i * 3 + 1] = ((c >> 8) & 0xFF) * 0x01010101UL;
        table[1 + i * 3 + 2] = (c & 0xFF) * 0x01010101UL;
    }
    table[1 + TC_COUNT * 3] = 0;
}
