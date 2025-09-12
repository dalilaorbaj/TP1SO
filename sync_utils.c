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
#include "sync_utils.h"
#include <stdio.h>
#include <errno.h>

// Implementación del patrón lectores-escritores que evita inanición del escritor
// (?) esta bien?
void initialize_semaphores(game_sync_t* sync, int num_players) {
    // Inicializar semáforos de comunicación entre master y vista
    sem_init(&sync->update_view_sem, 1, 0);  // en lugar de sem_master_to_view
    sem_init(&sync->view_done_sem, 1, 0);    // en lugar de sem_view_to_master
    
    // Inicializar semáforos para el algoritmo de lectores-escritores con prioridad a escritor
    sem_init(&sync->master_access_mutex, 1, 1); // en lugar de sem_turnstile
    sem_init(&sync->game_state_mutex, 1, 1);    // en lugar de sem_state_lock
    sem_init(&sync->readers_count_mutex, 1, 1); // en lugar de sem_read_count_lock
    sync->active_readers = 0;                   // en lugar de readers_count
    
    // Inicializar semáforos para controlar a los jugadores
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        sem_init(&sync->player_move_sem[i], 1, 0); // en lugar de sem_player
    }
}

void writer_enter(game_sync_t* sync) {
    if (sem_wait(&sync->master_access_mutex) == -1) {
        perror("sem_wait master_access_mutex");
        return;
    }
    
    if (sem_wait(&sync->game_state_mutex) == -1) {
        perror("sem_wait game_state_mutex");
        sem_post(&sync->master_access_mutex);
        return;
    }
}

void writer_exit(game_sync_t* sync) {
    if (sem_post(&sync->game_state_mutex) == -1) {
        perror("sem_post game_state_mutex");
    }
    
    if (sem_post(&sync->master_access_mutex) == -1) {
        perror("sem_post master_access_mutex");
    }
}

void reader_enter(game_sync_t* sync) {
    if (sem_wait(&sync->master_access_mutex) == -1) {
        perror("sem_wait master_access_mutex (reader)");
        return;
    }
    
    if (sem_wait(&sync->readers_count_mutex) == -1) {
        perror("sem_wait readers_count_mutex");
        sem_post(&sync->master_access_mutex);
        return;
    }
    
    sync->active_readers++;
    
    if (sync->active_readers == 1) {
        if (sem_wait(&sync->game_state_mutex) == -1) {
            perror("sem_wait game_state_mutex (first reader)");
            sync->active_readers--;
            sem_post(&sync->readers_count_mutex);
            sem_post(&sync->master_access_mutex);
            return;
        }
    }
    
    if (sem_post(&sync->readers_count_mutex) == -1) {
        perror("sem_post readers_count_mutex");
    }
    
    if (sem_post(&sync->master_access_mutex) == -1) {
        perror("sem_post master_access_mutex (reader)");
    }
}

void reader_exit(game_sync_t* sync) {
    if (sem_wait(&sync->readers_count_mutex) == -1) {
        perror("sem_wait readers_count_mutex (exit)");
        return;
    }
    
    sync->active_readers--;
    
    if (sync->active_readers == 0) {
        if (sem_post(&sync->game_state_mutex) == -1) {
            perror("sem_post game_state_mutex (last reader)");
        }
    }
    
    if (sem_post(&sync->readers_count_mutex) == -1) {
        perror("sem_post readers_count_mutex (exit)");
    }
}

// Funciones para sincronización vista-master

void notify_view(game_sync_t* sync) {
    if (sem_post(&sync->update_view_sem) == -1) {
        perror("sem_post update_view_sem");
    }
}

void wait_view_done(game_sync_t* sync) {
    if (sem_wait(&sync->view_done_sem) == -1) {
        perror("sem_wait view_done_sem");
    }
}

void wait_view_notification(game_sync_t* sync) {
    if (sem_wait(&sync->update_view_sem) == -1) {
        perror("sem_wait update_view_sem");
    }
}

void notify_view_done(game_sync_t* sync) {
    if (sem_post(&sync->view_done_sem) == -1) {
        perror("sem_post view_done_sem");
    }
}

// Funciones para sincronización master-jugadores

void allow_player_move(game_sync_t* sync, int player_id) {
    if (player_id >= 0 && player_id < MAX_PLAYERS) {
        if (sem_post(&sync->player_move_sem[player_id]) == -1) {
            perror("sem_post player_move_sem");
        }
    }
}

void wait_player_turn(game_sync_t* sync, int player_id) {
    if (player_id >= 0 && player_id < MAX_PLAYERS) {
        if (sem_wait(&sync->player_move_sem[player_id]) == -1) {
            perror("sem_wait player_move_sem");
        }
    }
}


// Función para limpiar semáforos

void cleanup_semaphores(game_sync_t* sync, unsigned int player_count) {
    if (sync == NULL) return;
    
    sem_destroy(&sync->update_view_sem);
    sem_destroy(&sync->view_done_sem);
    sem_destroy(&sync->master_access_mutex);
    sem_destroy(&sync->game_state_mutex);
    sem_destroy(&sync->readers_count_mutex);
    
    for (unsigned int i = 0; i < MAX_PLAYERS; i++) {
        sem_destroy(&sync->player_move_sem[i]);
    }
}