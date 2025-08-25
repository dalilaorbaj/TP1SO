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

#define MAX_PLAYERS 9
#define NAME_MAX_LEN 16

typedef struct {
    char name[NAME_MAX_LEN];
    unsigned int score;
    unsigned int invalid_moves;
    unsigned int valid_moves;
    unsigned short x;
    unsigned short y;
    pid_t pid;
    bool blocked;
} player_t;

typedef struct {
    unsigned short width;
    unsigned short height;
    unsigned int num_players;
    player_t players[MAX_PLAYERS];
    bool game_finished;
    int board[];  // Flexible array member for board cells
} game_state_t;

typedef struct {
    sem_t sem_master_to_view;   // A: Máster indica a vista que hay cambios por imprimir
    sem_t sem_view_to_master;   // B: Vista indica a máster que terminó de imprimir
    sem_t sem_turnstile;        // C: Mutex para evitar inanición del máster (prioridad de escritor)
    sem_t sem_state_lock;       // D: Mutex para acceso exclusivo al estado del juego
    sem_t sem_read_count_lock;  // E: Mutex para proteger el contador de lectores
    unsigned int readers_count; // F: Cantidad de procesos leyendo el estado actualmente
    sem_t sem_player[MAX_PLAYERS]; // G: Semáforos para controlar envíos de movimiento (uno por jugador)
} sync_t;

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

static void cleanup_resources(game_state_t *state, size_t state_size, sync_t *sync, size_t sync_size, int state_fd, int sync_fd, int pipe_fds[][2], int num_players) {
    // Cerrar extremos de pipes abiertos
    for (int i = 0; i < num_players; ++i) {
        if (pipe_fds[i][0] >= 0) close(pipe_fds[i][0]);
        if (pipe_fds[i][1] >= 0) close(pipe_fds[i][1]);
    }
    // Destruir semáforos en memoria compartida
    if (sync != NULL) {
        sem_destroy(&sync->sem_master_to_view);
        sem_destroy(&sync->sem_view_to_master);
        sem_destroy(&sync->sem_turnstile);
        sem_destroy(&sync->sem_state_lock);
        sem_destroy(&sync->sem_read_count_lock);
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            sem_destroy(&sync->sem_player[i]);
        }
    }
    // Desmapear memorias compartidas
    if (state != NULL) munmap(state, state_size);
    if (sync != NULL) munmap(sync, sync_size);
    // Cerrar fds de memoria compartida
    if (state_fd >= 0) close(state_fd);
    if (sync_fd >= 0) close(sync_fd);
    // Desvincular objetos de memoria compartida
    shm_unlink("/game_state");
    shm_unlink("/game_sync");
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
    // Si getopt no manejó -p, buscar manualmente la opción -p
    /*
    if (opt != 'p') {
        for (int i = optind; i < argc; ++i) {
            if (strcmp(argv[i], "-p") == 0) {
                optind = i + 1;
                break;
            }
        }
    }
    if (optind >= argc || strcmp(argv[optind], "-p") == 0) {
        // No se especificaron jugadores correctamente
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    // Saltar "-p" si está presente en optind (por seguridad)
    if (strcmp(argv[optind], "-p") == 0) {
        optind++;
    }
    if (optind >= argc) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }*/
    for (int i = optind; i < argc && num_players < MAX_PLAYERS; ++i) {
        if (argv[i][0] == '-') break; 
        player_paths[num_players++] = argv[i];
    }
    if (num_players < 1) {
        fprintf(stderr, "Error: Debe especificar al menos un jugador con -p\n");
        return EXIT_FAILURE;
    }
    if (num_players > MAX_PLAYERS) {
        fprintf(stderr, "Error: El máximo de jugadores permitidos es %d\n", MAX_PLAYERS);
        return EXIT_FAILURE;
    }

    // Crear memoria compartida para el estado del juego y sincronización
    size_t state_size = sizeof(game_state_t) + width * height * sizeof(int);
    size_t sync_size = sizeof(sync_t);
    int state_fd = shm_open("/game_state", O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (state_fd < 0) {
        perror("shm_open(/game_state)");
        return EXIT_FAILURE;
    }
    int sync_fd = shm_open("/game_sync", O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (sync_fd < 0) {
        perror("shm_open(/game_sync)");
        shm_unlink("/game_state");
        return EXIT_FAILURE;
    }
    if (ftruncate(state_fd, state_size) == -1) {
        perror("ftruncate(/game_state)");
        shm_unlink("/game_state");
        shm_unlink("/game_sync");
        return EXIT_FAILURE;
    }
    if (ftruncate(sync_fd, sync_size) == -1) {
        perror("ftruncate(/game_sync)");
        shm_unlink("/game_state");
        shm_unlink("/game_sync");
        return EXIT_FAILURE;
    }
    game_state_t *state = mmap(NULL, state_size, PROT_READ | PROT_WRITE, MAP_SHARED, state_fd, 0);
    if (state == MAP_FAILED) {
        perror("mmap(/game_state)");
        shm_unlink("/game_state");
        shm_unlink("/game_sync");
        return EXIT_FAILURE;
    }
    sync_t *sync = mmap(NULL, sync_size, PROT_READ | PROT_WRITE, MAP_SHARED, sync_fd, 0);
    if (sync == MAP_FAILED) {
        perror("mmap(/game_sync)");
        munmap(state, state_size);
        shm_unlink("/game_state");
        shm_unlink("/game_sync");
        return EXIT_FAILURE;
    }

    // Inicializar estado del juego
    state->width = width;
    state->height = height;
    state->num_players = num_players;
    state->game_finished = false;
    // Posicionar jugadores de forma determinística con margen similar
    int grid_rows = (int)ceil(sqrt(num_players));
    if (grid_rows < 1) grid_rows = 1;
    int grid_cols = (int)ceil((double)num_players / grid_rows);
    if (grid_cols < 1) grid_cols = num_players;
    int placed = 0;
    for (int r = 0; r < grid_rows && placed < num_players; ++r) {
        for (int c = 0; c < grid_cols && placed < num_players; ++c) {
            // Calcular posición proporcionalmente dentro de una grilla imaginaria
            unsigned short px = (unsigned short)(((c + 1) * (long)width) / (grid_cols + 1));
            unsigned short py = (unsigned short)(((r + 1) * (long)height) / (grid_rows + 1));
            if (px >= width) px = width - 1;
            if (py >= height) py = height - 1;
            player_t *p = &state->players[placed];
            // Nombre (basename de la ruta del ejecutable)
            const char *path = player_paths[placed];
            const char *basename = strrchr(path, '/');
            basename = (basename ? basename + 1 : path);
            strncpy(p->name, basename, NAME_MAX_LEN - 1);
            p->name[NAME_MAX_LEN - 1] = '\0';
            p->score = 0;
            p->invalid_moves = 0;
            p->valid_moves = 0;
            p->x = px;
            p->y = py;
            p->pid = 0;       // se asignará luego del fork
            p->blocked = false;
            placed++;
        }
    }
    // Inicializar tablero con recompensas aleatorias (y marcar posiciones iniciales de jugadores)
    srand(seed);
    for (unsigned short y = 0; y < height; ++y) {
        for (unsigned short x = 0; x < width; ++x) {
            // Verificar si esta celda es posición inicial de algún jugador
            int found_idx = -1;
            for (int k = 0; k < num_players; ++k) {
                if (state->players[k].x == x && state->players[k].y == y) {
                    found_idx = k;
                    break;
                }
            }
            if (found_idx >= 0) {
                // Celda ocupada inicialmente por jugador found_idx (marcar capturada por ese jugador)
                state->board[y * width + x] = -found_idx;
            } else {
                // Celda libre: asignar recompensa aleatoria 1-9
                state->board[y * width + x] = (rand() % 9) + 1;
            }
        }
    }
    // Inicializar semáforos de sincronización
    sem_init(&sync->sem_master_to_view, 1, 0);
    sem_init(&sync->sem_view_to_master, 1, 0);
    sem_init(&sync->sem_turnstile, 1, 1);
    sem_init(&sync->sem_state_lock, 1, 1);
    sem_init(&sync->sem_read_count_lock, 1, 1);
    sync->readers_count = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        sem_init(&sync->sem_player[i], 1, 0);
    }

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
            cleanup_resources(state, state_size, sync, sync_size, state_fd, sync_fd, pipe_fds, i);
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
            cleanup_resources(state, state_size, sync, sync_size, state_fd, sync_fd, pipe_fds, i);
            return EXIT_FAILURE;
        }
        if (pid == 0) {
            // Proceso hijo (jugador i)
            dup2(pipe_fds[i][1], STDOUT_FILENO);
            close(pipe_fds[i][0]); // cerrar extremo lectura en el hijo
            close(pipe_fds[i][1]); // cerrar descriptor duplicado (ya duplicado en stdout)
            char w_arg[16], h_arg[16];
            snprintf(w_arg, sizeof(w_arg), "%hu", width);
            snprintf(h_arg, sizeof(h_arg), "%hu", height);
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
            cleanup_resources(state, state_size, sync, sync_size, state_fd, sync_fd, pipe_fds, num_players);
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
        sem_post(&sync->sem_master_to_view);   // avisar a vista que hay estado para mostrar
        sem_wait(&sync->sem_view_to_master);   // esperar a que la vista termine de imprimir
    }
    // Permitir que los jugadores comiencen a enviar movimientos (inicializar semáforos de jugadores)
    for (int i = 0; i < num_players; ++i) {
        sem_post(&sync->sem_player[i]);
    }

    // Bucle principal de recepción de movimientos
    time_t last_valid_time = time(NULL);
    bool timeout_flag = false;
    bool all_blocked_flag = false;
    int start_index = 0;
    while (true) {
        // Calcular tiempo restante para timeout
        time_t now = time(NULL);
        long elapsed = now - last_valid_time;
        long remaining = (long)timeout_s - elapsed;
        if (remaining <= 0) {
            timeout_flag = true;
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
            timeout_flag = true;
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
                state->players[i].blocked = true;
                close(pipe_fds[i][0]);
                pipe_fds[i][0] = -1;
                // Verificar si ahora todos los jugadores están bloqueados/inactivos
                bool all_blocked_now = true;
                for (int k = 0; k < num_players; ++k) {
                    if (!state->players[k].blocked) {
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
        sem_wait(&sync->sem_turnstile);
        sem_wait(&sync->sem_state_lock);
        bool move_valid = false;
        if (direction > 7) {
            // Movimiento fuera de rango
            state->players[i].invalid_moves++;
        } else {
            short dx = DIR_OFFSETS[direction][0];
            short dy = DIR_OFFSETS[direction][1];
            int cur_x = state->players[i].x;
            int cur_y = state->players[i].y;
            int new_x = cur_x + dx;
            int new_y = cur_y + dy;
            if (new_x < 0 || new_x >= width || new_y < 0 || new_y >= height) {
                // Se intenta salir del tablero
                state->players[i].invalid_moves++;
            } else {
                int target_val = state->board[new_y * width + new_x];
                if (target_val <= 0) {
                    // La celda destino no está libre (ocupada/capturada)
                    state->players[i].invalid_moves++;
                } else {
                    // Movimiento válido
                    move_valid = true;
                    state->players[i].valid_moves++;
                    state->players[i].score += (unsigned int)target_val;
                    state->players[i].x = (unsigned short)new_x;
                    state->players[i].y = (unsigned short)new_y;
                    state->board[new_y * width + new_x] = -(int)i;
                }
            }
        }
        // Actualizar estado de bloqueo de todos los jugadores
        for (int k = 0; k < num_players; ++k) {
            if (!state->players[k].blocked) {
                bool any_free = false;
                for (int d = 0; d < 8; ++d) {
                    int nx = state->players[k].x + DIR_OFFSETS[d][0];
                    int ny = state->players[k].y + DIR_OFFSETS[d][1];
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        if (state->board[ny * width + nx] > 0) {
                            any_free = true;
                            break;
                        }
                    }
                }
                if (!any_free) {
                    state->players[k].blocked = true;
                }
            }
        }
        // Revisar condición de fin de juego (todos bloqueados)
        bool all_blocked_now = true;
        for (int k = 0; k < num_players; ++k) {
            if (!state->players[k].blocked) {
                all_blocked_now = false;
                break;
            }
        }
        if (all_blocked_now) {
            all_blocked_flag = true;
        }
        // Liberar locks de escritura
        sem_post(&sync->sem_state_lock);
        sem_post(&sync->sem_turnstile);
        // Actualizar temporizador de último movimiento válido
        if (move_valid) {
            last_valid_time = time(NULL);
        }
        // Notificar al jugador i que se procesó su movimiento
        if (pipe_fds[i][0] >= 0) {
            sem_post(&sync->sem_player[i]);
        }
        // Notificar a la vista del cambio de estado (si está presente)
        if (has_view) {
            sem_post(&sync->sem_master_to_view);
            sem_wait(&sync->sem_view_to_master);
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
    state->game_finished = true;
    if (has_view) {
        // Despertar a la vista para que imprima/termine
        sem_post(&sync->sem_master_to_view);
        sem_wait(&sync->sem_view_to_master);
    }
    // Despertar a jugadores que puedan estar esperando permiso, para que salgan
    for (int i = 0; i < num_players; ++i) {
        if (pipe_fds[i][0] >= 0) {
            sem_post(&sync->sem_player[i]);
        }
    }

    // Esperar a que todos los jugadores terminen y mostrar sus resultados
    for (int i = 0; i < num_players; ++i) {
        int status;
        waitpid(player_pids[i], &status, 0);
        printf("Jugador %d finalizado. Puntaje: %u. ", i, state->players[i].score);
        if (WIFEXITED(status)) {
            printf("Retorno: %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Terminado por señal: %d\n", WTERMSIG(status));
        } else {
            printf("Terminado.\n");
        }
    }
    // Esperar a que finalice la vista (si corresponde) y mostrar resultado
    if (has_view) {
        int status;
        waitpid(view_pid, &status, 0);
        printf("Proceso vista finalizado. ");
        if (WIFEXITED(status)) {
            printf("Retorno: %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Terminado por señal: %d\n", WTERMSIG(status));
        } else {
            printf("Terminado.\n");
        }
    }

    // Limpieza final de recursos
    cleanup_resources(state, state_size, sync, sync_size, state_fd, sync_fd, pipe_fds, num_players);
    return EXIT_SUCCESS;
}