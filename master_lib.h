#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/select.h>
#include <semaphore.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <math.h>
#include "shared_memory.h"
#include "sync_utils.h"

// Función para parsear argumentos del master
int parse_arguments(int argc, char *argv[], unsigned short *width, unsigned short *height, unsigned int *delay_ms, unsigned int *timeout_s, unsigned int *seed, char **view_path, char *player_paths[], int *num_players);

void cleanup_resources(game_state_t *state, game_sync_t *sync, int pipe_fds[][2], int num_players);

void initialize_game_state(game_state_t *state, char *player_paths[MAX_PLAYERS], int num_players, unsigned int seed);

/**
 * Crea los procesos jugador y configura sus pipes.
 * 
 * @param state Puntero al estado del juego
 * @param game_sync Puntero a la estructura de sincronización
 * @param player_paths Array de rutas a los ejecutables de los jugadores
 * @param num_players Número de jugadores
 * @param pipe_fds Array bidimensional donde se almacenarán los descriptores de los pipes
 * @param player_pids Array donde se almacenarán los PIDs de los procesos jugador
 * 
 * @return 0 en caso de éxito, -1 en caso de error
 */
int create_player_processes(game_state_t *state, game_sync_t *game_sync, char *player_paths[], int num_players, int pipe_fds[][2], pid_t player_pids[]);

/**
 * Crea el proceso de vista si se especificó una ruta válida.
 * 
 * @param state Estado del juego
 * @param game_sync Estructura de sincronización del juego
 * @param view_path Ruta al ejecutable de vista (NULL si no se usa vista)
 * @param width Ancho del tablero
 * @param height Alto del tablero
 * @param player_pids Array de PIDs de los jugadores (para limpieza en caso de error)
 * @param num_players Número de jugadores
 * @param pipe_fds Array de pipes de los jugadores (para limpieza en caso de error)
 * 
 * @return PID del proceso vista si se creó correctamente, -1 en caso contrario
 */
pid_t create_view_process(game_state_t *state, game_sync_t *game_sync, char *view_path, unsigned short width, unsigned short height, pid_t player_pids[], int num_players, int pipe_fds[][2]);

long calculate_remaining_time(time_t last_valid_time, unsigned int timeout_s);

int configure_fd_set(fd_set *readfds, int pipe_fds[][2], int num_players);

/**
 * Procesa el movimiento de un jugador y actualiza el estado del juego.
 * 
 * @param state Puntero al estado del juego
 * @param player_idx Índice del jugador que realiza el movimiento
 * @param direction Dirección del movimiento (0-7)
 * @param dir_offsets Matriz de desplazamientos para cada dirección
 * 
 * @return true si el movimiento fue válido, false en caso contrario
 */
bool process_player_move(game_state_t *state, int player_idx, unsigned char direction, const int dir_offsets[8][2]);

void update_lock_status(game_state_t *state, const int DIR_OFFSETS[8][2], bool move_valid);

// Parámetros: state
// Retorna: true si todos están bloqueados, false si no
bool all_players_blocked(game_state_t *state);

/**
 * Maneja las operaciones posteriores al procesamiento de un movimiento.
 * Actualiza temporizadores, notifica a jugadores y vista, y gestiona delays.
 * 
 * @param game_sync Estructura de sincronización
 * @param has_view Indica si hay un proceso de vista activo
 * @param pipe_fds Array de descriptores de pipes de los jugadores
 * @param i Índice del jugador actual
 * @param move_valid Indica si el movimiento fue válido
 * @param delay_ms Tiempo de espera entre movimientos (ms)
 * @param all_blocked_flag Puntero a la bandera que indica si todos los jugadores están bloqueados
 * @param last_valid_time Puntero al tiempo del último movimiento válido
 * 
 * @return true si se debe continuar el juego, false si se debe terminar
 */
bool handle_move_aftermath(game_sync_t *game_sync, bool has_view, int pipe_fds[][2], int i, bool move_valid, unsigned int delay_ms, bool all_blocked_flag, time_t *last_valid_time);

/**
 * Finaliza el juego, notifica a los procesos, muestra resultados y libera recursos.
 * 
 * @param state Estado del juego
 * @param game_sync Estructura de sincronización
 * @param has_view Indica si hay un proceso de vista activo
 * @param view_pid PID del proceso vista (si existe)
 * @param pipe_fds Array de descriptores de pipes de los jugadores
 * @param player_pids Array de PIDs de los procesos jugador
 * @param num_players Número de jugadores
 * 
 * @return El código de estado de salida (EXIT_SUCCESS o EXIT_FAILURE)
 */
int finalize_game(game_state_t *state, game_sync_t *game_sync, bool has_view, pid_t view_pid, int pipe_fds[][2], pid_t player_pids[], int num_players);

int check_game_status(game_state_t *state);

int check_game_sync(game_sync_t *game_sync, game_state_t *state, unsigned short width, unsigned short height);

int search_pipe_ready(fd_set *readfds, int pipe_fds[][2], int num_players, int *start_index);