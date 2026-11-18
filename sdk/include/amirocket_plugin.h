#ifndef AMIROCKET_PLUGIN_H
#define AMIROCKET_PLUGIN_H
/* ABI 1: AmigaOS m68k, 32-bit longs/pointers, C stack calling convention.
 * No SDK structure owns passed strings. Copy data you retain after a callback. */
#define AR_PLUGIN_ABI 1UL
#define AR_PLUGIN_MAGIC 0x41525031UL
typedef struct ArPluginHost ArPluginHost;
typedef struct { unsigned long size; const char *kind, *room_id, *username, *text; } ArPluginEvent;
struct ArPluginHost {
    unsigned long abi, size;
    void *context;
    long (*config_get)(void *context, const char *key, char *out, long size);
    long (*config_set)(void *context, const char *key, const char *value);
    long (*insert_text)(void *context, const char *latin1);
    long (*get_selection)(void *context, char *out, long size);
    void (*local_time)(void *context, char *out, long size);
    void (*status)(void *context, const char *latin1);
};
typedef struct {
    unsigned long abi, size;
    const char *id, *name, *action_label;
    long (*start)(const ArPluginHost *host); /* nonzero success, must not block */
    void (*stop)(const ArPluginHost *host);
    void (*action)(const ArPluginHost *host);
    void (*event)(const ArPluginHost *host, const ArPluginEvent *event);
} ArPlugin;
typedef struct {
    unsigned long magic, abi, size;
    const ArPlugin *(*entry)(void);
} ArPluginFile;
/* Supply this function; link sdk/plugin_header.s first (no C startup/runtime). */
const ArPlugin *ar_plugin_entry(void);
#endif
