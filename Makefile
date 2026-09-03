CC=m68k-amigaos-gcc
CFLAGS=-s -Os

TARGET=robovac
SRC=robovac.c

# robovac.c pulls the other modules in with #include (see robovac.c and
# robovac.h) so the compiler is only ever invoked on robovac.c itself, but
# every file below still needs to be listed here so `make` rebuilds when
# any of them change.
SOURCES=robovac.c robovac.h game.c ai.c render.c audio.c minigames.c network.c

RELEASE_ROOT ?= release
RELEASE_NAME ?= RoboVac-Rescue
RELEASE_DIR := $(RELEASE_ROOT)/$(RELEASE_NAME)

.PHONY: all clean release release-clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

release: $(TARGET)
	rm -rf "$(RELEASE_DIR)"
	mkdir -p "$(RELEASE_DIR)"
	cp "$(TARGET)" "$(RELEASE_DIR)/"
	@if [ -f README.md ]; then cp README.md "$(RELEASE_DIR)/"; fi
	@for f in *.info; do \
		[ -e "$$f" ] || continue; \
		cp "$$f" "$(RELEASE_DIR)/"; \
	done
	@if [ -d tiles ]; then cp -R tiles "$(RELEASE_DIR)/"; fi
	@if [ -d samples ]; then cp -R samples "$(RELEASE_DIR)/"; fi
	@echo "Release drawer ready: $(RELEASE_DIR)"

release-clean:
	rm -rf "$(RELEASE_ROOT)"

clean:
	rm -f $(TARGET)
