CC = gcc
CFLAGS = -std=gnu99 -Wall 
LDFLAGS = -pthread
LDFLAGS_VIEW = -pthread -lncurses

EXECUTABLES = master player player_right view_simple view 

SOURCES_MASTER = master.c shared_memory.c sync_utils.c
SOURCES_PLAYER = ./players/player.c shared_memory.c sync_utils.c
SOURCES_PLAYER_RIGHT = ./players/player_right.c shared_memory.c sync_utils.c
SOURCES_VIEW   = view.c shared_memory.c sync_utils.c
SOURCES_VIEW_SIMPLE = view_simple.c shared_memory.c sync_utils.c

all: $(EXECUTABLES) chompchamps

chompchamps:
	cp ChompChamps .

master: $(SOURCES_MASTER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

player: $(SOURCES_PLAYER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

player_right: $(SOURCES_PLAYER_RIGHT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

view: $(SOURCES_VIEW)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_VIEW)

view_simple: $(SOURCES_VIEW_SIMPLE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(EXECUTABLES)
