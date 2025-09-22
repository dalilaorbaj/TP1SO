#include "master_lib.h"

static const int DIR_OFFSETS[8][2] = {
    {0, -1}, // arriba
    {1, -1}, // arriba-derecha
    {1, 0},  // derecha
    {1, 1},  // abajo-derecha
    {0, 1},  // abajo
    {-1, 1}, // abajo-izquierda
    {-1, 0}, // izquierda
    {-1, -1} // arriba-izquierda
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

    game_sync_t *game_sync = create_game_sync(num_players);
    if (check_game_sync(game_sync, state, width, height) != 0)
        return EXIT_FAILURE;

    initialize_game_state(state, player_paths, num_players, seed);

   pid_t view_pid = -1;
    bool has_view = (view_path != NULL);

    if (has_view) {
        view_pid = create_view_process(state, game_sync, view_path, width, height);
        if (view_pid == -1) {
            fprintf(stderr, "Error al crear proceso de vista. Continuando sin vista.\n");
            has_view = false;
        }
    }
    // Crear pipes y procesos jugador
    int pipe_fds[MAX_PLAYERS][2];
    pid_t player_pids[MAX_PLAYERS];

    if (create_player_processes(state, game_sync, player_paths, num_players, pipe_fds, player_pids) != 0) {
        // Si la vista se creó correctamente, necesitamos limpiarla antes de salir
        if (has_view && view_pid > 0) {
            kill(view_pid, SIGTERM);
            waitpid(view_pid, NULL, 0);
        }
        return EXIT_FAILURE;
    }

    /* === imprimir estado inicial antes de los movimientos iniciales de los jugadores === */

    if (has_view) {
        notify_view(game_sync);
        wait_view_done(game_sync);
    }

    for (int i = 0; i < num_players; ++i){
        allow_player_move(game_sync, i);
    }

    // Bucle principal de recepción de movimientos
    time_t last_valid_time = time(NULL);
    bool all_blocked_flag = false;
    int start_index = 0;

    // loop principal
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
        else if (res == 0){
            // Se agotó el tiempo sin movimientos válidos
            break;
        }
        int ready_index = search_pipe_ready(&readfds, pipe_fds, num_players, &start_index);
        if (ready_index == -1){
            // Ningún FD encontrado listo
            continue;
        }
        int i = ready_index;
        unsigned char move;
        ssize_t nread = read(pipe_fds[i][0], &move, 1);
        if (nread <= 0){
            if (nread == 0){ // EOF
                writer_enter(game_sync);
                state->players[i].is_blocked = true;
                all_blocked_flag = all_players_blocked(state);
                writer_exit(game_sync);

                close(pipe_fds[i][0]);
                pipe_fds[i][0] = -1;

                if (!all_blocked_flag) {
                    continue;
                }
            }
        }

    
        unsigned char direction = move;

        writer_enter(game_sync);
        bool move_valid = process_player_move(state, i, direction, DIR_OFFSETS);    
        update_lock_status(state, DIR_OFFSETS, move_valid);
        all_blocked_flag = all_players_blocked(state);
        writer_exit(game_sync);
        bool continue_game = handle_move_aftermath(state, game_sync, has_view, pipe_fds, i, move_valid, delay_ms, all_blocked_flag, &last_valid_time);

        if (!continue_game) {
            break;  // Salir del bucle principal
        }
    }
    return finalize_game(state, game_sync, has_view, view_pid, pipe_fds, player_pids, num_players);
}
