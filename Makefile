CC = gcc
TARGET = pngslicer
SRC = pngslicer.c

# Get flags from pkg-config, with error check
MAGICK_CFLAGS := $(shell pkg-config --cflags MagickWand 2>/dev/null || pkg-config --cflags ImageMagick 2>/dev/null)
MAGICK_LIBS := $(shell pkg-config --libs MagickWand 2>/dev/null || pkg-config --libs ImageMagick 2>/dev/null)

ifeq ($(MAGICK_LIBS),)
$(error MagickWand or ImageMagick not found via pkg-config. Install libmagickwand-dev)
endif

# Standard flags
CFLAGS = $(MAGICK_CFLAGS) -Wall -Wextra -O2
LIBS = $(MAGICK_LIBS)

# Default build target
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)
	rm -rf out/bats-test

help:
	@echo "Usage:"
	@echo "  make         - Build the $(TARGET) utility"
	@echo "  make clean   - Remove the binary and test artifacts"
	@echo "  make help    - Show this message"

.PHONY: all clean help
