#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define REPOS_CURSOR "\x1b[1;1H"
#define CLEAR_SCREEN "\x1b[2J"
#define HIDE_CURSOR "\033[?25l"
#define SHOW_CURSOR "\033[?25h"

#define N 10
#define M 10
#define INPUT_BUF_LEN 10

#define MULTIPLIER_IN_A_ROW 2
#define NUM_THREADS 8
#define MAX_MOVES 100
#define DEFAULT_DEPTH 6

struct termios origterm;

typedef struct Pos {
  int x, y;
} Pos;

typedef struct Game {
  int grid[N][M];
  char input[INPUT_BUF_LEN];
  int failedInput;
  int invalidMove;
  int moveNo;
  int won;
  int aiThinking;
  Pos aiMove;
  int searchDepth;
} Game;

// Structure for move evaluation task
typedef struct MoveTask {
  int grid[N][M]; // Grid state after this move
  Pos move;       // The move position
  int score;      // Result score (filled by worker)
  int completed;  // Flag indicating task is done
} MoveTask;

// Thread pool structures
typedef struct ThreadPool {
  pthread_t threads[NUM_THREADS];
  pthread_mutex_t mutex;
  pthread_cond_t work_available;
  pthread_cond_t work_done;
  MoveTask *tasks[MAX_MOVES];
  int task_count;
  int next_task;
  int active_threads;
  int shutdown;
} ThreadPool;

Game *game;
FILE *logfile;
ThreadPool *pool;

// Forward declarations
void draw(void);
void threadPool_init(void);
void threadPool_wait(void);
void threadPool_destroy(void);
int minimax(int grid[N][M], int depth, int isMaximizing, int alpha, int beta);

void llog(const char *format, ...) {
#ifdef LOG_ENABLED
  va_list args;
  va_start(args, format);
  vfprintf(logfile, format, args);
  fflush(logfile);
  va_end(args);
#else
  (void)format;
#endif
}

void teardown(void) {
  printf("%s%s%s\n", SHOW_CURSOR, CLEAR_SCREEN, REPOS_CURSOR);
  fflush(stdout);
  if (logfile)
    fclose(logfile);
  if (pool)
    threadPool_destroy();
  free(game);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &origterm);
}

void signal_hander(int signum) {
  (void)signum;
  teardown();
  exit(0);
}

int isValidMove(int x, int y, int grid[N][M]) {
  return (x >= 0 && x < N && y >= 0 && y < M && grid[x][y] == 0);
}

Pos parseInput(void) {
  Pos p = {-1, -1};
  int x;
  char ys;

  int res = -1;
  // First attempt [ROW][COL]
  res = sscanf(game->input, " %d %c", &x, &ys);
  // First attempt [COL][ROW]
  if (res != 2)
    res = sscanf(game->input, " %c %d", &ys, &x);

  if (res == 2) {
    p.x = x - 1;
    if (ys >= 'A' && ys <= 'Z') {
      p.y = ys - 'A';
    } else if (ys >= 'a' && ys <= 'z') {
      p.y = ys - 'a';
    } else {
      game->failedInput = 1;
      return p;
    }
  }

  if ((p.x < 0 || p.x >= N) || (p.y >= M)) {
    game->failedInput = 1;
    return p;
  }

  return p;
}

// Assign a score (negative -> good for user, positive -> good for AI)
// The assignment is based on the given status of the grid
// The heuristic is very simple, it assigns a score based on how many
// in-a-row and interruptions there are for each users on the valid paths.
int assignScoreToGrid(int grid[N][M]) {
  int inarow = 0;
  int player = 0;
  int scores[2] = {0, 0};

  unsigned short int visited[2][N][M];

  memset(visited, 0, 2 * sizeof(unsigned short int) * N * M);

  for (int i = 0; i < N; i++) {
    for (int j = 0; j < M; j++) {
      if (grid[i][j] == 0)
        continue;

      player = grid[i][j];

      // Horiz
      inarow = 1;
      for (int h = j + 1; h < M; h++) {
        if (grid[i][h] == player) {
          visited[player - 1][i][h] = 1;
          inarow++;
          scores[player - 1] += MULTIPLIER_IN_A_ROW * inarow;
          if (inarow == 4)
            goto won;
        } else if (grid[i][h] == 0) {
          inarow = 0;
          scores[player - 1] += 1;
        } else {
          inarow = 0;
          break;
        }
      }

      // Vert
      inarow = 1;
      for (int v = i + 1; v < N; v++) {
        if (grid[v][j] == player) {
          visited[player - 1][v][j] = 1;
          inarow++;
          scores[player - 1] += MULTIPLIER_IN_A_ROW * inarow;
          if (inarow == 4)
            goto won;
        } else if (grid[v][j] == 0) {
          inarow = 0;
          scores[player - 1] += 1;
        } else {
          inarow = 0;
          break;
        }
      }

      // Diag down-right
      inarow = 1;
      for (int h = j + 1, v = i + 1; v < N && h < M; v++, h++) {
        if (grid[v][h] == player) {
          visited[player - 1][v][h] = 1;
          inarow++;
          scores[player - 1] += MULTIPLIER_IN_A_ROW * inarow;
          if (inarow == 4)
            goto won;
        } else if (grid[v][h] == 0) {
          inarow = 0;
          scores[player - 1] += 1;
        } else {
          inarow = 0;
          break;
        }
      }

      // Diag down-left
      inarow = 1;
      for (int h = j - 1, v = i + 1; v < N && h >= 0; v++, h--) {
        if (grid[v][h] == player) {
          visited[player - 1][v][h] = 1;
          inarow++;
          scores[player - 1] += MULTIPLIER_IN_A_ROW * inarow;
          if (inarow == 4)
            goto won;
        } else if (grid[v][h] == 0) {
          inarow = 0;
          scores[player - 1] += 1;
        } else {
          inarow = 0;
          break;
        }
      }
    }
  }

  return scores[1] - scores[0];

won:
  return player == 0 ? 0 : (player == 1 ? -1000 : 1000);
}

// Structure for move ordering in minimax
typedef struct ScoredMove {
  Pos pos;
  int score;
} ScoredMove;

// Comparison function for sorting moves (best moves first for AI)
int compareMoves(const void *a, const void *b) {
  MoveTask *taskA = *(MoveTask **)a;
  MoveTask *taskB = *(MoveTask **)b;
  // Quick heuristic: use assignScoreToGrid as estimate
  int scoreA = assignScoreToGrid(taskA->grid);
  int scoreB = assignScoreToGrid(taskB->grid);
  return scoreB - scoreA; // Sort descending (best first)
}

// Comparison for scored moves (maximizing player - descending)
int compareScoredMovesMax(const void *a, const void *b) {
  ScoredMove *moveA = (ScoredMove *)a;
  ScoredMove *moveB = (ScoredMove *)b;
  return moveB->score - moveA->score;
}

// Comparison for scored moves (minimizing player - ascending)
int compareScoredMovesMin(const void *a, const void *b) {
  ScoredMove *moveA = (ScoredMove *)a;
  ScoredMove *moveB = (ScoredMove *)b;
  return moveA->score - moveB->score;
}

// Thread worker function
void *worker_thread(void *arg) {
  (void)arg;

  while (1) {
    pthread_mutex_lock(&pool->mutex);

    // Wait for work or shutdown signal
    while (pool->next_task >= pool->task_count && !pool->shutdown) {
      pthread_cond_wait(&pool->work_available, &pool->mutex);
    }

    if (pool->shutdown) {
      pthread_mutex_unlock(&pool->mutex);
      break;
    }

    // Get next task
    int task_idx = pool->next_task++;
    pool->active_threads++;
    pthread_mutex_unlock(&pool->mutex);

    // Process task (outside of lock)
    MoveTask *task = pool->tasks[task_idx];
    task->score = minimax(task->grid, game->searchDepth, 0, -10000, 10000);
    task->completed = 1;

    // Mark thread as done with this task
    pthread_mutex_lock(&pool->mutex);
    pool->active_threads--;
    pthread_cond_signal(&pool->work_done);
    pthread_mutex_unlock(&pool->mutex);
  }

  return NULL;
}

// Initialize thread pool
void threadPool_init(void) {
  pool = malloc(sizeof(ThreadPool));
  pool->task_count = 0;
  pool->next_task = 0;
  pool->active_threads = 0;
  pool->shutdown = 0;

  pthread_mutex_init(&pool->mutex, NULL);
  pthread_cond_init(&pool->work_available, NULL);
  pthread_cond_init(&pool->work_done, NULL);

  // Create worker threads
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_create(&pool->threads[i], NULL, worker_thread, NULL);
  }
}

// Wait for all tasks to complete
void threadPool_wait(void) {
  pthread_mutex_lock(&pool->mutex);
  while (pool->next_task < pool->task_count || pool->active_threads > 0) {
    pthread_cond_wait(&pool->work_done, &pool->mutex);
  }
  pthread_mutex_unlock(&pool->mutex);
}

// Shutdown thread pool
void threadPool_destroy(void) {
  pthread_mutex_lock(&pool->mutex);
  pool->shutdown = 1;
  pthread_cond_broadcast(&pool->work_available);
  pthread_mutex_unlock(&pool->mutex);

  // Join all threads
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(pool->threads[i], NULL);
  }

  pthread_mutex_destroy(&pool->mutex);
  pthread_cond_destroy(&pool->work_available);
  pthread_cond_destroy(&pool->work_done);
  free(pool);
}

// Minimax with alpha-beta pruning
// player: 1 = human (minimizing), 2 = AI (maximizing)
// Returns the score for the current board state
int minimax(int grid[N][M], int depth, int isMaximizing, int alpha, int beta) {
  // Check if game is won/lost
  int score = assignScoreToGrid(grid);

  // Terminal conditions
  if (score == -1000 || score == 1000) {
    // Game won - return score adjusted by depth to prefer faster wins
    return score + (score > 0 ? -depth : depth);
  }

  if (depth == 0) {
    // Max depth reached - return heuristic score
    return score;
  }

  // Check if board is full (draw)
  int movesPossible = 0;
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < M; j++) {
      if (grid[i][j] == 0) {
        movesPossible = 1;
        break;
      }
    }
    if (movesPossible)
      break;
  }
  if (!movesPossible) {
    return 0; // Draw
  }

  if (isMaximizing) {
    // AI's turn (maximize score) - with move ordering
    ScoredMove moves[MAX_MOVES];
    int moveCount = 0;

    // Generate and score all moves
    for (int i = 0; i < N; i++) {
      for (int j = 0; j < M; j++) {
        if (grid[i][j] == 0) {
          int newGrid[N][M];
          memcpy(newGrid, grid, N * M * sizeof(int));
          newGrid[i][j] = 2;
          moves[moveCount].pos.x = i;
          moves[moveCount].pos.y = j;
          moves[moveCount].score = assignScoreToGrid(newGrid);
          moveCount++;
        }
      }
    }

    // Sort moves (best first)
    qsort(moves, moveCount, sizeof(ScoredMove), compareScoredMovesMax);

    // Evaluate moves in order
    int maxEval = -10000;
    for (int m = 0; m < moveCount; m++) {
      int newGrid[N][M];
      memcpy(newGrid, grid, N * M * sizeof(int));
      newGrid[moves[m].pos.x][moves[m].pos.y] = 2;

      int eval = minimax(newGrid, depth - 1, 0, alpha, beta);
      maxEval = eval > maxEval ? eval : maxEval;
      alpha = alpha > eval ? alpha : eval;

      // Beta cutoff
      if (beta <= alpha)
        break;
    }
    return maxEval;
  } else {
    // Player's turn (minimize score) - with move ordering
    ScoredMove moves[MAX_MOVES];
    int moveCount = 0;

    // Generate and score all moves
    for (int i = 0; i < N; i++) {
      for (int j = 0; j < M; j++) {
        if (grid[i][j] == 0) {
          int newGrid[N][M];
          memcpy(newGrid, grid, N * M * sizeof(int));
          newGrid[i][j] = 1;
          moves[moveCount].pos.x = i;
          moves[moveCount].pos.y = j;
          moves[moveCount].score = assignScoreToGrid(newGrid);
          moveCount++;
        }
      }
    }

    // Sort moves (worst first for minimizing player)
    qsort(moves, moveCount, sizeof(ScoredMove), compareScoredMovesMin);

    // Evaluate moves in order
    int minEval = 10000;
    for (int m = 0; m < moveCount; m++) {
      int newGrid[N][M];
      memcpy(newGrid, grid, N * M * sizeof(int));
      newGrid[moves[m].pos.x][moves[m].pos.y] = 1;

      int eval = minimax(newGrid, depth - 1, 1, alpha, beta);

      // If player can win, stop exploring
      if (eval == -1000 + depth - 1) {
        return eval;
      }

      minEval = eval < minEval ? eval : minEval;
      beta = beta < eval ? beta : eval;

      // Alpha cutoff
      if (beta <= alpha)
        break;
    }
    return minEval;
  }
}

Pos aiPlay(void) {
  Pos p = {-1, -1};
  int bestScore = -10000;

  llog("\n=== AI's turn ===\n");

  // First pass: check for immediate winning moves
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < M; j++) {
      if (game->grid[i][j] == 0) {
        int newGrid[N][M];
        memcpy(newGrid, game->grid, N * M * sizeof(int));
        newGrid[i][j] = 2;

        int score = assignScoreToGrid(newGrid);
        if (score == 1000) {
          llog("AI found winning move at [%d][%d]\n", i, j);
          p.x = i;
          p.y = j;
          return p;
        }
      }
    }
  }

  // Second pass: parallel evaluation of all moves
  MoveTask *tasks[MAX_MOVES];
  int taskCount = 0;

  // Create tasks for each legal move
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < M; j++) {
      if (game->grid[i][j] == 0) {
        tasks[taskCount] = malloc(sizeof(MoveTask));
        memcpy(tasks[taskCount]->grid, game->grid, N * M * sizeof(int));
        tasks[taskCount]->grid[i][j] = 2;
        tasks[taskCount]->move.x = i;
        tasks[taskCount]->move.y = j;
        tasks[taskCount]->score = -10000;
        tasks[taskCount]->completed = 0;
        taskCount++;
      }
    }
  }

  // Sort tasks by heuristic score (move ordering for better pruning)
  qsort(tasks, taskCount, sizeof(MoveTask *), compareMoves);

  // Submit tasks to thread pool
  pthread_mutex_lock(&pool->mutex);
  pool->task_count = taskCount;
  pool->next_task = 0;
  for (int i = 0; i < taskCount; i++) {
    pool->tasks[i] = tasks[i];
  }
  pthread_cond_broadcast(&pool->work_available);
  pthread_mutex_unlock(&pool->mutex);

  // Wait for all tasks to complete
  threadPool_wait();

  // Find best move from results
  for (int i = 0; i < taskCount; i++) {
    llog("Move [%d][%d] score: %d\n", tasks[i]->move.x, tasks[i]->move.y,
         tasks[i]->score);
    if (tasks[i]->score > bestScore) {
      bestScore = tasks[i]->score;
      p = tasks[i]->move;
    }
  }

  // Clean up tasks
  for (int i = 0; i < taskCount; i++) {
    free(tasks[i]);
  }

  // Reset pool for next use
  pthread_mutex_lock(&pool->mutex);
  pool->task_count = 0;
  pool->next_task = 0;
  pthread_mutex_unlock(&pool->mutex);

  llog("AI chose [%d][%d] with score %d\n", p.x, p.y, bestScore);
  return p;
}

int checkWin(int grid[N][M]) {
  int score = assignScoreToGrid(grid);
  if (score == -1000)
    return 1;
  if (score == 1000)
    return 2;
  return 0;
}

void setup(void) {
  struct termios raw;

#ifdef LOG_ENABLED
  logfile = fopen("/tmp/zeroglog", "w");
  if (!logfile) {
    perror("open logfile");
  }
#endif

  signal(SIGINT, signal_hander);
  signal(SIGKILL, signal_hander);
  signal(SIGTERM, signal_hander);

  tcgetattr(STDIN_FILENO, &origterm);
  raw = origterm;

  raw.c_lflag &= ~(ICANON | ECHO); // Disable canonical and echo mode
  raw.c_cc[VMIN] = 0;              // Not blocking
  raw.c_cc[VTIME] = 0;             // No timeout

  // Apply changes
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

  atexit(teardown);

  printf("%s%s%s", HIDE_CURSOR, CLEAR_SCREEN, REPOS_CURSOR);
  fflush(stdout);

  game = malloc(sizeof(Game));
  memset(game->grid, 0, M * N * sizeof(int));
  memset(game->input, 0, INPUT_BUF_LEN);
  game->failedInput = 0;
  game->won = 0;
  game->moveNo = 0;
  game->aiThinking = 0;
  game->aiMove.x = -1;
  game->aiMove.y = -1;
  game->searchDepth = DEFAULT_DEPTH;

  // Initialize thread pool
  threadPool_init();
}

void update(void) {
  char c;
  Pos pos;
  int moveDone = 0;

  if (read(STDIN_FILENO, &c, 1) == 1) {
    // Reset vars
    game->failedInput = 0;
    game->invalidMove = 0;

    switch (c) {
    // Backwards to delete last char
    case 127: {
      int s = strlen(game->input);
      if (s > 0) {
        game->input[s - 1] = '\0';
      }
      break;
    }
    case '\n': {
      if (game->won) {
        setup();
        return;
      }
      pos = parseInput();
      moveDone = 1;
      memset(game->input, 0, INPUT_BUF_LEN);
      break;
    }
    default: {
      int s = strlen(game->input);
      if (s < INPUT_BUF_LEN - 1) {
        game->input[s] = c;
        game->input[s + 1] = '\0';
      }
    }
    }
  }

  if (moveDone) {
    if (!isValidMove(pos.x, pos.y, game->grid)) {
      game->invalidMove = 1;
      return;
    }
    game->grid[pos.x][pos.y] = 1;
    game->moveNo++;

    int winningPlayer = checkWin(game->grid);
    if (winningPlayer) {
      game->won = winningPlayer;
      llog("%s won!!!\n", winningPlayer == 1 ? "You" : "AI");
      return;
    }

    // Show user's move before AI thinks
    game->aiThinking = 1;
    draw();

    // AI computes its move (blocking)
    Pos aiPos = aiPlay();
    game->grid[aiPos.x][aiPos.y] = 2;
    game->aiThinking = 0;

    winningPlayer = checkWin(game->grid);
    if (winningPlayer) {
      game->won = winningPlayer;
      llog("%s won!!!\n", winningPlayer == 1 ? "You" : "AI");
    }
  }
}

void drawGrid(int grid[N][M]) {
  for (int j = 0; j < M && j < 26; j++) {
    printf("%c ", 'A' + j);
  }
  printf("\n");
  for (int i = 0; i < N; i++) {
    printf(" %02d |", i + 1);
    for (int j = 0; j < M; j++) {
      printf(" %c", grid[i][j] == 0 ? '.' : (grid[i][j] == 1 ? 'X' : 'O'));
    }
    printf("\n");
  }
}

void draw(void) {
  printf("%s%s\n      ", CLEAR_SCREEN, REPOS_CURSOR);

  drawGrid(game->grid);

  printf("\n");

  if (game->won) {
    printf("You %s\nPress <Enter> to play again.",
           game->won == 1 ? "won!" : "lose...");
    fflush(stdout);
  } else if (game->aiThinking) {
    printf("AI thinking (depth %d, %d threads)...\n", game->searchDepth,
           NUM_THREADS);
  } else {
    // Draw input buf
    printf("Your move: %s\n",
           game->invalidMove
               ? "Invalid move, cell alreay set or out of bound."
               : (game->failedInput ? "Invalid input" : game->input));
    printf("AI search depth: %d\n", game->searchDepth);
  }
}

int main(int argc, char *argv[]) {
  setup();

  if (argc > 1) {
    int depth = atoi(argv[1]);
    if (depth > 0 && depth <= 12) {
      game->searchDepth = depth;
      llog("Search depth set to %d\n", depth);
    } else {
      printf("Invalid depth. Using default depth %d. Valid range: 1-12\n",
             DEFAULT_DEPTH);
      // Wait to show message
      sleep(2);
    }
  }

  while (1) {
    update();
    draw();
    usleep(16666); // ~60 FPS (1/60 second = 16.666ms)
  }
}
