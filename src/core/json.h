/*
 * AmiRocket - small JSON tokenizer and builder.
 *
 * json_parse() turns a document into a flat token array that points into
 * the original text (no copies). Every token records the index of the
 * token that follows its subtree, so skipping and lookups are cheap.
 * Two passes are made: one to count tokens, one to fill them, so memory
 * use is exactly one array allocation per document.
 */
#ifndef AR_JSON_H
#define AR_JSON_H

#include <stddef.h>
#include "buf.h"

typedef enum {
    JT_INVALID = 0,
    JT_OBJECT,
    JT_ARRAY,
    JT_STRING,
    JT_NUMBER,
    JT_TRUE,
    JT_FALSE,
    JT_NULL
} JType;

typedef struct {
    unsigned char type;
    unsigned char escaped;  /* string contains backslash escapes */
    int start;              /* byte offsets into the text; strings exclude quotes */
    int end;
    int size;               /* object: number of pairs, array: number of items */
    int next;               /* index of the token after this subtree */
} JTok;

typedef struct {
    const char *js;
    size_t len;
    JTok *t;
    int n;
    int err_pos;            /* byte offset of a parse error, -1 if none */
} JDoc;

#define JSON_MAX_DEPTH 48

int  json_parse(JDoc *d, const char *js, size_t len);  /* 0 ok, -1 error */
void json_free(JDoc *d);

/* Navigation; all return a token index, or -1 if not found/wrong type. */
int json_get(const JDoc *d, int obj, const char *key);
int json_at(const JDoc *d, int arr, int idx);
int json_path(const JDoc *d, int root, const char *dotted);  /* "data.me.username" */
int json_first(const JDoc *d, int container);                /* first child value */
int json_next(const JDoc *d, int i);                         /* sibling after i */
int json_type(const JDoc *d, int i);
int json_size(const JDoc *d, int i);

/* Values. */
int   json_is_null(const JDoc *d, int i);         /* missing or null */
int   json_eq(const JDoc *d, int i, const char *s);
int   json_str(const JDoc *d, int i, char *out, size_t outsz); /* UTF-8, returns length or -1 */
char *json_strdup(const JDoc *d, int i);           /* ar_malloc'd UTF-8; NULL if not a string */
int   json_bool(const JDoc *d, int i, int def);
long long json_ll(const JDoc *d, int i, long long def);

/* Builder helpers. */
void json_put_string(Buf *b, const char *utf8);    /* writes a quoted, escaped string */

#endif
