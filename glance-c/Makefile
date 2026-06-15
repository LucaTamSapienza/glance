# glance-c — C rewrite of glance. Slice 1: the standalone Markdown renderer.
CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
CFLAGS  += $(shell pkg-config --cflags md4c)
LDLIBS  += $(shell pkg-config --libs md4c)

SRC_DIR := src
BUILD   := build

.PHONY: all clean

all: glance-render

# slice 1: render-only CLI
glance-render: $(SRC_DIR)/main_render.c $(SRC_DIR)/render_md.c $(SRC_DIR)/render_md.h
	$(CC) $(CFLAGS) -o $@ $(SRC_DIR)/main_render.c $(SRC_DIR)/render_md.c $(LDLIBS)

clean:
	rm -f glance-render
	rm -rf $(BUILD)
