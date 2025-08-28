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

// Desplazamientos de dirección (0 = arriba, 1 = arriba-derecha, ... sentido horario)
static const int DIR_OFFSETS[8][2] = {
    { 0, -1},  // 0: arriba
    { 1, -1},  // 1: arriba-derecha
    { 1,  0},  // 2: derecha
    { 1,  1},  // 3: abajo-derecha
    { 0,  1},  // 4: abajo
    {-1,  1},  // 5: abajo-izquierda
    {-1,  0},  // 6: izquierda
    {-1, -1}   // 7: arriba-izquierda
};

static void print_usage(const char *progname) {
    fprintf(stderr, "Uso: %s [-w ancho] [-h alto] [-d delay_ms] [-t timeout_s] [-s semilla] [-v ruta_vista] -p jugador1 [jugador2 ...]\n", progname);
}

static void cleanup_resources(game_state_t *state, game_sync_t *sync, int pipe_fds[][2], int num_players) {
    // Cerrar pipes
    for (int i = 0; i < num_players; ++i) {
        if (pipe_fds[i][0] >= 0) close(pipe_fds[i][0]);
        if (pipe_fds[i][1] >= 0) close(pipe_fds[i][1]);
    }
    
    // Usar funciones de librería para cleanup
    if (sync != NULL) {
        cleanup_semaphores(sync, num_players);
        close_game_sync(sync);
    }
    
    if (state != NULL) {
        close_game_state(state, state->board_width, state->board_height);
    }
    
    // Limpiar shared memory
    unlink_shared_memory(GAME_STATE_NAME);
    unlink_shared_memory(GAME_SYNC_NAME);
}

int main(int argc, char *argv[]) {
    // Parámetros por defecto
    unsigned short width = 10, height = 10;
    unsigned int delay_ms = 200;
    unsigned int timeout_s = 10;
    unsigned int seed = (unsigned int)time(NULL);
    char *view_path = NULL;
    char *player_paths[MAX_PLAYERS];
    int num_players = 0;

    // Procesar argumentos
    int opt;
    while ((opt = getopt(argc, argv, "w:h:d:t:s:v:p")) != -1) {
        switch (opt) {
            case 'w':
                width = (unsigned short)atoi(optarg);
                if (width < 10) width = 10;
                break;
            case 'h':
                height = (unsigned short)atoi(optarg);
                if (height < 10) height = 10;
                break;
            case 'd':
                delay_ms = (unsigned int)atoi(optarg);
                break;
            case 't':
                timeout_s = (unsigned int)atoi(optarg);
                break;
            case 's':
                seed = (unsigned int)atoi(optarg);
                break;
            case 'v':
                view_path = optarg;
                break;
            case 'p':
                // La opción -p será manejada fuera de getopt
                break;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
        //if (opt == 'p') break; // salir del bucle si encontramos -p
    }
    for (int i = optind; i < argc && num_players < MAX_PLAYERS; ++i) {
        if (argv[i][0] == '-') break; 
        player_paths[num_players++] = argv[i];
    }
    // (?) esta bien imprimir por consola y no por view?
    if (num_players < 1) {
        fprintf(stderr, "Error: Debe especificar al menos un jugador con -p\n");
        return EXIT_FAILURE;
    }
    if (num_players > MAX_PLAYERS) {
        fprintf(stderr, "Error: El máximo de jugadores permitidos es %d\n", MAX_PLAYERS);
        return EXIT_FAILURE;
    }

    // Crear memoria compartida para el estado del juego y sincronización
    game_state_t *state = create_game_state(width, height);
    if (state == NULL) {
        fprintf(stderr, "Error creating game state\n");
        return EXIT_FAILURE;
    }

    game_sync_t * game_sync = create_game_sync(num_players);
    if (game_sync == NULL) {
        fprintf(stderr, "Error creating game sync\n");
        close_game_state(state, width, height);
        return EXIT_FAILURE;
    }
    if (state == MAP_FAILED) {
        perror("mmap(/game_state)");
        shm_unlink("/game_state");
        shm_unlink("/game_sync");
        return EXIT_FAILURE;
    }
    if (game_sync == MAP_FAILED) {
        perror("mmap(/game_sync)");
        //munmap(state, state_size);
        shm_unlink("/game_state");
        shm_unlink("/game_sync");
        return EXIT_FAILURE;
    }

    // Inicializar estado del juego
    //(?) estos dos de aca abajo no se si hacen falta o ya vienen inicializados por create_game_state()
    //state->board_width = width;
    //state->board_height = height;
    state->player_count = num_players;
    state->game_over = false;
    // Posicionar jugadores de forma determinística con margen similar
    for (int i = 0; i < num_players; ++i) {
    player_t *p = &state->players[i];
    
    // Nombre (basename de la ruta del ejecutable)
    const char *path = player_paths[i];
    const char *basename = strrchr(path, '/');
    basename = (basename ? basename + 1 : path);
    strncpy(p->player_name, basename, MAX_NAME_LENGTH - 1);
    p->player_name[MAX_NAME_LENGTH - 1] = '\0';
    
    // Posicionar jugadores de forma determinística
    int grid_rows = (int)ceil(sqrt(num_players));
    if (grid_rows < 1) grid_rows = 1;
    int grid_cols = (int)ceil((double)num_players / grid_rows);
    if (grid_cols < 1) grid_cols = num_players;
    
    int row = i / grid_cols;
    int col = i % grid_cols;
    
    unsigned short px = (unsigned short)(((col + 1) * (long)state->board_width) / (grid_cols + 1));
    unsigned short py = (unsigned short)(((row + 1) * (long)state->board_height) / (grid_rows + 1));
    if (px >= state->board_width) px = state->board_width - 1;
    if (py >= state->board_height) py = state->board_height - 1;
    
    p->pos_x = px;
    p->pos_y = py;
    p->pid = 0;       // se asignará luego del fork
    p->is_blocked = false;
    }
    // Inicializar tablero con recompensas aleatorias (y marcar posiciones iniciales de jugadores)
    srand(seed);
    for (unsigned short y = 0; y < state->board_height; ++y) {
        for (unsigned short x = 0; x < state->board_width; ++x) {
            // Verificar si esta celda es posición inicial de algún jugador
            int found_idx = -1;
            for (int k = 0; k < state->player_count; ++k) {
                if (state->players[k].pos_x == x && state->players[k].pos_y == y) {
                    found_idx = k;
                    break;
                }
            }
            if (found_idx >= 0) {
                // Celda ocupada inicialmente por jugador found_idx
                state->board[y * state->board_width + x] = -found_idx;
            } else {
                // Celda libre: asignar recompensa aleatoria 1-9
                state->board[y * state->board_width + x] = (rand() % 9) + 1;
            }
        }
    }

    initialize_semaphores(game_sync, num_players);
    
    // Crear pipes y procesos jugador
    int pipe_fds[MAX_PLAYERS][2];
    pid_t player_pids[MAX_PLAYERS];
    for (int i = 0; i < num_players; ++i) {
        if (pipe(pipe_fds[i]) == -1) {
            perror("pipe");
            // Terminar procesos ya creados
            for (int j = 0; j < i; ++j) {
                kill(player_pids[j], SIGTERM);
            }
            cleanup_resources(state, game_sync, pipe_fds, i);
            return EXIT_FAILURE;
        }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(pipe_fds[i][0]);
            close(pipe_fds[i][1]);
            for (int j = 0; j < i; ++j) {
                kill(player_pids[j], SIGTERM);
            }
            cleanup_resources(state, game_sync, pipe_fds, i);
            return EXIT_FAILURE;
        }
        if (pid == 0) {
            // Proceso hijo (jugador i)
            dup2(pipe_fds[i][1], STDOUT_FILENO);
            close(pipe_fds[i][0]); // cerrar extremo lectura en el hijo
            close(pipe_fds[i][1]); // cerrar descriptor duplicado (ya duplicado en stdout)
            char w_arg[16], h_arg[16];
            snprintf(w_arg, sizeof(w_arg), "%hu", state->board_width);
            snprintf(h_arg, sizeof(h_arg), "%hu", state->board_height);
            execl(player_paths[i], player_paths[i], w_arg, h_arg, (char*)NULL);
            fprintf(stderr, "Error: no se pudo ejecutar %s: %s\n", player_paths[i], strerror(errno));
            _exit(127);
        }
        // Proceso padre
        player_pids[i] = pid;
        state->players[i].pid = pid;
        close(pipe_fds[i][1]); // cerrar extremo de escritura en el máster
    }

    // Crear proceso vista (si corresponde)
    pid_t view_pid = -1;
    bool has_view = (view_path != NULL);
    if (has_view) {
        pid_t vpid = fork();
        if (vpid < 0) {
            perror("fork (vista)");
            // Terminar jugadores en caso de fallo
            for (int j = 0; j < num_players; ++j) {
                kill(player_pids[j], SIGTERM);
            }
            cleanup_resources(state, game_sync, pipe_fds, num_players);
            return EXIT_FAILURE;
        }
        if (vpid == 0) {
            // Proceso hijo (vista)
            char w_arg[16], h_arg[16];
            snprintf(w_arg, sizeof(w_arg), "%hu", width);
            snprintf(h_arg, sizeof(h_arg), "%hu", height);
            execl(view_path, view_path, w_arg, h_arg, (char*)NULL);
            fprintf(stderr, "Error: no se pudo ejecutar vista %s: %s\n", view_path, strerror(errno));
            _exit(127);
        }
        view_pid = vpid;
    }

    // Imprimir estado inicial (si hay vista conectada)
    if (has_view) {
        notify_view(game_sync);
        wait_view_done(game_sync);
    }
    // Permitir que los jugadores comiencen a enviar movimientos (inicializar semáforos de jugadores)
    for (int i = 0; i < num_players; ++i) {
        allow_player_move(game_sync, i);
    }

    // Bucle principal de recepción de movimientos
    time_t last_valid_time = time(NULL);
    bool all_blocked_flag = false;
    int start_index = 0;
    while (true) {
        // Calcular tiempo restante para timeout
        time_t now = time(NULL);
        long elapsed = now - last_valid_time;
        long remaining = (long)timeout_s - elapsed;
        if (remaining <= 0) {
            break;
        }
        // Configurar conjunto de lectura para select()
        fd_set readfds;
        FD_ZERO(&readfds);
        int maxfd = -1;
        for (int i = 0; i < num_players; ++i) {
            if (pipe_fds[i][0] >= 0) {
                FD_SET(pipe_fds[i][0], &readfds);
                if (pipe_fds[i][0] > maxfd) {
                    maxfd = pipe_fds[i][0];
                }
            }
        }
        if (maxfd == -1) {
            // No quedan jugadores activos
            break;
        }
        struct timeval tv;
        tv.tv_sec = remaining;
        tv.tv_usec = 0;
        int res = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (res < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (res == 0) {
            // Se agotó el tiempo sin movimientos válidos
            break;
        }
        // Determinar qué pipe está listo usando round-robin
        int ready_index = -1;
        for (int j = 0; j < num_players; ++j) {
            int idx = (start_index + j) % num_players;
            if (pipe_fds[idx][0] >= 0 && FD_ISSET(pipe_fds[idx][0], &readfds)) {
                ready_index = idx;
                start_index = (idx + 1) % num_players;
                break;
            }
        }
        if (ready_index == -1) {
            // Ningún FD encontrado listo (no debería suceder si res > 0)
            continue;
        }
        int i = ready_index;
        // Leer un byte de movimiento del jugador i
        unsigned char move;
        ssize_t nread = read(pipe_fds[i][0], &move, 1);
        if (nread <= 0) {
            if (nread == 0) {
                // EOF: el jugador i terminó (cerró pipe)
                state->players[i].is_blocked  = true;
                close(pipe_fds[i][0]);
                pipe_fds[i][0] = -1;
                // Verificar si ahora todos los jugadores están bloqueados/inactivos
                bool all_blocked_now = true;
                for (int k = 0; k < state->player_count; ++k) {
                    if (!state->players[k].is_blocked) {
                        all_blocked_now = false;
                        break;
                    }
                }
                if (all_blocked_now) {
                    all_blocked_flag = true;
                    // No salimos inmediatamente para procesar impresión final
                }
                if (!all_blocked_flag) {
                    // Continuar si quedan jugadores activos
                    continue;
                }
            } else {
                perror("read");
                continue;
            }
        }
        if (nread <= 0) {
            // Salir del bucle si todos bloqueados debido a EOF
            if (all_blocked_flag) break;
            else continue;
        }
        unsigned char direction = move;
        // Tomar locks de escritura (evitar inanición del escritor)
        writer_enter(game_sync);
        bool move_valid = false;
        if (direction > 7) {
            // Movimiento fuera de rango
            state->players[i].invalid_moves++;
        } else {
            short dx = DIR_OFFSETS[direction][0];
            short dy = DIR_OFFSETS[direction][1];
            int cur_x = state->players[i].pos_x;
            int cur_y = state->players[i].pos_y;
            int new_x = cur_x + dx;
            int new_y = cur_y + dy;
            if (new_x < 0 || new_x >= state->board_width || new_y < 0 || new_y >= state->board_height) {
                // Se intenta salir del tablero
                state->players[i].invalid_moves++;
            } else {
                int target_val = state->board[new_y * state->board_width + new_x];
                if (target_val <= 0) {
                    // La celda destino no está libre (ocupada/capturada)
                    state->players[i].invalid_moves++;
                } else {
                    // Movimiento válido
                    move_valid = true;
                    state->players[i].valid_moves++;
                    state->players[i].score += (unsigned int)target_val;
                    state->players[i].pos_x = (unsigned short)new_x;
                    state->players[i].pos_y = (unsigned short)new_y;
                    state->board[new_y * state->board_width + new_x] = -(int)i;
                }
            }
        }
        // Actualizar estado de bloqueo de todos los jugadores

    for (int i = 0; i < state->player_count; ++i) {
        if (move_valid && !state->players[i].is_blocked) {
            bool any_free = false;
            for (int d = 0; d < 8; ++d) {
                int nx = state->players[i].pos_x + DIR_OFFSETS[d][0];
                int ny = state->players[i].pos_y + DIR_OFFSETS[d][1];
                if (nx >= 0 && nx < state->board_width && ny >= 0 && ny < state->board_height) {
                    if (state->board[ny * state->board_width + nx] > 0) {
                        any_free = true;
                        break;
                    }
                }
            }
            if (!any_free) {
                state->players[i].is_blocked = true;
            }
        }
    }

        // Verificar si todos están bloqueados (función separada)
        bool all_blocked_now = true;
        for (int k = 0; k < state->player_count; ++k) {
            if (!state->players[k].is_blocked) {
                all_blocked_now = false;
                break;
            }
        }
        // Revisar condición de fin de juego (todos bloqueados)
        for (int k = 0; k < state->player_count; ++k) {
            if (!state->players[k].is_blocked) {
                all_blocked_now = false;
                break;
            }
        }
        if (all_blocked_now) {
            all_blocked_flag = true;
        }
        // Liberar locks de escritura
        writer_exit(game_sync);

        // Actualizar temporizador de último movimiento válido
        if (move_valid) {
            last_valid_time = time(NULL);
        }
        // Notificar al jugador i que se procesó su movimiento
        if (pipe_fds[i][0] >= 0) {
            allow_player_move(game_sync, i);

        }
        // Notificar a la vista del cambio de estado (si está presente)
        if (has_view) {
            notify_view(game_sync);
            wait_view_done(game_sync);
        }
        // Esperar el delay configurado antes de procesar próximo movimiento
        if (delay_ms > 0) {
            usleep(delay_ms * 1000);
        }
        // Si se alcanzó la condición de fin, salir del bucle
        if (all_blocked_flag) {
            break;
        }
    }

    // Fin del juego: marcar en el estado y notificar
    state->game_over = true;
    if (has_view) {
        notify_view(game_sync);
        wait_view_done(game_sync);
    }
    // Despertar a jugadores que puedan estar esperando permiso, para que salgan
    for (int i = 0; i < num_players; ++i) {
        if (pipe_fds[i][0] >= 0) {
            allow_player_move(game_sync, i);
        }
    }

    if (has_view) {
        int status;
        waitpid(view_pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("View exited (%d)\n", WEXITSTATUS(status));
            for (int i = 0; i < num_players; ++i) {
                int status;
                waitpid(player_pids[i], &status, 0);
                printf("Player %s (%d) exited (%d) with a score of %u / %u valid moves / %u invalid moves\n", 
                state->players[i].player_name, i, WEXITSTATUS(status), //(?) no estoy segura de esto de weixtstatus(status), le aparece a todos los players 136 que no se si esta bien 
                state->players[i].score, 
                state->players[i].valid_moves, 
                state->players[i].invalid_moves);
            }
        } else if (WIFSIGNALED(status)) {
            printf("View terminated by signal (%d)\n", WTERMSIG(status));
        } else {
            printf("View terminated abnormally\n");
        }
    }

    // Limpieza final de recursos
    cleanup_resources(state, game_sync, pipe_fds, num_players);
    return EXIT_SUCCESS;
}