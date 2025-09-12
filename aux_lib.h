#include "shared_memory.h"
#include <stdlib.h>


void cleanup_resources(bool * cleanup_done, game_state_t * game_state, game_sync_t * game_sync, int * shm_sync_fd, int * shm_state_fd);
void signal_handler(int sig);