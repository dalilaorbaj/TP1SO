/* #include "shared_memory.h"
#include "sync_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ncurses.h>
#include <signal.h>

// Variables globales
game_state_t *game_state = NULL;
game_sync_t *game_sync = NULL;
int shm_state_fd = -1;
int shm_sync_fd = -1;
bool cleanup_done = false;
unsigned short width, height;

// Colores para los jugadores (1-8, 0 es para celdas libres)
int player_colors[] = {
    COLOR_WHITE,   // Celdas libres
    COLOR_RED,     // Jugador 1
    COLOR_GREEN,   // Jugador 2
    COLOR_YELLOW,  // Jugador 3
    COLOR_BLUE,    // Jugador 4
    COLOR_MAGENTA, // Jugador 5
    COLOR_CYAN,    // Jugador 6
    COLOR_WHITE,   // Jugador 7
    COLOR_BLACK    // Jugador 8
};

void cleanup_resources()
{
    if (cleanup_done)
        return;
    cleanup_done = true;

    // Limpiar ncurses
    if (stdscr != NULL)
    {
        endwin();
    }

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
    cleanup_resources();
    exit(EXIT_SUCCESS);
}

void init_colors()
{
    if (has_colors())
    {
        start_color();
        use_default_colors();

        // Inicializar pares de colores para cada jugador
        for (int i = 0; i < 9; i++)
        {
            init_pair(i + 1, player_colors[i], -1);
        }

        // Par especial para celdas libres con recompensas
        init_pair(10, COLOR_GREEN, -1);
    }
}

void draw_board(game_state_t *state)
{
    int start_y = 2;
    int start_x = 2;

    // Dibujar el tablero
    for (int y = 0; y < state->board_height; y++)
    {
        move(start_y + y, start_x);
        for (int x = 0; x < state->board_width; x++)
        {
            int cell_value = state->board[y * state->board_width + x];

            if (cell_value >= 1 && cell_value <= 9)
            {
                // Celda libre con recompensa
                attron(COLOR_PAIR(10));
                printw("%d", cell_value);
                attroff(COLOR_PAIR(10));
            }
            else if (cell_value <= 0 && cell_value >= -8)
            {
                // Celda capturada por jugador
                int player_id = -cell_value;
                if (player_id < 9)
                {
                    attron(COLOR_PAIR(player_id + 1));
                    printw("#");
                    attroff(COLOR_PAIR(player_id + 1));
                }
            }
            else
            {
                printw("?"); // Valor inesperado
            }
            printw(" ");
        }
    }

    // Dibujar posiciones de jugadores
    for (unsigned int i = 0; i < state->player_count; i++)
    {
        player_t player = state->players[i];
        if (player.pos_x < state->board_width && player.pos_y < state->board_height)
        {
            move(start_y + player.pos_y, start_x + player.pos_x * 2);
            attron(COLOR_PAIR(i + 1) | A_BOLD);
            printw("P");
            attroff(COLOR_PAIR(i + 1) | A_BOLD);
        }
    }
}

void draw_scoreboard(game_state_t *state)
{
    int start_y = 2;
    int start_x = state->board_width * 2 + 5;

    move(start_y, start_x);
    printw("=== SCOREBOARD ===");

    for (unsigned int i = 0; i < state->player_count; i++)
    {
        move(start_y + 2 + i, start_x);

        if (state->players[i].is_blocked)
        {
            attron(COLOR_PAIR(i + 1) | A_DIM);
            printw("P%d: %s [BLOCKED]", i + 1, state->players[i].player_name);
        }
        else
        {
            attron(COLOR_PAIR(i + 1));
            printw("P%d: %s", i + 1, state->players[i].player_name);
        }
        attroff(COLOR_PAIR(i + 1) | A_DIM);

        // Mostrar puntuación y movimientos
        move(start_y + 2 + i, start_x + 20);
        printw("Score: %u", state->players[i].score);

        move(start_y + 2 + i, start_x + 35);
        printw("Moves: %u valid / %u invalid",
               state->players[i].valid_moves,
               state->players[i].invalid_moves);
    }
}

void draw_legend()
{
    int legend_y = LINES - 6;

    move(legend_y, 2);
    printw("=== LEGEND ===");
    move(legend_y + 1, 2);
    printw("P = Player position");
    move(legend_y + 2, 2);
    printw("# = Captured cell");
    move(legend_y + 3, 2);
    printw("1-9 = Free cells with rewards");
    move(legend_y + 4, 2);
    printw("Press 'q' to quit");
}

int main(int argc, char *argv[])
{
    // Registrar manejadores de señales
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Primero abrir memoria compartida de sincronización
    shm_sync_fd = open_shared_memory(GAME_SYNC_NAME, sizeof(game_sync_t), O_RDWR);
    if (shm_sync_fd == -1)
    {
        perror("open_shared_memory game_sync");
        fprintf(stderr, "View must be started by master process\n");
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
    game_state = map_shared_memory_readonly(shm_state_fd, shm_stat.st_size);
    if (game_state == NULL)
    {
        perror("map_shared_memory game_state");
        cleanup_resources();
        return EXIT_FAILURE;
    }

    // Actualizar las variables globales width y height
    width = game_state->board_width;
    height = game_state->board_height;

    // Configurar variable TERM si no está definida
    if (getenv("TERM") == NULL)
    {
        putenv("TERM=xterm-256color");
    }

    // Inicializar ncurses con manejo de errores
    if (initscr() == NULL)
    {
        fprintf(stderr, "Error initializing ncurses. Try setting TERM environment variable: export TERM=xterm\n");
        cleanup_resources();

        // En caso de error, notificar al master y salir
        if (game_sync != NULL)
        {
            notify_view_done(game_sync);
        }
        return EXIT_FAILURE;
    }

    cbreak();
    noecho();
    nodelay(stdscr, TRUE); // No bloquear en getch()
    keypad(stdscr, TRUE);
    curs_set(0); // Ocultar cursor

    init_colors();

    // Verificar tamaño mínimo de terminal
    if (LINES < height + 10 || COLS < width * 2 + 50)
    {
        cleanup_resources();
        fprintf(stderr, "Error: Terminal too small. Need at least %dx%d\n",
                width * 2 + 50, height + 10);
        return EXIT_FAILURE;
    }

    // Loop principal
    while (!game_state->game_over)
    {
        // Esperar notificación del master usando funciones de sync_utils.h
        wait_view_notification(game_sync);

        // Limpiar pantalla
        clear();

        // Dibujar título
        move(0, 2);
        attron(A_BOLD);
        printw("ChompChamps - Board: %dx%d", width, height);
        attroff(A_BOLD);

        // Lectura segura del estado del juego
        reader_enter(game_sync);

        // Dibujar el tablero y la información del juego
        draw_board(game_state);
        draw_scoreboard(game_state);
        draw_legend();

        reader_exit(game_sync);

        // Actualizar pantalla
        refresh();

        // Notificar al master que hemos terminado
        notify_view_done(game_sync);

        // Verificar si el usuario presionó 'q' para salir
        int ch = getch();
        if (ch == 'q' || ch == 'Q')
        {
            break;
        }

        // Pequeña pausa para no consumir CPU
        usleep(10000); // 10ms
    }

    cleanup_resources();
    return EXIT_SUCCESS;
} */