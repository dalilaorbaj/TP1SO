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
#include <ncurses.h>
#include <sys/stat.h>
#include <stdarg.h>

// Variables globales
game_state_t *game_state = NULL;
game_sync_t *game_sync = NULL;
int shm_state_fd = -1;
int shm_sync_fd = -1;
bool cleanup_done = false;
unsigned short width, height, player_count;

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

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

void print_colored_text(WINDOW *win, int y, int x, int color_pair, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    
    wattron(win, COLOR_PAIR(color_pair));
    wmove(win, y, x);
    vw_printw(win, format, args);
    wattroff(win, COLOR_PAIR(color_pair));
    
    va_end(args);
}

void init_colors(void)
{
    start_color();
    
    const int color_defs[][3] = {
        {COLOR_BOARD_BG, COLOR_BLACK, COLOR_GREEN},
        {COLOR_PLAYER1, COLOR_BLACK, COLOR_RED},
        {COLOR_PLAYER2, COLOR_BLACK, COLOR_BLUE},
        {COLOR_PLAYER3, COLOR_BLACK, COLOR_YELLOW},
        {COLOR_PLAYER4, COLOR_BLACK, COLOR_MAGENTA},
        {COLOR_SCORE, COLOR_WHITE, COLOR_BLACK},
        {COLOR_CAPTURED, COLOR_BLACK, COLOR_WHITE}
    };
    
    for (size_t i = 0; i < sizeof(color_defs) / sizeof(color_defs[0]); i++) {
        init_pair(color_defs[i][0], color_defs[i][1], color_defs[i][2]);
    }
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
                print_colored_text(win, pos_y, pos_x, color_pair, "P%u", player_id + 1);
            }
            else if (cell_value > 0)
            {
                // Mostrar el valor de la recompensa
                print_colored_text(win, pos_y, pos_x, COLOR_BOARD_BG, "%2d", cell_value);
            }

            else // cel_vallue <= 0
            {
                // Mostrar celda capturada con el color del jugador que la capturó
                int player_idx = -cell_value; // Convertimos el valor negativo al índice del jugador
                if (player_idx < state->player_count) {
                    // Color del jugador que capturó la celda
                    int color_pair = COLOR_PLAYER1 + (player_idx % 4);
                    print_colored_text(win, pos_y, pos_x, color_pair, "##");
                } else {
                    // Si por alguna razón no hay índice de jugador válido, usar el color de capturada por defecto
                    print_colored_text(win, pos_y, pos_x, COLOR_CAPTURED, "##");
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
        print_colored_text(win, i+1, 2, color_pair, "P%u", i+1);

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

void draw_legend(WINDOW *win, unsigned short player_count, player_t players[])
{
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " Legend ");
    
    print_colored_text(win, 1, 2, COLOR_BOARD_BG, "##");
    wprintw(win, " - Cell with points");

    print_colored_text(win, 2, 2, COLOR_CAPTURED, "##");
    wprintw(win, " - Captured cell");
    
    for (unsigned int i = 0; i < player_count; i++) {
        // Calculate color for player (cycling through available colors)
        int color_pair = COLOR_PLAYER1 + (i % 4);
        
        // Display player identifier on its own line
        int row = 3 + i;  // Start player entries from row 3
        print_colored_text(win, row, 2, color_pair, "P%u", i+1);
        wprintw(win, " - %s", players[i].player_name);
    }
    
    
    wrefresh(win);
}

int main(int argc, char *argv[])
{
    // Registrar manejadores de señales
    /*signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);*/
    setup_signal_handlers();


    // Abrir memoria compartida de sincronización (primero)
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
    game_state = map_shared_memory(shm_state_fd, shm_stat.st_size, true);
    if (game_state == NULL)
    {
        perror("map_shared_memory_readonly game_state");
        cleanup_resources();
        return EXIT_FAILURE;
    }

    // Actualizar las variables globales width, height y player_count
    player_count = game_state->player_count;
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

    
    
    int scoreboard_height = player_count + 2;  // +2 para los bordes
    int scoreboard_width = max_x - 2;  // Ancho casi completo
    
    int legend_height = player_count + 4;  // Alto fijo para la leyenda
    int legend_width = 30;  // Ancho fijo para la leyenda
    
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
    bool gameOver = false;
    while(1){
        // Esperar notificación del master
        wait_view_notification(game_sync);
        
        // Lectura segura del estado del juego
        reader_enter(game_sync);

        gameOver = game_state->game_over;
        if(gameOver) {
            reader_exit(game_sync);
            break;
        }
        
        // Actualizar la interfaz
        draw_board(board_win, game_state);
        draw_scoreboard(scoreboard_win, game_state);
        draw_legend(legend_win, player_count, game_state->players);
        
        doupdate();  // Actualizar todas las ventanas
        
        reader_exit(game_sync);
        
        // Notificar al master que hemos terminado
        notify_view_done(game_sync);
        
        // Pequeña pausa para no consumir CPU
        napms(50);
    };

    napms(2000);

    // Mostrar mensaje de fin de juego
    mvprintw(max_y-2, 1, "Game over! Press any key to exit...");
    refresh();
    getch();
    
    // Limpiar recursos y salir
    cleanup_resources();
    return EXIT_SUCCESS;
}