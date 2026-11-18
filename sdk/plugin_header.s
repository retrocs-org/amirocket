        .text
        .globl _ar_plugin_header
_ar_plugin_header:
        .long 0x41525031
        .long 1
        .long 16
        .long _ar_plugin_entry
