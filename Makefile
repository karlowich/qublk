CLANG_FORMAT ?= clang-format
SRC := $(wildcard src/*.c src/*.h)

.PHONY: format format-check

format:
	$(CLANG_FORMAT) -i $(SRC)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SRC)
