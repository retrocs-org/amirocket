# Standalone SDK examples: make -C sdk [AMIGA_CC=/path/to/m68k-amigaos-gcc]
AMIGA_CC ?= $(HOME)/Development/tools/amiga/m68k-amigaos-gcc-16.2/bin/m68k-amigaos-gcc
.PHONY: all clean
all: build/timestamp.arp.meta build/quickreply.arp.meta build/timestamp.arp build/quickreply.arp build/AmiRocketMonitor
build/%.arp: examples/%/plugin.c plugin_header.s include/amirocket_plugin.h
	@mkdir -p build
	$(AMIGA_CC) -m68020 -nostdlib -nostartfiles -fno-builtin -Os -Iinclude -Wl,-e,_ar_plugin_header -o $@ plugin_header.s $<
build/AmiRocketMonitor: examples/rexx_monitor.c include/amirocket_rexx.h
	@mkdir -p build
	$(AMIGA_CC) -m68020 -noixemul -Os -o $@ $<
clean:
	rm -f build/timestamp.arp build/quickreply.arp build/AmiRocketMonitor

build/%.arp.meta: examples/%/plugin.meta
	@mkdir -p build
	cp $< $@
