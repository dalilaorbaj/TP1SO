#include "shared_memory.h"
#include "sync_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <ncurses.h>
#include <sys/stat.h>

// Variables globales
game_state_t *game_state = NULL;
game_sync_t *game_sync = NULL;
int shm_state_fd = -1;
int shm_sync_fd = -1;
bool cleanup_done = false;
unsigned short width, height;

// Colores para ncurses
#define COLOR_BOARD_BG 1
#define COLOR_PLAYER1 2
#define COLOR_PLAYER2 3
#define COLOR_PLAYER3 4
#define COLOR_PLAYER4 5
#define COLOR_SCORE 6
#define COLOR_CAPTURED 7

void cleanup_resources(void)
{
    if (cleanup_done)
        return;
    cleanup_done = true;

    // Finalizar ncurses
    if (stdscr != NULL)
        endwin();

    // Desconectar memorias compartidas
    if (game_state != NULL && width > 0 && height > 0) //no debería usar los width y height de game_state?
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

void init_colors(void)
{
    start_color();
    init_pair(COLOR_BOARD_BG, COLOR_BLACK, COLOR_GREEN);
    init_pair(COLOR_PLAYER1, COLOR_BLACK, COLOR_RED);
    init_pair(COLOR_PLAYER2, COLOR_BLACK, COLOR_BLUE);
    init_pair(COLOR_PLAYER3, COLOR_BLACK, COLOR_YELLOW);
    init_pair(COLOR_PLAYER4, COLOR_BLACK, COLOR_MAGENTA);
    init_pair(COLOR_SCORE, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_CAPTURED, COLOR_BLACK, COLOR_WHITE);
}

void draw_board(WINDOW *win, game_state_t *state)
{
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " ChompChamps Board ");

    // Calcular el ancho de cada celda (3 caracteres)
    int cell_width = 3;
    
    // Dibujar el tablero
    for (int y = 0; y < state->board_height; y++)
    {
        for (int x = 0; x < state->board_width; x++)
        {
            int cell_value = state->board[y * state->board_width + x];
            int pos_x = 2 + (x * cell_width);
            int pos_y = 1 + y;
            
            // Verificar si hay un jugador en esta posición
            bool player_here = false;
            int player_id = -1;
            for (unsigned int i = 0; i < state->player_count; i++)
            {
                if (state->players[i].pos_x == x && state->players[i].pos_y == y)
                {
                    player_here = true;
                    player_id = i;
                    break;
                }
            }
            
            if (player_here)
            {
                // Mostrar el identificador del jugador con color según su ID
                int color_pair = COLOR_PLAYER1 + (player_id % 4);
                wattron(win, COLOR_PAIR(color_pair));
                mvwprintw(win, pos_y, pos_x, "P%d", player_id + 1);
                wattroff(win, COLOR_PAIR(color_pair));
            }
            else if (cell_value > 0)
            {
                // Mostrar el valor de la recompensa
                wattron(win, COLOR_PAIR(COLOR_BOARD_BG));
                mvwprintw(win, pos_y, pos_x, "%2d", cell_value);
                wattroff(win, COLOR_PAIR(COLOR_BOARD_BG));
            }

            else if (cell_value <= 0)
            {
                // Mostrar celda capturada con el color del jugador que la capturó
                int player_idx = -cell_value; // Convertimos el valor negativo al índice del jugador
                if (player_idx >= 0 && player_idx < state->player_count) {
                    // Color del jugador que capturó la celda
                    int color_pair = COLOR_PLAYER1 + (player_idx % 4);
                    wattron(win, COLOR_PAIR(color_pair));
                    mvwprintw(win, pos_y, pos_x, "##");
                    wattroff(win, COLOR_PAIR(color_pair));
                } else {
                    // Si por alguna razón no hay índice de jugador válido, usar el color de capturada por defecto
                    wattron(win, COLOR_PAIR(COLOR_CAPTURED));
                    mvwprintw(win, pos_y, pos_x, "##");
                    wattroff(win, COLOR_PAIR(COLOR_CAPTURED));
                }
            }
        }
    }
    
    wrefresh(win);
}

void draw_scoreboard(WINDOW *win, game_state_t *state)
{
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " Scoreboard ");

    wattron(win, COLOR_PAIR(COLOR_SCORE));
    
    for (unsigned int i = 0; i < state->player_count; i++)
    {
        int color_pair = COLOR_PLAYER1 + (i % 4);
        wattron(win, COLOR_PAIR(color_pair));
        mvwprintw(win, i+1, 2, "P%d", i+1);
        wattroff(win, COLOR_PAIR(color_pair));
        
        wattron(win, COLOR_PAIR(COLOR_SCORE));
        mvwprintw(win, i+1, 5, "%-15s Score: %4u  Moves: %3u/%3u %s", 
                 state->players[i].player_name,
                 state->players[i].score,
                 state->players[i].valid_moves,
                 state->players[i].invalid_moves,
                 state->players[i].is_blocked ? "[BLOCKED]" : "");
    }
    
    wattroff(win, COLOR_PAIR(COLOR_SCORE));
    wrefresh(win);
}

void draw_legend(WINDOW *win)
{
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " Legend ");
    
    wattron(win, COLOR_PAIR(COLOR_BOARD_BG));
    mvwprintw(win, 1, 2, "##");
    wattroff(win, COLOR_PAIR(COLOR_BOARD_BG));
    wprintw(win, " - Cell with points");
    
    wattron(win, COLOR_PAIR(COLOR_CAPTURED));
    mvwprintw(win, 2, 2, "##");
    wattroff(win, COLOR_PAIR(COLOR_CAPTURED));
    wprintw(win, " - Captured cell");
    
    wattron(win, COLOR_PAIR(COLOR_PLAYER1));
    mvwprintw(win, 3, 2, "P1");
    wattroff(win, COLOR_PAIR(COLOR_PLAYER1));
    wprintw(win, " - Player 1");
    
    if (game_state && game_state->player_count > 1) {
        wattron(win, COLOR_PAIR(COLOR_PLAYER2));
        mvwprintw(win, 4, 2, "P2");
        wattroff(win, COLOR_PAIR(COLOR_PLAYER2));
        wprintw(win, " - Player 2");
    }
    
    wrefresh(win);
}

int main(int argc, char *argv[])
{
    // Registrar manejadores de señales
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Abrir memoria compartida de sincronización (primero)
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

    // Abrir la memoria compartida del estado del juego
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
        perror("map_shared_memory_readonly game_state");
        cleanup_resources();
        return EXIT_FAILURE;
    }

    // Actualizar las variables globales width y height
    width = game_state->board_width;
    height = game_state->board_height;

    // Inicializar ncurses con configuración robusta para Docker
    if (getenv("TERM") == NULL) {
        putenv("TERM=xterm-256color"); // Establecer TERM si no está definido
    }
    
    // Inicializar ncurses
    initscr();
    if (!has_colors()) {
        endwin();
        fprintf(stderr, "Your terminal does not support colors\n");
        cleanup_resources();
        return EXIT_FAILURE;
    }
    
    cbreak();
    noecho();
    curs_set(0);  // Ocultar el cursor
    init_colors();
    
    // Crear ventanas
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int board_height = height + 2;  // +2 para los bordes
    int board_width = width * 3 + 4;  // 3 caracteres por celda + bordes
    
    int scoreboard_height = game_state->player_count + 2;  // +2 para los bordes
    int scoreboard_width = max_x - 2;  // Ancho casi completo
    
    int legend_height = 6;  // Alto fijo para la leyenda
    int legend_width = 25;  // Ancho fijo para la leyenda
    
    WINDOW *board_win = newwin(board_height, board_width, 1, (max_x - board_width) / 2);
    WINDOW *scoreboard_win = newwin(scoreboard_height, scoreboard_width, board_height + 1, 1);
    WINDOW *legend_win = newwin(legend_height, legend_width, board_height + scoreboard_height + 2, 1);
    
    if (!board_win || !scoreboard_win || !legend_win) {
        endwin();
        fprintf(stderr, "Failed to create windows\n");
        cleanup_resources();
        return EXIT_FAILURE;
    }
    
    // Notificar al master que estamos listos
    notify_view_done(game_sync);
    
    // Loop principal
    while (!game_state->game_over)
    {
        // Esperar notificación del master
        wait_view_notification(game_sync);
        
        // Lectura segura del estado del juego
        reader_enter(game_sync);
        
        // Actualizar la interfaz
        draw_board(board_win, game_state);
        draw_scoreboard(scoreboard_win, game_state);
        draw_legend(legend_win);
        
        doupdate();  // Actualizar todas las ventanas
        
        reader_exit(game_sync);
        
        // Notificar al master que hemos terminado
        notify_view_done(game_sync);
        
        // Pequeña pausa para no consumir CPU
        napms(50);
    }

    napms(2000);

    // Mostrar mensaje de fin de juego
    mvprintw(max_y-2, 1, "Game over! Press any key to exit...");
    refresh();
    getch();
    
    // Limpiar recursos y salir
    cleanup_resources();
    return EXIT_SUCCESS;
}