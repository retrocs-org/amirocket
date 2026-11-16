/*
 * AmiRocket - Rocket.Chat message markup to plain text.
 *
 * The OS 3.2 listbrowser cannot mix text styles, so markup is removed
 * rather than rendered:
 *   ``` fences          dropped, code lines kept verbatim
 *   `code`              backticks removed, contents untouched
 *   *bold* _italic_ ~strike~ (and doubled forms)   markers removed
 *   [text](url)         "text (url)", or just the url when they match
 *   # Heading           hashes removed
 *   :shortcode:         common emoji as emoticons, others kept as typed
 * Input and output are UTF-8.
 */
#ifndef AR_MDTEXT_H
#define AR_MDTEXT_H

#include "buf.h"

void md_to_plain(Buf *out, const char *utf8);

/* Emoticon for an emoji shortcode without colons ("heart_eyes" -> "<3"),
 * or NULL when there is none. */
const char *md_emoticon(const char *code);

#endif
