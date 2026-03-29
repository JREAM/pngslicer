CC = gcc
TARGET = pngslicer
SRC = pngslicer.c

# Detect all possible ImageMagick 6/7 paths
IM_INCLUDE = /usr/include/ImageMagick-6
IM7_INCLUDE = /usr/include/ImageMagick-7

# Get flags from pkg-config (default)
PKG_CFLAGS = $(shell pkg-config --cflags MagickWand 2>/dev/null || pkg-config --cflags ImageMagick 2>/dev/null)
PKG_LIBS = $(shell pkg-config --libs MagickWand 2>/dev/null || pkg-config --libs ImageMagick 2>/dev/null)

# Fallback headers if pkg-config is not available or incomplete
CFLAGS = $(PKG_CFLAGS) -I$(IM_INCLUDE) -I$(IM7_INCLUDE) -Wall -O2
LIBS = $(PKG_LIBS)

# Default build target
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)
	rm -f *-*.png

help:
	@echo "Usage:"
	@echo "  make         - Build the pngextract utility"
	@echo "  make clean   - Remove the binary and extracted PNGs"
	@echo "  make help    - Show this message"

.PHONY: all clean help
