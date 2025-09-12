#include "master_lib.h"

static void print_usage(const char *progname)
{
    fprintf(stderr, "Uso: %s [-w ancho] [-h alto] [-d delay_ms] [-t timeout_s] [-s semilla] [-v ruta_vista] -p jugador1 [jugador2 ...]\n", progname);
}

int parse_arguments(int argc, char *argv[], unsigned short *width, unsigned short *height, unsigned int *delay_ms, unsigned int *timeout_s, unsigned int *seed, char **view_path, char *player_paths[], int *num_players)
{
    // Procesar opciones
    int opt;
    while ((opt = getopt(argc, argv, "w:h:d:t:s:v:p")) != -1)
    {
        switch (opt)
        {
        case 'w':
            unsigned short new_width = atoi(optarg);
            if (new_width > 10)
                *width = new_width;
            break;
        case 'h':
            unsigned short new_height = atoi(optarg);
            if (new_height > 10)
                *height = new_height;
            break;
        case 'd':
            *delay_ms = (unsigned int)atoi(optarg);
            break;
        case 't':
            *timeout_s = (unsigned int)atoi(optarg);
            break;
        case 's':
            *seed = (unsigned int)atoi(optarg);
            break;
        case 'v':
            *view_path = optarg;
            break;
        case 'p':
            // Solo marcador, jugadores procesados después
            break;
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    // Recoger jugadores
    for (int i = optind; i < argc && *num_players < MAX_PLAYERS; ++i)
    {
        if (argv[i][0] == '-')
            break;
        if(access(argv[i], X_OK) != 0) {
            fprintf(stderr, "Error: No se puede ejecutar el jugador '%s': %s\n", argv[i], strerror(errno));
            return -1;
        }
        else{
            player_paths[(*num_players)++] = argv[i];
        }
    }

    // Validaciones
    if (*num_players < 1)
    {
        fprintf(stderr, "Error: Debe especificar al menos un jugador\n");
        return -1;
    }

    return 0;
}

void cleanup_resources(game_state_t *state, game_sync_t *sync, int pipe_fds[][2], int num_players)
{
    // Cerrar pipes
    for (int i = 0; i < num_players; ++i)
    {
        if (pipe_fds[i][0] >= 0)
            close(pipe_fds[i][0]);
        if (pipe_fds[i][1] >= 0)
            close(pipe_fds[i][1]);
    }

    // Usar funciones de librería para cleanup
    if (sync != NULL)
    {
        cleanup_semaphores(sync, num_players);
        close_game_sync(sync);
    }

    if (state != NULL)
    {
        close_game_state(state, state->board_width, state->board_height);
    }

    // Limpiar shared memory
    unlink_shared_memory(GAME_STATE_NAME);
    unlink_shared_memory(GAME_SYNC_NAME);
}

void initialize_game_state(game_state_t *state, char *player_paths[MAX_PLAYERS], int num_players, unsigned int seed){
    state->player_count = num_players;
    state->game_over = false;

    // Posicionar jugadores de forma determinística con margen similar
    for (int i = 0; i < num_players; ++i)
    {
        player_t *p = &state->players[i];

        // Nombre (basename de la ruta del ejecutable)
        const char *path = player_paths[i];
        const char *basename = strrchr(path, '/');
        basename = (basename ? basename + 1 : path);
        strncpy(p->player_name, basename, MAX_NAME_LENGTH - 1);
        p->player_name[MAX_NAME_LENGTH - 1] = '\0';

        // Posicionar jugadores de forma determinística
        int grid_rows = (int)ceil(sqrt(num_players));
        if (grid_rows < 1)
            grid_rows = 1;
        int grid_cols = (int)ceil((double)num_players / grid_rows);
        if (grid_cols < 1)
            grid_cols = num_players;

        int row = i / grid_cols;
        int col = i % grid_cols;

        unsigned short px = (unsigned short)(((col + 1) * (long)state->board_width) / (grid_cols + 1));
        unsigned short py = (unsigned short)(((row + 1) * (long)state->board_height) / (grid_rows + 1));
        if (px >= state->board_width)
            px = state->board_width - 1;
        if (py >= state->board_height)
            py = state->board_height - 1;

        p->pos_x = px;
        p->pos_y = py;
        p->pid = 0; // se asignará luego del fork
        p->is_blocked = false;
    }
    
    // Inicializar tablero con recompensas aleatorias (y marcar posiciones iniciales de jugadores)
    srand(seed);
    for (unsigned short y = 0; y < state->board_height; ++y)
    {
        for (unsigned short x = 0; x < state->board_width; ++x)
        {
            // Verificar si esta celda es posición inicial de algún jugador
            int found_idx = -1;
            for (int k = 0; k < state->player_count; ++k)
            {
                if (state->players[k].pos_x == x && state->players[k].pos_y == y)
                {
                    found_idx = k;
                    break;
                }
            }
            if (found_idx >= 0)
            {
                // Celda ocupada inicialmente por jugador found_idx
                state->board[y * state->board_width + x] = -found_idx;
            }
            else
            {
                // Celda libre: asignar recompensa aleatoria 1-9
                state->board[y * state->board_width + x] = (rand() % 9) + 1;
            }
        }
    }
}


int create_player_processes(game_state_t *state, game_sync_t *game_sync, char *player_paths[], int num_players, int pipe_fds[][2], pid_t player_pids[]) {
    for (int i = 0; i < num_players; ++i)
    {
        if (pipe(pipe_fds[i]) == -1)
        {
            perror("pipe");
            // Terminar procesos ya creados
            for (int j = 0; j < i; ++j)
            {
                kill(player_pids[j], SIGTERM);
            }
            cleanup_resources(state, game_sync, pipe_fds, i);
            return -1;
        }
        
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            close(pipe_fds[i][0]);
            close(pipe_fds[i][1]);
            for (int j = 0; j < i; ++j)
            {
                kill(player_pids[j], SIGTERM);
            }
            cleanup_resources(state, game_sync, pipe_fds, i);
            return -1;
        }
        
        if (pid == 0)
        {
            // Proceso hijo (jugador i)
            dup2(pipe_fds[i][1], STDOUT_FILENO);
            close(pipe_fds[i][0]); // cerrar extremo lectura en el hijo
            close(pipe_fds[i][1]); // cerrar descriptor duplicado (ya duplicado en stdout)
            
            char w_arg[16], h_arg[16];
            snprintf(w_arg, sizeof(w_arg), "%hu", state->board_width);
            snprintf(h_arg, sizeof(h_arg), "%hu", state->board_height);
            
            execl(player_paths[i], player_paths[i], w_arg, h_arg, (char *)NULL);
            fprintf(stderr, "Error: no se pudo ejecutar %s: %s\n", player_paths[i], strerror(errno));
            _exit(127);
        }
        
        // Proceso padre
        player_pids[i] = pid;
        state->players[i].pid = pid;
        close(pipe_fds[i][1]); // cerrar extremo de escritura en el máster
    }
    
    return 0;
}

pid_t create_view_process(game_state_t *state, game_sync_t *game_sync, char *view_path, unsigned short width, unsigned short height, pid_t player_pids[], int num_players, int pipe_fds[][2]) {
    if (view_path == NULL) {
        return -1;  // No se especificó vista
    }
    
    pid_t vpid = fork();
    if (vpid < 0) {
        perror("fork (vista)");
        // Terminar jugadores en caso de fallo
        for (int j = 0; j < num_players; ++j) {
            kill(player_pids[j], SIGTERM);
        }
        cleanup_resources(state, game_sync, pipe_fds, num_players);
        return -1;
    }
    
    if (vpid == 0) {
        // Proceso hijo (vista)
        char w_arg[16], h_arg[16];
        snprintf(w_arg, sizeof(w_arg), "%hu", width);
        snprintf(h_arg, sizeof(h_arg), "%hu", height);
        execl(view_path, view_path, w_arg, h_arg, (char *)NULL);
        fprintf(stderr, "Error: no se pudo ejecutar vista %s: %s\n", view_path, strerror(errno));
        _exit(127);
    }
    
    // Proceso padre: sincronización con la vista
    notify_view(game_sync);
    wait_view_done(game_sync);
    
    return vpid;
}

long calculate_remaining_time(time_t last_valid_time, unsigned int timeout_s) {
    time_t now = time(NULL);
    long elapsed = now - last_valid_time;
    return (long)timeout_s - elapsed;
}

// Parámetros: &readfds, pipe_fds, num_players
// Retorna: maxfd (int)
int configure_fd_set(fd_set *readfds, int pipe_fds[][2], int num_players) {
    FD_ZERO(readfds);
    int maxfd = -1;
    for (int i = 0; i < num_players; ++i) {
        if (pipe_fds[i][0] >= 0) {
            FD_SET(pipe_fds[i][0], readfds);
            if (pipe_fds[i][0] > maxfd) {
                maxfd = pipe_fds[i][0];
            }
        }
    }
    return maxfd;
}

// Parámetros: &readfds, pipe_fds, num_players, &start_index
// Retorna: índice del jugador listo (int)
int search_pipe_ready(fd_set *readfds, int pipe_fds[][2], int num_players, int *start_index) {
    for (int j = 0; j < num_players; ++j) {
        int idx = (*start_index + j) % num_players;
        if (pipe_fds[idx][0] >= 0 && FD_ISSET(pipe_fds[idx][0], readfds)) {
            *start_index = (idx + 1) % num_players;
            return idx;
        }
    }
    return -1;
}

bool process_player_move(game_state_t *state, int player_idx, unsigned char direction, const int dir_offsets[8][2]) {
    if (direction > 7) {
        // Movimiento fuera de rango
        state->players[player_idx].invalid_moves++;
        return false;
    }
    
    short dx = dir_offsets[direction][0];
    short dy = dir_offsets[direction][1];
    int cur_x = state->players[player_idx].pos_x;
    int cur_y = state->players[player_idx].pos_y;
    int new_x = cur_x + dx;
    int new_y = cur_y + dy;
    
    if (new_x < 0 || new_x >= state->board_width || new_y < 0 || new_y >= state->board_height) {
        // Se intenta salir del tablero
        state->players[player_idx].invalid_moves++;
        return false;
    }
    
    int target_val = state->board[new_y * state->board_width + new_x];
    if (target_val <= 0) {
        // La celda destino no está libre (ocupada/capturada)
        state->players[player_idx].invalid_moves++;
        return false;
    }
    
    // Movimiento válido
    state->players[player_idx].valid_moves++;
    state->players[player_idx].score += (unsigned int)target_val;
    state->players[player_idx].pos_x = (unsigned short)new_x;
    state->players[player_idx].pos_y = (unsigned short)new_y;
    state->board[new_y * state->board_width + new_x] = -(int)player_idx;
    
    return true;
}

// Parámetros: state, i, direction, DIR_OFFSETS
// Retorna: true si el movimiento fue válido, false si no
void update_lock_status(game_state_t *state, const int DIR_OFFSETS[8][2], bool move_valid) {
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
}

bool all_players_blocked(game_state_t *state) {
    for (int k = 0; k < state->player_count; ++k) {
        if (!state->players[k].is_blocked) {
            return false;
        }
    }
    return true;
}

bool handle_move_aftermath(game_sync_t *game_sync, bool has_view, int pipe_fds[][2], int i, bool move_valid, unsigned int delay_ms, bool all_blocked_flag, time_t *last_valid_time) {
    // Actualizar temporizador de último movimiento válido
    if (move_valid) {
        *last_valid_time = time(NULL);
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
    
    // Si se alcanzó la condición de fin, indicar salida del bucle
    return !all_blocked_flag;
}

int finalize_game(game_state_t *state, game_sync_t *game_sync, bool has_view, pid_t view_pid, int pipe_fds[][2], pid_t player_pids[], int num_players) {
    // Marcar el fin del juego en el estado compartido
    state->game_over = true;
    
    // Notificar a la vista si existe
    if (has_view) {
        notify_view(game_sync);
        wait_view_done(game_sync);
    }
    
    // Despertar a los jugadores que puedan estar esperando
    for (int i = 0; i < num_players; ++i) {
        if (pipe_fds[i][0] >= 0) {
            allow_player_move(game_sync, i);
        }
    }

    // Esperar a que terminen los procesos y mostrar resultados
    if (has_view) {
        int status;
        waitpid(view_pid, &status, 0);
        
        if (WIFEXITED(status)) {
            printf("View exited (%d)\n", WEXITSTATUS(status));
            
            // Esperar por cada proceso jugador y mostrar sus estadísticas
            for (int i = 0; i < num_players; ++i) {
                int player_status;
                waitpid(player_pids[i], &player_status, 0);
                printf("Player %s (%d) exited (%d) with a score of %u / %u valid moves / %u invalid moves\n",
                       state->players[i].player_name, i, WEXITSTATUS(player_status),
                       state->players[i].score,
                       state->players[i].valid_moves,
                       state->players[i].invalid_moves);
            }
        }
        else if (WIFSIGNALED(status)) {
            printf("View terminated by signal (%d)\n", WTERMSIG(status));
        }
        else {
            printf("View terminated abnormally\n");
        }
    }

    // Limpieza final de recursos
    cleanup_resources(state, game_sync, pipe_fds, num_players);
    
    return EXIT_SUCCESS;
}


int check_game_status(game_state_t *state) {
    if (state == NULL) {
        fprintf(stderr, "Error creating game state\n");
        return -1;
    }
    if (state == MAP_FAILED) {
        perror("mmap(/game_state)");
        shm_unlink("/game_state");
        shm_unlink("/game_sync");
        return -1;
    }
    return 0;
}

int check_game_sync(game_sync_t *game_sync, game_state_t *state, unsigned short width, unsigned short height) {
    if (game_sync == NULL) {
        fprintf(stderr, "Error creating game sync\n");
        close_game_state(state, width, height);
        return -1;
    }
    if (game_sync == MAP_FAILED) {
        perror("mmap(/game_sync)");
        shm_unlink("/game_state");
        shm_unlink("/game_sync");
        return -1;
    }
    return 0;
}