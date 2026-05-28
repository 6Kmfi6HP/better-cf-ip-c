CC ?= gcc

# Detect OS
UNAME_S := $(shell uname -s)

# macOS Homebrew openssl is keg-only, need explicit pkg-config path
ifeq ($(UNAME_S),Darwin)
BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
ifneq ($(BREW_PREFIX),)
BREW_OPENSSL := $(BREW_PREFIX)/opt/openssl@3
ifneq ($(wildcard $(BREW_OPENSSL)/lib/pkgconfig),)
export PKG_CONFIG_PATH := $(BREW_OPENSSL)/lib/pkgconfig:$(PKG_CONFIG_PATH)
endif
endif
endif

PKG_CFLAGS := $(shell pkg-config --cflags libcurl openssl 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs libcurl openssl 2>/dev/null)
ifeq ($(strip $(PKG_LIBS)),)
PKG_LIBS := -lcurl -lssl -lcrypto
endif

# Platform-specific settings
ifneq (,$(findstring MINGW,$(UNAME_S)))
TARGET := better-cf-ip-c.exe
else
TARGET := better-cf-ip-c
endif

SRC := better_cf_ip.c
TEST_SRC := test_better_cf_ip.c
TEST_BIN := test_runner

WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion \
            -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes \
            -Wmissing-declarations -Wcast-qual -Wwrite-strings

.PHONY: all release debug asan test format clean
.DEFAULT_GOAL := release

all: release

release: $(TARGET)

$(TARGET): $(SRC)
	$(CC) -O3 -std=c11 -Wall -Wextra -pedantic $(PKG_CFLAGS) -o $@ $< $(PKG_LIBS) -pthread

debug: $(TARGET)-debug

$(TARGET)-debug: $(SRC)
	$(CC) -O0 -g3 -ggdb -std=c11 $(WARNINGS) $(PKG_CFLAGS) -o $@ $< $(PKG_LIBS) -pthread

asan: $(TARGET)-asan

$(TARGET)-asan: $(SRC)
	$(CC) -O1 -g -std=c11 $(WARNINGS) -fsanitize=address -fsanitize=undefined \
		-fno-omit-frame-pointer $(PKG_CFLAGS) -o $@ $< $(PKG_LIBS) -pthread

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) $(SRC)
	$(CC) -DUNIT_TESTING -O0 -g -std=c11 $(WARNINGS) $(PKG_CFLAGS) -o $@ $^ $(PKG_LIBS) -pthread

format:
	clang-format -i -style=file $(SRC)

clean:
	rm -f $(TARGET) $(TARGET)-debug $(TARGET)-asan $(TEST_BIN)
	rm -rf $(TARGET)-debug.dSYM $(TARGET)-asan.dSYM $(TEST_BIN).dSYM
