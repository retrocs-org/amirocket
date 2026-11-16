# AmiRocket - Rocket.Chat client for AmigaOS 3.2
#
#   make            Amiga executable (68020) -> build/amiga/AmiRocket, copied to
#                   ~/Development/builds/AmiRocket/AmiRocket (Builds:AmiRocket/AmiRocket)
#   make host       host tools and tests -> build/host/{rcli,test_core}
#   make test       run host unit + integration tests (needs python3, openssl)
#   make release    LhA archive in build/release (needs lha in PATH or toolchain)
#   make qa-selftest      run SELFTEST in Amiga Workbench.app (AmigaMac)
#   make qa-probe         network/TLS check against open.rocket.chat

VERSION    := 0.1
BUILDS_DIR ?= $(HOME)/Development/builds/AmiRocket
AMIGAMAC   ?= /Applications/Amiga Workbench.app/Contents/MacOS/AmigaWorkbench
AMIGA_PREFIX ?= $(HOME)/Development/tools/amiga/m68k-amigaos-gcc-16.2
AMIGA_CC   ?= $(AMIGA_PREFIX)/bin/m68k-amigaos-gcc
AMIGA_CPU  ?= -m68020
AMIGA_CFLAGS = $(AMIGA_CPU) -noixemul -Os -std=gnu99 -Wall -Wextra -Wno-unused-parameter \
               -Wno-missing-field-initializers -Wno-format-truncation -Wno-pointer-sign -fomit-frame-pointer -DAR_VERSION=\"$(VERSION)\" -DAR_DATE=\"$(shell date +%-d.%-m.%Y)\"
AMIGA_LDFLAGS = $(AMIGA_CPU) -noixemul -s

HOST_CC    ?= cc
# OpenSSL for the host tools: pkg-config, else Homebrew's openssl@3
ifeq ($(shell pkg-config --exists openssl 2>/dev/null && echo yes),yes)
OPENSSL_CFLAGS ?= $(shell pkg-config --cflags openssl)
OPENSSL_LIBS   ?= $(shell pkg-config --libs openssl)
else
OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null)
OPENSSL_CFLAGS ?= $(if $(OPENSSL_PREFIX),-I$(OPENSSL_PREFIX)/include)
OPENSSL_LIBS   ?= $(if $(OPENSSL_PREFIX),-L$(OPENSSL_PREFIX)/lib) -lssl -lcrypto
endif
HOST_CFLAGS = -std=c99 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -g -O1 -Wall -Wextra -Wno-unused-parameter \
              -Wno-missing-field-initializers -Wno-format-truncation $(OPENSSL_CFLAGS) $(SANITIZE)

CORE_SRC  := $(wildcard src/core/*.c)
AMIGA_SRC := $(CORE_SRC) $(wildcard src/amiga/*.c) tests/test_core.c
HOST_CORE := $(CORE_SRC) src/posix/mem_posix.c src/posix/net_posix.c

AMIGA_OBJ := $(patsubst %.c,build/amiga/obj/%.o,$(AMIGA_SRC))

.PHONY: all amiga host test clean release install-builds qa-selftest qa-probe icon emoji

all: amiga

amiga: build/amiga/AmiRocket
	cp assets/icon/AmiRocket.info build/amiga/AmiRocket.info
	@if [ -d "$(dir $(BUILDS_DIR))" ]; then mkdir -p "$(BUILDS_DIR)" && cp build/amiga/AmiRocket build/amiga/AmiRocket.info "$(BUILDS_DIR)/" && echo "-> $(BUILDS_DIR)/AmiRocket (+ icon)"; fi

# Regenerates the emoji table and pixel-art bitmaps (needs Pillow); the
# results are committed.
emoji:
	python3 tools/emoji/gen_table.py src/core/emoji_table.c
	python3 tools/emoji/draw_emoji.py src/amiga/emoji_data.c assets/emoji-preview.png

# Regenerates the Workbench icon from tools/icon (needs Pillow); the result
# is committed, so normal builds do not need Python.
icon:
	python3 tools/icon/draw_icon.py assets/icon
	python3 tools/icon/make_info.py assets/icon/normal.png assets/icon/selected.png assets/icon/AmiRocket.info

build/amiga/AmiRocket: $(AMIGA_OBJ)
	@mkdir -p $(dir $@)
	$(AMIGA_CC) $(AMIGA_LDFLAGS) -o $@ $^
	@ls -l $@

build/amiga/obj/%.o: %.c $(wildcard src/core/*.h src/amiga/*.h tests/*.h)
	@mkdir -p $(dir $@)
	$(AMIGA_CC) $(AMIGA_CFLAGS) -c -o $@ $<

host: build/host/rcli build/host/test_core

build/host/rcli: tools/rcli.c $(HOST_CORE) $(wildcard src/core/*.h src/posix/*.h)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tools/rcli.c $(HOST_CORE) $(OPENSSL_LIBS)

build/host/test_core: tests/test_core.c tests/test_core_main.c $(HOST_CORE) $(wildcard src/core/*.h)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/test_core.c tests/test_core_main.c $(HOST_CORE) $(OPENSSL_LIBS)

test: host
	./build/host/test_core
	python3 tests/integration.py build/host/rcli

release: amiga
	sh tools/make-release.sh $(VERSION)

install-builds: amiga

# Only the AmiRocket folder is mounted (as AmiRocket:): test mode copies
# every mounted folder, and all of ~/Development/builds is large.
QA_RUN = "$(AMIGAMAC)" --workbench latest --mount "AmiRocket:$(BUILDS_DIR)"

qa-selftest: amiga
	$(QA_RUN) --test qa/selftest.json --output qa-results/selftest-$$(date +%Y%m%d-%H%M%S)

qa-probe: amiga
	$(QA_RUN) --test qa/probe-public.json --output qa-results/probe-$$(date +%Y%m%d-%H%M%S)

clean:
	rm -rf build
