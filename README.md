# ChompChamps - Juego Multijugador con Memoria Compartida

Un juego multijugador implementado en C que utiliza memoria compartida POSIX, semáforos y pipes para la comunicación entre procesos.

## 📋 Descripción

ChompChamps es un juego donde múltiples jugadores compiten en un tablero para capturar celdas con recompensas. Los jugadores se mueven en 8 direcciones (norte, noreste, este, sureste, sur, suroeste, oeste, noroeste) y obtienen puntos por las celdas que capturan. El juego termina cuando todos los jugadores están bloqueados o se agota el tiempo límite.

## 🏗️ Arquitectura

El juego está compuesto por tres tipos de procesos:

- **Master**: Controla la lógica del juego, maneja el estado y coordina la comunicación
- **Players**: Procesos independientes que envían movimientos al master
- **View** (opcional): Interfaz visual usando ncurses para mostrar el estado del juego

## 🚀 Compilación

```bash
make clean
make
```

Esto genera los ejecutables:
- `master` - Proceso principal del juego
- `player` - Proceso jugador
- `view` - Interfaz visual

## 📖 Uso

### Sintaxis básica:
```bash
./master [-w ancho] [-h alto] [-d delay_ms] [-t timeout_s] [-s semilla] [-v ./view] -p ./player [./player ...]
```

### Parámetros:

| Parámetro | Descripción | Valor por defecto |
|-----------|-------------|-------------------|
| `-w ancho` | Ancho del tablero (mínimo 10) | 10 |
| `-h alto` | Alto del tablero (mínimo 10) | 10 |
| `-d delay_ms` | Delay entre movimientos en ms | 200 |
| `-t timeout_s` | Tiempo límite sin movimientos válidos | 10 |
| `-s semilla` | Semilla para generación aleatoria | tiempo actual |
| `-v ruta_vista` | Ruta al ejecutable de la vista | sin vista |
| `-p jugador...` | Rutas a los ejecutables de jugadores | requerido |

### Ejemplos:

```bash
# Juego básico con 3 jugadores y vista
./master -p ./player ./player ./player -v ./view

# Tablero personalizado 15x15 con delay de 500ms
./master -w 15 -h 15 -d 500 -p ./player ./player -v ./view

# Juego sin vista con timeout de 30 segundos
./master -t 30 -p ./player ./player ./player

# Semilla fija para reproducibilidad
./master -s 12345 -p ./player ./player -v ./view
```

## 🎮 Mecánicas del Juego

### Tablero
- Tablero 2D con celdas que contienen recompensas (valores 1-9)
- Los jugadores capturan celdas moviéndose sobre ellas
- Las celdas capturadas se marcan con el símbolo **##** el color que le corresponde al jugador que la capturó

### Movimientos
- 8 direcciones posibles (0-7):
  - 0: Norte, 1: Noreste, 2: Este, 3: Sureste
  - 4: Sur, 5: Suroeste, 6: Oeste, 7: Noroeste

### Condiciones de Victoria
- **Puntaje más alto** cuando el juego termina
- En caso de empate: menor número de movimientos válidos
- Si persiste el empate: menor número de movimientos inválidos
- Si el empate persiste después de comparar movimientos inválidos: **empate declarado**

### Fin del Juego
- Todos los jugadores están bloqueados (sin movimientos válidos)
- Se agota el tiempo límite sin movimientos válidos
- Intervención manual (Ctrl+C)

## 🔧 Arquitectura Técnica

### Memoria Compartida
- **`/game_state`**: Estado del juego (tablero, jugadores, puntajes)
- **`/game_sync`**: Semáforos para sincronización

### Sincronización
- **Readers-Writers**: Para acceso concurrente al estado del juego
- **Semáforos de turno**: Control de movimientos de jugadores
- **Sincronización vista-master**: Para actualización de la interfaz

### Comunicación
- **Pipes**: Master ← Players (envío de movimientos)
- **Semáforos**: Sincronización entre todos los procesos
- **Memoria compartida**: Estado global accesible por todos

## 🖥️ Interfaz Visual

La vista muestra:
- **Tablero**: Celdas con recompensas y posiciones de jugadores
- **Scoreboard**: Puntajes, movimientos válidos/inválidos, estado de bloqueo
- **Leyenda**: Códigos de colores para jugadores y tipos de celda


## 📁 Estructura del Proyecto

```
TP1SO/
├── Makefile              # Configuración de compilación
├── master.c              # Proceso principal del juego
├── master_lib.c          # Lógica del master
├── master_lib.h          # Headers del master
├── player.c              # Proceso jugador
├── view.c                # Interfaz visual
├── shared_memory.c       # Gestión de memoria compartida
├── shared_memory.h       # Estructuras y definiciones
├── sync_utils.c          # Utilidades de sincronización
├── sync_utils.h          # Headers de sincronización
└── README.md             # Este archivo
```

## ⚠️ Limitaciones y Consideraciones

- **Máximo 9 jugadores** simultáneos
- **Dimensiones mínimas**: 10x10. El tamaño del tablero está limitado por la memoria disponible y el tamaño de la terminal
- **Terminal**: Se requiere soporte para colores (ncurses)
- **Plataforma**: Linux/Unix con soporte POSIX

## 🐛 Solución de Problemas

### Error "Terminal too small"
Reduzca las dimensiones del tablero con `-w` y `-h`

### Error "View must be started by master"
La vista solo puede ser ejecutada por el master, no manualmente

### Procesos colgados
Use `Ctrl+C` para terminar limpiamente todos los procesos

### Problemas de sincronización
Verifique que no haya otros procesos usando la memoria compartida:
```bash
# Listar memoria compartida
ls /dev/shm/

# Limpiar memoria compartida si es necesario
rm /dev/shm/game_*
```

## 📝 Licencia

Este es un proyecto académico personal para el aprendizaje de sistemas operativos y programación concurrente, dentro del marco de la materia Sistemas Operativos (72.11)
