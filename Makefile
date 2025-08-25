CC = gcc
CFLAGS = -std=gnu99 -Wall -I.
LDFLAGS = -pthread
LDFLAGS_VIEW = -pthread -lncurses

EXECUTABLES = master player view_simple view

SOURCES_MASTER = master.c shared_memory.c sync_utils.c
SOURCES_PLAYER = players/player.c shared_memory.c sync_utils.c
SOURCES_VIEW   = view.c shared_memory.c sync_utils.c
SOURCES_VIEW_SIMPLE = view_simple.c shared_memory.c sync_utils.c

all: $(EXECUTABLES) chompchamps

chompchamps:
	@if [ ! -f ./ChompChamps ]; then cp ChompChamps .; fi

master: $(SOURCES_MASTER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

player: $(SOURCES_PLAYER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

view: $(SOURCES_VIEW)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_VIEW)

view_simple: $(SOURCES_VIEW_SIMPLE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(EXECUTABLES)
