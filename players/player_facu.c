#include "shared_memory.h"
#include "sync_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>

static int player_id = -1;
static int board_width = 0;
static int board_height = 0;
static volatile size_t  running = 1;

// Variables globales
game_state_t *game_state = NULL;
game_sync_t *game_sync = NULL;
int shm_state_fd = -1;
int shm_sync_fd = -1;
bool cleanup_done = false;

static int dx[] = {0, 1, 1, 1, 0, -1, -1, -1};
static int dy[] = {-1, -1, 0, 1, 1, 1, 0, -1};

void cleanup_resources()
{
    if (cleanup_done)
        return;
    cleanup_done = true;

    // Desconectar memorias compartidas
    if (game_state != NULL && board_width > 0 && board_height > 0)
    {
        size_t state_size = sizeof(game_state_t) + board_width * board_height * sizeof(int);
        unmap_shared_memory(game_state, state_size);
        game_state = NULL;
    }

    if (game_sync != NULL)
    {
        unmap_shared_memory(game_sync, sizeof(game_sync_t));
        game_sync = NULL;
    }

    // Cerrar file descriptors
    if (shm_state_fd != -1)
    {
        close_shared_memory(shm_state_fd);
        shm_state_fd = -1;
    }

    if (shm_sync_fd != -1)
    {
        close_shared_memory(shm_sync_fd);
        shm_sync_fd = -1;
    }
}

void signal_handler(int sig)
{
    cleanup_resources();
    exit(EXIT_SUCCESS);
}


int wait_for_turn(int player_id) {
    if (game_sync == NULL) {
        return -1;
    }
    
    wait_player_turn(game_sync, player_id);
    return running ? 0 : -1;
}

int is_valid_move(int dir){
    return dir>=0 && dir<8 && game_state->players[player_id].pos_x+dx[dir]>=0 && game_state->players[player_id].pos_x+dx[dir]<board_width &&
           game_state->players[player_id].pos_y+dy[dir]>=0 && game_state->players[player_id].pos_y+dy[dir]<board_height;
}

int send_move() {

    writer_enter(game_sync);

    int r = rand();
    for(int i=0 ; i<8 ; i++){
        if(is_valid_move((i+r)%8)){
            char chosen_dir = (char)((i+r)%8);
            if(write(STDOUT_FILENO, &chosen_dir, 1) != 1){
                perror("Error: cannot write player's movement.\n");
                writer_exit(game_sync);
                return -1;
            }
            break;
        }
        
    }
    
    writer_exit(game_sync);
    
    return 0;
}


int main(int argc, char *argv[])
{
    if(argc != 3){
        perror("invalid parameters.\n");
        return 1;
    }
    
    board_width = atoi(argv[1]);
    board_height = atoi(argv[2]);
    
    if(board_width<=0 || board_height<=0){
        fprintf(stderr, "Error: width and height must be positive.\n");
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Player process started\n");
    
    shm_sync_fd = open_shared_memory(GAME_SYNC_NAME, sizeof(game_sync_t), O_RDWR);
    if (shm_sync_fd == -1)
    {
        perror("open_shared_memory game_sync");
        fprintf(stderr, "Player must be started by master process\n");
        cleanup_resources();
        return EXIT_FAILURE;
    }

    game_sync = map_shared_memory(shm_sync_fd, sizeof(game_sync_t));
    if (game_sync == MAP_FAILED)
    {
        perror("map_shared_memory game_sync");
        cleanup_resources();
        return EXIT_FAILURE;
    }

    shm_state_fd = open_shared_memory(GAME_STATE_NAME, 0, O_RDONLY);
    if (shm_state_fd == -1)
    {
        perror("open_shared_memory game_state");
        fprintf(stderr, "Error opening game state shared memory\n");
        cleanup_resources();
        return EXIT_FAILURE;
    }

    struct stat shm_stat;
    if (fstat(shm_state_fd, &shm_stat) == -1)
    {
        perror("fstat");
        cleanup_resources();
        return EXIT_FAILURE;
    }

    game_state = map_shared_memory(shm_state_fd, shm_stat.st_size);
    if (game_state == NULL)
    {
        perror("map_shared_memory game_state");
        cleanup_resources();
        return EXIT_FAILURE;
    }

    srand(time(NULL) + getpid());

    while (running) {
        
        if (wait_for_turn(player_id) != 0) { // esperar su turno
            break;
        }
        
        if (send_move() != 0) {
            perror("Error: cannot send player's movement.\n");
            break;
        }
    
        
        usleep(10000); // 10ms
    }

    return 0;
}
