// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "shared_memory.h"
#include "sync_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>

// Variables globales
game_state_t *game_state = NULL;
game_sync_t *game_sync = NULL;
int shm_state_fd = -1;
int shm_sync_fd = -1;
bool cleanup_done = false;
char player_name[MAX_NAME_LENGTH] = {0};
int player_id = -1;
int pipe_write_fd = 1; //el master se encarga de que el extremo de escritura del pipe anónimo esté asociado al fd 1 (stdout) del jugador 
unsigned char move = -1;

void cleanup_resources()
{
    if (cleanup_done)
        return;
    cleanup_done = true;

    // Desconectar memorias compartidas
    if (game_state != NULL)
    {
        unmap_shared_memory(game_state, sizeof(game_state_t) + game_state->board_width * game_state->board_height * sizeof(int));
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

// Inicializa la semilla aleatoria una sola vez al inicio del programa
void init_random_seed(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srandom((unsigned int)(ts.tv_sec ^ ts.tv_nsec));
}

// Llama a esta función cada vez que necesites un número aleatorio
unsigned char generate_random_direction(void) {
    return (unsigned char)(random() % 8);
}

// Función para encontrar mi ID de jugador
int find_my_player_id() {
    pid_t my_pid = getpid();
    for (int i = 0; i < game_state->player_count; i++) {
        if (game_state->players[i].pid == my_pid) {
            return i;
        }
    }
    return -1;
}

int main(int argc, char *argv[])
{
    init_random_seed();

    // Registrar manejadores de señales
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    
    // Abrir memoria compartida de sincronización
    shm_sync_fd = open_shared_memory(GAME_SYNC_NAME, sizeof(game_sync_t), O_RDWR);
    if (shm_sync_fd == -1)
    {
        perror("open_shared_memory game_sync");
        fprintf(stderr, "Player must be started by master process\n");
        cleanup_resources();
        return EXIT_FAILURE;
    }
    
    game_sync = map_shared_memory(shm_sync_fd, sizeof(game_sync_t), false);
    if (game_sync == MAP_FAILED)
    {
        perror("map_shared_memory game_sync");
        cleanup_resources();
        return EXIT_FAILURE;
    }
    
    // Abrir la memoria compartida del estado del juego
    shm_state_fd = open_shared_memory(GAME_STATE_NAME, 0, O_RDONLY);
    if (shm_state_fd == -1)
    {
        perror("open_shared_memory game_state");
        cleanup_resources();
        return EXIT_FAILURE;
    }
    
    // Obtener el tamaño real de la memoria compartida
    struct stat shm_stat;
    if (fstat(shm_state_fd, &shm_stat) == -1)
    {
        perror("fstat");
        cleanup_resources();
        return EXIT_FAILURE;
    }
    
    // Mapear la memoria compartida
    game_state = map_shared_memory(shm_state_fd, shm_stat.st_size, true);
    if (game_state == NULL)
    {
        perror("map_shared_memory game_state");
        cleanup_resources();
        return EXIT_FAILURE;
    }

    reader_enter(game_sync);    
    player_id = find_my_player_id();
    reader_exit(game_sync);

    if (player_id == -1) {
        fprintf(stderr, "Error: no se pudo identificar al jugador\n");
        cleanup_resources();
        return EXIT_FAILURE;
    }
    
    bool gameOver = false;

    // Loop principal del juego
    while(1)
    {
       // Esperar mi turno
        wait_player_turn(game_sync, player_id);

        /* Acá deberíamos entrar como readers en game_sync? */
        reader_enter(game_sync);
        
        gameOver = game_state->game_over;

        /* Salir como readers de game_sync? */
        reader_exit(game_sync);

        if(gameOver) {
            reader_exit(game_sync);
            break;
        }
        // Generar un movimiento aleatorio
        move = generate_random_direction();
        
        // Enviar el movimiento
        write(pipe_write_fd, &move, sizeof(move));
    }
    

    
    cleanup_resources();
    return EXIT_SUCCESS;
}