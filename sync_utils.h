
#ifndef SYNC_UTILS_H
#define SYNC_UTILS_H

#include "shared_memory.h"

void initialize_semaphores(game_sync_t* sync, int num_players);

// Funciones para sincronización lectores-escritores (evita inanición del escritor)
void writer_enter(game_sync_t* sync);
void writer_exit(game_sync_t* sync);
void reader_enter(game_sync_t* sync);
void reader_exit(game_sync_t* sync);

// Funciones para sincronización vista-master
void notify_view(game_sync_t* sync);
void wait_view_done(game_sync_t* sync);
void wait_view_notification(game_sync_t* sync);
void notify_view_done(game_sync_t* sync);

// Funciones para sincronización master-jugadores
void allow_player_move(game_sync_t* sync, int player_id);
void wait_player_turn(game_sync_t* sync, int player_id);

// Función para limpiar todos los semáforos al finalizar
void cleanup_semaphores(game_sync_t* sync, unsigned int player_count);

#endif