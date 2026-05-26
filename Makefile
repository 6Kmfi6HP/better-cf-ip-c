CC ?= gcc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -pedantic -pthread
PKG_CFLAGS := $(shell pkg-config --cflags libcurl openssl 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs libcurl openssl 2>/dev/null)
ifeq ($(strip $(PKG_LIBS)),)
PKG_LIBS := -lcurl -lssl -lcrypto
endif

TARGET := better-cf-ip-c
SRC := better_cf_ip.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ $< $(PKG_LIBS) -pthread

clean:
	rm -f $(TARGET)
