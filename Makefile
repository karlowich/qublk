BUILD_DIR ?= builddir

CLANG_FORMAT ?= clang-format
SRC := $(wildcard src/*.c src/*.h)

.PHONY: config config-debug build install clean format format-check 

config:
	meson setup $(BUILD_DIR)

config-debug:
	meson setup $(BUILD_DIR) --buildtype=debug

build:
	meson compile -C $(BUILD_DIR)

install:
	meson install -C $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

format:
	$(CLANG_FORMAT) -i $(SRC)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SRC)
