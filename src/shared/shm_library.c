#include "shm_library.h"

typedef struct {
    /* Master y Vista */
    sem_t A; // El máster le indica a la vista que hay cambios por imprimir
    sem_t B; // La vista le indica al máster que terminó de imprimir
    
    /* Master y Jugadores */
    sem_t C; // Mutex para evitar inanición del máster al acceder al estado
    sem_t D; // Mutex para el estado del juego
    sem_t E; // Mutex para la siguiente variable
    unsigned int F; // Cantidad de jugadores leyendo el estado
    sem_t G[9]; // Le indican a cada jugador que puede enviar 1 movimiento
} ZZZ;

