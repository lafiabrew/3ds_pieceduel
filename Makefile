# Makefile for 3ds_pieceduel
# Supports:
#   make test-host     - Compile & run native C11 unit + headless UI simulation tests (ASan/UBSan)
#   make test-compat   - Run cross-language C-vs-TS differential engine tests & P2P/Web Socket.IO tests
#   make 3ds           - Build 3DS ARM11 .elf, .smdh, .3dsx, and .cia (via local devkitARM or OCI container)
#   make netload IP=.. - Push 3ds_pieceduel.3dsx directly to CFW 3DS Homebrew Launcher over Wi-Fi

APP_TITLE   := PieceDuel
APP_DESC    := 4-in-a-Row Strategy Game
APP_AUTHOR  := lafiabrew
APP_CODE    := CTR-P-PD3D
TARGET      := 3ds_pieceduel

HOST_CC     := gcc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -g -Iinclude
SAN_FLAGS   := -fsanitize=address,undefined

ENGINE_SRCS := \
	src/engine/attributes.c \
	src/engine/board.c \
	src/engine/rules.c \
	src/engine/state.c \
	src/engine/ai.c \
	src/engine/puzzle.c \
	src/engine/forced_win.c \
	src/engine/review.c \
	src/engine/tutorial.c

NET_SRCS := \
	src/net/net_common.c \
	src/net/net_p2p.c \
	src/net/net_uds.c \
	src/net/net_web.c

APP_SRCS := \
	src/storage/storage.c \
	src/app.c \
	src/ui/ui_input.c \
	src/ui/ui_render.c

OCI_RUNTIME := $(shell command -v podman 2>/dev/null || command -v docker 2>/dev/null)
OCI_IMAGE   := localhost/pieceduel-3ds-toolchain:latest
OCI_USER    := $(if $(findstring podman,$(OCI_RUNTIME)),--userns=keep-id,-u $(shell id -u):$(shell id -g))

.PHONY: all host test test-host test-compat 3ds 3ds-native 3ds-container assets verify-3ds netload qr serve-cia clean

all: test

assets:
	@python3 meta/generate_assets.py

host: build/host/pd_cli build/host/test_engine build/host/test_ui_sim build/host/$(TARGET)_host

build/host:
	@mkdir -p build/host

build/host/pd_cli: $(ENGINE_SRCS) tests/pd_cli.c | build/host
	$(HOST_CC) $(HOST_CFLAGS) $(SAN_FLAGS) $(ENGINE_SRCS) tests/pd_cli.c -lm -o $@

build/host/test_engine: $(ENGINE_SRCS) tests/test_engine.c | build/host
	$(HOST_CC) $(HOST_CFLAGS) $(SAN_FLAGS) $(ENGINE_SRCS) tests/test_engine.c -lm -o $@

build/host/test_ui_sim: $(ENGINE_SRCS) $(NET_SRCS) $(APP_SRCS) src/platform/platform_host.c tests/test_ui_sim.c | build/host
	$(HOST_CC) $(HOST_CFLAGS) $(SAN_FLAGS) $(ENGINE_SRCS) $(NET_SRCS) $(APP_SRCS) src/platform/platform_host.c tests/test_ui_sim.c -lm -o $@

build/host/$(TARGET)_host: $(ENGINE_SRCS) $(NET_SRCS) $(APP_SRCS) src/platform/platform_host.c src/main.c | build/host
	$(HOST_CC) $(HOST_CFLAGS) $(ENGINE_SRCS) $(NET_SRCS) $(APP_SRCS) src/platform/platform_host.c src/main.c -lm -o $@

test-host: build/host/test_engine build/host/test_ui_sim
	./build/host/test_engine
	./build/host/test_ui_sim

test-compat: build/host/pd_cli
	node tests/compat_diff.test.mjs
	node tests/net_compat.test.mjs

test: test-host test-compat

# Native 3DS ARM11 target (invoked inside devkitARM environment or container)
3ds-native: assets
	@mkdir -p build/3ds
	$(DEVKITARM)/bin/arm-none-eabi-gcc \
		-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
		-std=gnu11 -Wall -Wextra -Werror -Os -flto -fomit-frame-pointer -ffunction-sections -fdata-sections \
		-D__3DS__ -Iinclude \
		-I$(DEVKITPRO)/libctru/include \
		$(ENGINE_SRCS) $(NET_SRCS) $(APP_SRCS) src/platform/platform_3ds.c src/main.c \
		-specs=3dsx.specs \
		-L$(DEVKITPRO)/libctru/lib \
		-Wl,--gc-sections \
		-lcitro2d -lcitro3d -lctru -lm \
		-o build/3ds/$(TARGET).elf
	$(DEVKITARM)/bin/arm-none-eabi-strip --strip-debug build/3ds/$(TARGET).elf
	$(DEVKITPRO)/tools/bin/smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" meta/icon.png build/3ds/$(TARGET).smdh
	python3 -c 'import sys; sys.path.insert(0, "meta"); from generate_bnr_shared import patch_smdh_for_cia; patch_smdh_for_cia("build/3ds/$(TARGET).smdh")'
	$(DEVKITPRO)/tools/bin/3dsxtool build/3ds/$(TARGET).elf build/3ds/$(TARGET).3dsx --smdh=build/3ds/$(TARGET).smdh
	@if command -v makerom >/dev/null 2>&1; then \
		makerom -f cia -o build/3ds/$(TARGET).cia -DAPP_ENCRYPTED=false -rsf meta/app.rsf -target t -exefslogo -elf build/3ds/$(TARGET).elf -icon build/3ds/$(TARGET).smdh -banner meta/banner.bnr; \
	fi

3ds-container: assets
	@if ! $(OCI_RUNTIME) image inspect $(OCI_IMAGE) >/dev/null 2>&1; then \
		$(OCI_RUNTIME) build -f Containerfile.3ds -t $(OCI_IMAGE) .; \
	fi
	$(OCI_RUNTIME) run --rm $(OCI_USER) -v "$(CURDIR):/work:Z" -w /work $(OCI_IMAGE) make 3ds-native

3ds:
	@if [ -n "$(DEVKITARM)" ] && [ -x "$(DEVKITARM)/bin/arm-none-eabi-gcc" ]; then \
		$(MAKE) 3ds-native; \
	else \
		$(MAKE) 3ds-container; \
	fi

verify-3ds:
	bash scripts/verify-3ds-build.sh

netload:
	@if [ -z "$(IP)" ]; then echo "Usage: make netload IP=<3ds-ip>"; exit 1; fi
	@if command -v 3dslink >/dev/null 2>&1; then \
		3dslink -a "$(IP)" build/3ds/$(TARGET).3dsx; \
	else \
		$(OCI_RUNTIME) run --rm --net=host -v "$(CURDIR):/work:Z" -w /work $(OCI_IMAGE) 3dslink -a "$(IP)" build/3ds/$(TARGET).3dsx; \
	fi

qr serve-cia:
	python3 ../serve_3ds.py $(TARGET) $(if $(IP),--push $(IP),)

clean:
	rm -rf build/ meta/icon.png meta/banner.png meta/audio.wav meta/banner.bnr
