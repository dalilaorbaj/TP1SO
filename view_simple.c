// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
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

// Variables globales
game_state_t *game_state = NULL;
game_sync_t *game_sync = NULL;
int shm_state_fd = -1;
int shm_sync_fd = -1;
bool cleanup_done = false;
unsigned short width, height;

void cleanup_resources()
{
    if (cleanup_done)
        return;
    cleanup_done = true;

    // Desconectar memorias compartidas
    if (game_state != NULL && width > 0 && height > 0)
    {
        size_t state_size = sizeof(game_state_t) + width * height * sizeof(int);
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
    notify_view_done(game_sync);
    cleanup_resources();
    exit(EXIT_SUCCESS);
}

// Versión simple del tablero sin ncurses
void draw_board_simple(game_state_t *state)
{
    printf("\n=== ChompChamps Board (%dx%d) ===\n", state->board_width, state->board_height);

    // Dibujar el tablero
    for (int y = 0; y < state->board_height; y++)
    {
        for (int x = 0; x < state->board_width; x++)
        {
            int cell_value = state->board[y * state->board_width + x];

            // Verificar si hay un jugador en esta posición
            bool player_here = false;
            int player_id = -1;
            for (unsigned int i = 0; i < state->player_count; i++)
            {
                if (state->players[i].pos_x == x && state->players[i].pos_y == y)
                {
                    player_here = true;
                    player_id = i + 1;
                    break;
                }
            }

            if (player_here)
            {
                printf("P%d ", player_id);
            }
            else if (cell_value >= 1 && cell_value <= 9)
            {
                printf("%d  ", cell_value);
            }
            else if (cell_value <= 0 && cell_value >= -8)
            {
                printf("## "); // Celda capturada
            }
            else
            {
                printf("?  "); // Valor inesperado
            }
        }
        printf("\n");
    }
}

void draw_scoreboard_simple(game_state_t *state)
{
    printf("\n=== SCOREBOARD ===\n");

    for (unsigned int i = 0; i < state->player_count; i++)
    {
        printf("Player %u (%s): Score %u, Moves: %u valid / %u invalid %s\n",
            i + 1,
            state->players[i].player_name,
            state->players[i].score,
            state->players[i].valid_moves,
            state->players[i].invalid_moves,
            state->players[i].is_blocked ? "[BLOCKED]" : "");
    }

    printf("\n");
}

int main(int argc, char *argv[])
{
    // Registrar manejadores de señales
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Starting simple view (no ncurses)...\n");

    // Primero abrir memoria compartida de sincronización
    shm_sync_fd = open_shared_memory(GAME_SYNC_NAME, sizeof(game_sync_t), O_RDWR);
    if (shm_sync_fd == -1)
    {
        perror("open_shared_memory game_sync");
        fprintf(stderr, "View must be started by master process\n");
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

    // Ahora abrir la memoria compartida del estado
    shm_state_fd = open_shared_memory(GAME_STATE_NAME, 0, O_RDONLY);
    if (shm_state_fd == -1)
    {
        perror("open_shared_memory game_state");
        fprintf(stderr, "Error opening game state shared memory\n");
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

    // Actualizar las variables globales width y height
    width = game_state->board_width;
    height = game_state->board_height;

    printf("Board size: %hux%hu, Player count: %u\n", width, height, game_state->player_count);

    printf("Waiting for game updates...\n\n");

    // Loop principal
    while (!game_state->game_over)
    {
        // Esperar notificación del master usando funciones de sync_utils.h
        wait_view_notification(game_sync);

        printf("\033[2J\033[H"); // Limpiar pantalla en terminal

        // Lectura segura del estado del juego
        reader_enter(game_sync);

        // Dibujar el tablero y la información del juego con formato simple
        draw_board_simple(game_state);
        draw_scoreboard_simple(game_state);

        reader_exit(game_sync);

        // Notificar al master que hemos terminado
        notify_view_done(game_sync);

        // Pequeña pausa para no consumir CPU
        usleep(100000); // 100ms
    }

    printf("Game over!\n");
    cleanup_resources();
    return EXIT_SUCCESS;
}
