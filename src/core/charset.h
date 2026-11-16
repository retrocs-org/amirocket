/*
 * AmiRocket - UTF-8 <-> ISO-8859-1 conversion.
 *
 * Rocket.Chat speaks UTF-8; Workbench fonts are ISO-8859-1. Characters
 * outside Latin-1 are mapped to readable ASCII where a sensible substitute
 * exists (typographic quotes, dashes, a handful of common emoji), dropped
 * when invisible (zero-width joiners, variation selectors, skin tones) and
 * shown as '?' otherwise.
 */
#ifndef AR_CHARSET_H
#define AR_CHARSET_H

#include "buf.h"

/* Converts and appends to out. multiline: keep '\n', else turn it into ' '. */
void utf8_to_latin1(Buf *out, const char *utf8, int multiline);
/* As above; with marks, known emoji become EMOJI_MARK+index (see emoji.h)
 * instead of their text fallback. */
void utf8_to_latin1_ex(Buf *out, const char *utf8, int multiline, int marks);
void latin1_to_utf8(Buf *out, const char *latin1);

/* Decodes one code point; returns bytes consumed (>=1). Invalid -> U+FFFD. */
int utf8_decode(const unsigned char *s, unsigned long *cp);

#endif
