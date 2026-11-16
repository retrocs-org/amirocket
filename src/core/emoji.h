/*
 * AmiRocket - emoji table shared by text conversion, markup and the
 * Amiga's pixel-art emoji images (same index everywhere).
 *
 * In ISO-8859-1 display text an emoji is encoded as two bytes,
 * EMOJI_MARK followed by EMOJI_BASE + index; the views draw the image.
 */
#ifndef AR_EMOJI_H
#define AR_EMOJI_H

#include <stddef.h>

#define EMOJI_MARK 0x01
#define EMOJI_BASE 0x80   /* index byte never looks like space or newline */

typedef struct {
    unsigned long cp;        /* Unicode code point */
    const char   *code;      /* shortcode without colons */
    const char   *fallback;  /* ASCII emoticon */
} EmojiInfo;

typedef struct {
    const char *alias;
    int         idx;
} EmojiAlias;

extern const EmojiInfo  emoji_table[];
extern const int        emoji_count;
extern const EmojiAlias emoji_aliases[];

int emoji_by_cp(unsigned long cp);                  /* index or -1 */
int emoji_by_code(const char *code, size_t len);    /* shortcode or alias, -1 */
/* Indices whose shortcode (or alias) starts with prefix; returns count. */
int emoji_complete(const char *prefix, int *out, int max);

#endif
