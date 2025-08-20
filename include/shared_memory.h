#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdbool.h>
#include <sys/types.h>

#define GAME_STATE_NAME "/game_state"
#define GAME_SYNC_NAME "/game_sync"
#define MAX_NAME_LENGTH 16
#define MAX_PLAYERS 9


/* Las siguientes estructuras son almacenadas en una memoria compartida cuyo nombre es “/game_state” */
typedef struct
{
    char player_name[MAX_NAME_LENGTH]; // Nombre del jugador
    unsigned int score;                // Puntaje
    unsigned int invalid_moves;        // Cantidad de solicitudes de movimientos inválidas realizadas
    unsigned int valid_moves;          // Cantidad de solicitudes de movimientos válidas realizadas
    unsigned short pos_x, pos_y;       // Coordenadas x e y en el tablero
    pid_t pid;                         // Identificador de proceso
    bool is_blocked;                   // Indica si el jugador está bloqueado
} player_t;

typedef struct
{
    unsigned short board_width;    // Ancho del tablero
    unsigned short board_height;   // Alto del tablero
    unsigned int player_count;     // Cantidad de jugadores
    player_t players[MAX_PLAYERS]; // Lista de jugadores
    bool game_over;                // Indica si el juego se ha terminado
    int board[];                   // Puntero al comienzo del tablero. fila-0, fila-1, ..., fila-n-1
} game_state_t;

/* La siguiente estructura se almacena en una memoria compartida cuyo nombre es “/game_sync”*/
typedef struct
{
    /* Master y Vista */
    sem_t update_view_sem; // El máster le indica a la vista que hay cambios por imprimir
    sem_t view_done_sem;   // La vista le indica al máster que terminó de imprimir

    /* Master y Jugadores */
    sem_t master_access_mutex;          // Mutex para evitar inanición del máster al acceder al estado
    sem_t game_state_mutex;             // Mutex para el estado del juego
    sem_t readers_count_mutex;          // Mutex para la siguiente variable
    unsigned int active_readers;        // Cantidad de jugadores leyendo el estado
    sem_t player_move_sem[MAX_PLAYERS]; // Le indican a cada jugador que puede enviar 1 movimiento
} game_sync_t;


// Funciones para crear y abrir memoria compartida
int create_shared_memory(const char *name, size_t size);
int open_shared_memory(const char *name, size_t size, int flags);
void *map_shared_memory(int fd, size_t size);
void unmap_shared_memory(void *ptr, size_t size);
void close_shared_memory(int fd);
void unlink_shared_memory(const char *name);

// Funciones específicas para el juego
game_state_t* create_game_state(unsigned short width, unsigned short height);
game_state_t* open_game_state(unsigned short width, unsigned short height);
void close_game_state(game_state_t* state, unsigned short width, unsigned short height);

game_sync_t* create_game_sync(unsigned int player_count);
game_sync_t* open_game_sync(void);
void close_game_sync(game_sync_t* sync);

// Funciones de utilidad para calcular tamaños
size_t calculate_game_state_size(unsigned short width, unsigned short height);

#endif