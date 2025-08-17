chompchamps:
	cp ref/ChompChamps build/ChompChamps
CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -O2 -pedantic
LDFLAGS=-pthread

BUILD_DIR=build
SRC_MASTER=$(wildcard src/master/*.c) $(wildcard shared/*.c)
SRC_VIEW=$(wildcard src/view/*.c) $(wildcard shared/*.c)
SRC_PLAYER=$(wildcard src/player/*.c) $(wildcard shared/*.c)

all: $(BUILD_DIR)/master $(BUILD_DIR)/view $(BUILD_DIR)/player

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/master: $(BUILD_DIR) $(SRC_MASTER)
	$(CC) $(CFLAGS) -o $@ $(SRC_MASTER) $(LDFLAGS)

$(BUILD_DIR)/view: $(BUILD_DIR) $(SRC_VIEW)
	$(CC) $(CFLAGS) -o $@ $(SRC_VIEW) $(LDFLAGS)

$(BUILD_DIR)/player: $(BUILD_DIR) $(SRC_PLAYER)
	$(CC) $(CFLAGS) -o $@ $(SRC_PLAYER) $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
