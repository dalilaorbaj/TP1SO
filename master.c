// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "master_lib.h"

// Desplazamientos de dirección (0 = arriba, 1 = arriba-derecha, ... sentido horario)
static const int DIR_OFFSETS[8][2] = {
    {0, -1}, // 0: arriba
    {1, -1}, // 1: arriba-derecha
    {1, 0},  // 2: derecha
    {1, 1},  // 3: abajo-derecha
    {0, 1},  // 4: abajo
    {-1, 1}, // 5: abajo-izquierda
    {-1, 0}, // 6: izquierda
    {-1, -1} // 7: arriba-izquierda
};

// Parámetros por defecto
unsigned short width = 10, height = 10;
unsigned int delay_ms = 200;
unsigned int timeout_s = 10;
char *view_path = NULL;
char *player_paths[MAX_PLAYERS];
int num_players = 0;
unsigned int seed = 0;

int main(int argc, char *argv[])
{
    seed = (unsigned int)time(NULL);

    if (parse_arguments(argc, argv, &width, &height, &delay_ms, &timeout_s, &seed, &view_path, player_paths, &num_players) != 0)
        return EXIT_FAILURE;

    game_state_t *state = create_game_state(width, height); //(!) chequear que create_game_state maneje el caso MAP_FAILED internamente y devuelva NULL en ese caso --> Chequeado! Flor
    if (check_game_status(state) != 0)
        return EXIT_FAILURE;

    game_sync_t *game_sync = create_game_sync(num_players); //(!) chequear que create_game_state maneje el caso MAP_FAILED internamente y devuelva NULL en ese caso
    if (check_game_sync(game_sync, state, width, height) != 0)
        return EXIT_FAILURE;

    initialize_game_state(state, player_paths, num_players, seed);

    // Crear pipes y procesos jugador
    int pipe_fds[MAX_PLAYERS][2];
    pid_t player_pids[MAX_PLAYERS];

    if (create_player_processes(state, game_sync, player_paths, num_players, pipe_fds, player_pids) != 0) {
        return EXIT_FAILURE;
    }

    // Crear proceso vista (si corresponde)
    pid_t view_pid = -1;
    bool has_view = (view_path != NULL);

    if (has_view) {
        view_pid = create_view_process(state, game_sync, view_path, width, height, player_pids, num_players, pipe_fds);
        if (view_pid == -1) {
            return EXIT_FAILURE;
        }
    }

    for (int i = 0; i < num_players; ++i){
        allow_player_move(game_sync, i);
    }

    // Bucle principal de recepción de movimientos
    time_t last_valid_time = time(NULL);
    bool all_blocked_flag = false;
    int start_index = 0;
    while (true){
        long remaining = calculate_remaining_time(last_valid_time, timeout_s);
        if (remaining <= 0){
            break;
        }
        fd_set readfds;
        int maxfd = configure_fd_set(&readfds, pipe_fds, num_players);
        if (maxfd == -1){
            // No quedan jugadores activos
            break;
        }
        struct timeval tv;
        tv.tv_sec = remaining;
        tv.tv_usec = 0;
        int res = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (res < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }
        if (res == 0){
            // Se agotó el tiempo sin movimientos válidos
            break;
        }
        int ready_index = search_pipe_ready(&readfds, pipe_fds, num_players, &start_index);
        if (ready_index == -1){
            // Ningún FD encontrado listo (no debería suceder si res > 0)
            continue;
        }
        int i = ready_index;
        unsigned char move;
        ssize_t nread = read(pipe_fds[i][0], &move, 1);
        if (nread <= 0){
            if (nread == 0){ // EOF: el jugador i terminó (cerró pipe)
                state->players[i].is_blocked = true;
                close(pipe_fds[i][0]);
                pipe_fds[i][0] = -1;
                
                all_blocked_flag = all_players_blocked(state);
                if (!all_blocked_flag) {
                    continue;
                }
            }
            else{
                perror("read");
                continue;
            }
        }
        if (nread <= 0){
            // Salir del bucle si todos bloqueados debido a EOF
            if (all_blocked_flag)
                break;
            else
                continue;
        }
    
        unsigned char direction = move;

        writer_enter(game_sync);
        bool move_valid = process_player_move(state, i, direction, DIR_OFFSETS);    
        update_lock_status(state, DIR_OFFSETS, move_valid);
        all_blocked_flag = all_players_blocked(state);
        writer_exit(game_sync);
        bool continue_game = handle_move_aftermath(game_sync, has_view, pipe_fds, i, move_valid, delay_ms, all_blocked_flag, &last_valid_time);

        if (!continue_game) {
            break;  // Salir del bucle principal
        }
    }
    return finalize_game(state, game_sync, has_view, view_pid, pipe_fds, player_pids, num_players);
}