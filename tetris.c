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
// #define LOG_ENABLED

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
} Game;

Game *game;
FILE *logfile;

// Forward declarations
void draw(void);

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

  // llog("\n=== assignScoreToGrid called ===\n");
  // for (int i = 0; i < N; i++) {
  //   for (int j = 0; j < M; j++) {
  //     if (grid[i][j] > 0)
  //       llog("[%d][%d]=%d ", i, j, grid[i][j]);
  //   }
  // }
  // llog("\n");

  for (int i = 0; i < N; i++) {
    for (int j = 0; j < M; j++) {
      if (grid[i][j] == 0)
        continue;

      player = grid[i][j];

      // Horiz
      inarow = 1;
      // llog("Checking horiz from [%d][%d], player=%d\n", i, j, player);
      for (int h = j + 1; h < M; h++) {
        // llog("  Checking [%d][%d], grid=%d, inarow=%d\n", i, h, grid[i][h],
        //      inarow);
        if (grid[i][h] == player) {
          visited[player - 1][i][h] = 1;
          inarow++;
          scores[player - 1] += MULTIPLIER_IN_A_ROW * inarow;
          // llog("  Match! inarow now=%d\n", inarow);
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
      // llog("Checking diag down-right from [%d][%d], player=%d\n", i, j,
      // player);
      for (int h = j + 1, v = i + 1; v < N && h < M; v++, h++) {
        // llog("  Checking [%d][%d], grid=%d, inarow=%d\n", v, h, grid[v][h],
        //      inarow);
        if (grid[v][h] == player) {
          visited[player - 1][v][h] = 1;
          inarow++;
          scores[player - 1] += MULTIPLIER_IN_A_ROW * inarow;
          // llog("  Match! inarow now=%d\n", inarow);
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

  int score = scores[1] - scores[0];
  // llog("Score: %d\n", score);
  return score;

won:
  return player == 0 ? 0 : (player == 1 ? -1000 : 1000);
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
    // AI's turn (maximize score)
    int maxEval = -10000;
    for (int i = 0; i < N; i++) {
      for (int j = 0; j < M; j++) {
        if (grid[i][j] == 0) {
          int newGrid[N][M];
          memcpy(newGrid, grid, N * M * sizeof(int));
          newGrid[i][j] = 2;

          int eval = minimax(newGrid, depth - 1, 0, alpha, beta);
          maxEval = eval > maxEval ? eval : maxEval;
          alpha = alpha > eval ? alpha : eval;

          // Beta cutoff - player can force a better outcome elsewhere
          if (beta <= alpha)
            break;
        }
      }
      if (beta <= alpha)
        break;
    }
    return maxEval;
  } else {
    // Player's turn (minimize score)
    int minEval = 10000;
    for (int i = 0; i < N; i++) {
      for (int j = 0; j < M; j++) {
        if (grid[i][j] == 0) {
          int newGrid[N][M];
          memcpy(newGrid, grid, N * M * sizeof(int));
          newGrid[i][j] = 1;

          int eval = minimax(newGrid, depth - 1, 1, alpha, beta);

          // If player can win, stop exploring this branch
          if (eval == -1000 + depth - 1) {
            return eval;
          }

          minEval = eval < minEval ? eval : minEval;
          beta = beta < eval ? beta : eval;

          // Alpha cutoff - AI can force a better outcome elsewhere
          if (beta <= alpha)
            break;
        }
      }
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

  // Try each possible move
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < M; j++) {
      if (game->grid[i][j] == 0) {
        int newGrid[N][M];
        memcpy(newGrid, game->grid, N * M * sizeof(int));
        newGrid[i][j] = 2;

        // Check if this move wins immediately
        int score = assignScoreToGrid(newGrid);
        if (score == 1000) {
          llog("AI found winning move at [%d][%d]\n", i, j);
          p.x = i;
          p.y = j;
          return p;
        }

        // Otherwise, use minimax to evaluate the move
        int moveScore = minimax(newGrid, 4, 0, -10000, 10000);
        llog("Move [%d][%d] score: %d\n", i, j, moveScore);

        if (moveScore > bestScore) {
          bestScore = moveScore;
          p.x = i;
          p.y = j;
        }
      }
    }
  }

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

  logfile = fopen("/tmp/tetrislog", "w");
  if (!logfile) {
    perror("open logfile");
  }

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
}

void update(void) {
  char c;
  Pos pos;
  int moveDone = 0;

  if (read(STDIN_FILENO, &c, 1) == 1) {
    // Reset vars
    game->failedInput = 0;
    game->invalidMove = 0;

    // llog("Current input: '%s'\n", game->input);

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
      llog("-----\n");
      pos = parseInput();
      llog("Pos %d, %d\n", pos.x, pos.y);
      if (game->failedInput == 1) {
        llog("Failed pos\n");
      }
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
  // Draw grid
  printf("%s%s\n      ", CLEAR_SCREEN, REPOS_CURSOR);
  drawGrid(game->grid);
  printf("\n");

  if (game->won) {
    printf("You %s\nPress <Enter> to play again.",
           game->won == 1 ? "won!" : "lose...");
    fflush(stdout);
  } else if (game->aiThinking) {
    printf("AI thinking...\n");
  } else {
    // Draw input buf
    printf("Your move: %s\n",
           game->invalidMove
               ? "Invalid move, cell alreay set or out of bound."
               : (game->failedInput ? "Invalid input" : game->input));
  }
}

int main(void) {
  setup();

  while (1) {
    update();
    draw();
    usleep(16666); // ~60 FPS (1/60 second = 16.666ms)
  }
}
