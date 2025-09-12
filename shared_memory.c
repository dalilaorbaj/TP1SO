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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

// Funciones básicas de memoria compartida

int create_shared_memory(const char* name, size_t size) { 
    int fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open create");
        return -1;
    }
    
    if (ftruncate(fd, size) == -1) {
        perror("ftruncate");
        close(fd);
        shm_unlink(name);
        return -1;
    }
    
    return fd;
}

int open_shared_memory(const char* name, size_t size, int flags) {
    int fd = shm_open(name, flags, 0666);
    if (fd == -1) {
        perror("shm_open");
        return -1;
    }
    
    return fd;
}


void* map_shared_memory(int fd, size_t size, bool readonly) {
    int prot = readonly ? PROT_READ : (PROT_READ | PROT_WRITE);
    
    void* ptr = mmap(NULL, size, prot, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }
    
    return ptr;
}

void unmap_shared_memory(void* ptr, size_t size) {
    if (munmap(ptr, size) == -1) {
        perror("munmap");
    }
}

void close_shared_memory(int fd) {
    if (close(fd) == -1) {
        perror("close shared memory fd");
    }
}

void unlink_shared_memory(const char* name) {
    if (shm_unlink(name) == -1) {
        perror("shm_unlink");
    }
}

// Funciones específicas para el estado del juego

size_t calculate_game_state_size(unsigned short width, unsigned short height) {
    return sizeof(game_state_t) + (width * height * sizeof(int));
}

game_state_t* create_game_state(unsigned short width, unsigned short height) {
    size_t size = calculate_game_state_size(width, height);
    
    int fd = create_shared_memory(GAME_STATE_NAME, size);
    if (fd == -1) {
        return NULL;
    }
    
    game_state_t* state = (game_state_t*)map_shared_memory(fd, size, false);
    if (state == NULL) {
        close_shared_memory(fd);
        unlink_shared_memory(GAME_STATE_NAME);
        return NULL;
    }
    
    // Inicializar el estado del juego
    state->board_width = width;
    state->board_height = height;
    state->player_count = 0;
    state->game_over = false;
    
    // Inicializar jugadores
    for (int i = 0; i < MAX_PLAYERS; i++) {
        memset(state->players[i].player_name, 0, MAX_NAME_LENGTH);
        state->players[i].score = 0;
        state->players[i].invalid_moves = 0;
        state->players[i].valid_moves = 0;
        state->players[i].pos_x = 0;
        state->players[i].pos_y = 0;
        state->players[i].pid = 0;
        state->players[i].is_blocked = false;
    }
    
    // Inicializar el tablero con recompensas aleatorias (1-9)
    for (int i = 0; i < width * height; i++) {
        state->board[i] = (rand() % 9) + 1;
    }
    
    close_shared_memory(fd);
    return state;
}

game_state_t* open_game_state(unsigned short width, unsigned short height) {
    size_t size = calculate_game_state_size(width, height);
    
    int fd = open_shared_memory(GAME_STATE_NAME, size, O_RDWR);
    if (fd == -1) {
        return NULL;
    }
    
    game_state_t* state = (game_state_t*)map_shared_memory(fd, size, false);
    if (state == NULL) {
        close_shared_memory(fd);
        return NULL;
    }
    
    close_shared_memory(fd);
    return state;
}

void close_game_state(game_state_t* state, unsigned short width, unsigned short height) {
    if (state != NULL) {
        size_t size = calculate_game_state_size(width, height);
        unmap_shared_memory(state, size);
    }
}

// Funciones específicas para la sincronización

game_sync_t* create_game_sync(unsigned int player_count) {
    size_t size = sizeof(game_sync_t);
    
    int fd = create_shared_memory(GAME_SYNC_NAME, size);
    if (fd == -1) {
        return NULL;
    }
    
    game_sync_t* sync = (game_sync_t*)map_shared_memory(fd, size, false);
    if (sync == NULL) {
        close_shared_memory(fd);
        unlink_shared_memory(GAME_SYNC_NAME);
        return NULL;
    }
    
    // Inicializar semáforos
    if (sem_init(&sync->update_view_sem, 1, 0) == -1) {
        perror("sem_init update_view_sem");
        goto cleanup;
    }
    
    if (sem_init(&sync->view_done_sem, 1, 0) == -1) {
        perror("sem_init view_done_sem");
        sem_destroy(&sync->update_view_sem);
        goto cleanup;
    }
    
    if (sem_init(&sync->master_access_mutex, 1, 1) == -1) {
        perror("sem_init master_access_mutex");
        sem_destroy(&sync->update_view_sem);
        sem_destroy(&sync->view_done_sem);
        goto cleanup;
    }
    
    if (sem_init(&sync->game_state_mutex, 1, 1) == -1) {
        perror("sem_init state_mutex");
        sem_destroy(&sync->update_view_sem);
        sem_destroy(&sync->view_done_sem);
        sem_destroy(&sync->master_access_mutex);
        goto cleanup;
    }
    
    if (sem_init(&sync->readers_count_mutex, 1, 1) == -1) {
        perror("sem_init readers_count_mutex");
        sem_destroy(&sync->update_view_sem);
        sem_destroy(&sync->view_done_sem);
        sem_destroy(&sync->master_access_mutex);
        sem_destroy(&sync->game_state_mutex);
        goto cleanup;
    }
    
    sync->active_readers = 0;
    
    // Inicializar semáforos de los jugadores
    for (unsigned int i = 0; i < player_count && i < MAX_PLAYERS; i++) {
        if (sem_init(&sync->player_move_sem[i], 1, 1) == -1) {
            perror("sem_init player_move_sem");
            // Limpiar semáforos ya inicializados
            for (unsigned int j = 0; j < i; j++) {
                sem_destroy(&sync->player_move_sem[j]);
            }
            sem_destroy(&sync->update_view_sem);
            sem_destroy(&sync->view_done_sem);
            sem_destroy(&sync->master_access_mutex);
            sem_destroy(&sync->game_state_mutex);
            sem_destroy(&sync->readers_count_mutex);
            goto cleanup;
        }
    }
    
    // Inicializar el resto de semáforos de jugadores con 0 (no usados)
    for (unsigned int i = player_count; i < MAX_PLAYERS; i++) {
        if (sem_init(&sync->player_move_sem[i], 1, 0) == -1) {
            perror("sem_init unused player_move_sem");
            // Limpiar todos los semáforos
            for (unsigned int j = 0; j < i; j++) {
                sem_destroy(&sync->player_move_sem[j]);
            }
            sem_destroy(&sync->update_view_sem);
            sem_destroy(&sync->view_done_sem);
            sem_destroy(&sync->master_access_mutex);
            sem_destroy(&sync->game_state_mutex);
            sem_destroy(&sync->readers_count_mutex);
            goto cleanup;
        }
    }
    
    close_shared_memory(fd);
    return sync;
    
cleanup:
    unmap_shared_memory(sync, size);
    close_shared_memory(fd);
    unlink_shared_memory(GAME_SYNC_NAME);
    return NULL;
}

game_sync_t* open_game_sync(void) {
    size_t size = sizeof(game_sync_t);
    
    int fd = open_shared_memory(GAME_SYNC_NAME, size, O_RDWR);
    if (fd == -1) {
        return NULL;
    }
    
    game_sync_t* sync = (game_sync_t*)map_shared_memory(fd, size, false);
    if (sync == NULL) {
        close_shared_memory(fd);
        return NULL;
    }
    
    close_shared_memory(fd);
    return sync;
}

void close_game_sync(game_sync_t* sync) {
    if (sync != NULL) {
        unmap_shared_memory(sync, sizeof(game_sync_t));
    }
}
