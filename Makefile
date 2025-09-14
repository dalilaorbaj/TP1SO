CC = gcc
CFLAGS = -std=gnu99 -Wall -I.
LDFLAGS = -pthread
LDFLAGS_VIEW = -pthread -lncurses

EXECUTABLES = master player view

SOURCES_MASTER = master.c shared_memory.c sync_utils.c master_lib.c
SOURCES_PLAYER = player.c shared_memory.c sync_utils.c
SOURCES_VIEW   = view.c shared_memory.c sync_utils.c

# Check if ncurses is installed
NCURSES_CHECK = $(shell pkg-config --exists ncurses 2>/dev/null && echo "yes" || echo "no")

all: check-ncurses $(EXECUTABLES) chompchamps

check-ncurses:
	@echo "Checking for ncurses library..."
	@if ! pkg-config --exists ncurses 2>/dev/null; then \
		echo "ncurses not found. Installing..."; \
		if command -v apt-get >/dev/null 2>&1; then \
			apt-get update && apt-get install -y libncurses5-dev libncursesw5-dev; \
		elif command -v brew >/dev/null 2>&1; then \
			brew install ncurses; \
		else \
			echo "Error: Package manager not found. Please install ncurses manually:"; \
			echo "  Ubuntu/Debian: apt-get install libncurses5-dev libncursesw5-dev"; \
			echo "  macOS: brew install ncurses"; \
			exit 1; \
		fi; \
	else \
		echo "ncurses library found."; \
	fi

install-deps: check-ncurses

chompchamps:
	@if [ ! -f ./ChompChamps ]; then cp ChompChamps .; fi

master: $(SOURCES_MASTER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

player: $(SOURCES_PLAYER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

view: check-ncurses $(SOURCES_VIEW)
	$(CC) $(CFLAGS) -o $@ $(SOURCES_VIEW) $(LDFLAGS_VIEW)

# Alternative target that forces dependency installation
setup: install-deps
	@echo "Dependencies installed successfully."

# Clean and rebuild everything
rebuild: clean all

.PHONY: clean check-ncurses install-deps setup rebuild
clean:
	rm -f $(EXECUTABLES)
	@echo "Cleaned executables."