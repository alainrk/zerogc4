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

struct termios origterm;

typedef struct Pos {
  int x, y;
} Pos;

typedef struct Game {
  int grid[N][M];
  char input[INPUT_BUF_LEN];
  int failedInput;
  int invalidMove;
  int won;
} Game;

Game *game;
FILE *logfile;

void llog(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(logfile, format, args);
  fflush(logfile);
  va_end(args);
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

Pos aiPosition(void) {
  Pos p = {-1, -1};

  // For each possible valid ai move
  // Calculate the possible move of the opponent in response
  // Repeat down for N depth level
  // Calculate the score of this game
  // Choose the next move based on that

  return p;
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
// The heuristic is very simple and works as follow:
// - Going Top -> Bottom, Left -> Right
// - If we are on a cell of user X, it keeps on every direction
//   (Right, Down, Down-Right, Down-Left).
//  - We set the "local" score to 1
//  - We set a multiplier starting at 1
//  - At each step the totalScore for the current user is incremented as
//  follows:
//      - userScore += (localScore * multiplier)
//  - If we are on a empty cell the multiplier returns to 1
//  - If we are on a sequence, the multiplier is incremented by 0.5
//  - If we are on a opponent-filled sequence
int assignScoreToGrid(int grid[N][M]) {
  int inarow = 0;
  int player = 0;
  int score[2] = {0, 0};

  unsigned short int visited[2][N][M];

  memset(visited, 0, 2 * sizeof(unsigned short int) * N * M);

  for (int i = 0; i < N; i++) {
    for (int j = 0; j < M; j++) {
      if (grid[i][j] == 0)
        continue;

      player = grid[i][j];

      // Don't recalculate from here for this player again
      if (visited[player - 1][i][j])
        continue;

      visited[player - 1][i][j] = 1;

      // Horiz
      inarow = 1;
      for (int h = j + 1; h < M; h++) {
        visited[player - 1][i][h] = 1;
        if (grid[i][h] == player) {
          inarow++;
          score[player - 1] += MULTIPLIER_IN_A_ROW * inarow;
          if (inarow == 4)
            goto won;
        } else if (grid[i][h] == 0) {
          inarow = 0;
          score[player - 1] += 1;
        } else {
          inarow = 0;
        }
      }

      // Vert
      inarow = 1;
      for (int v = i + 1; v < N; v++) {
        visited[player - 1][v][j] = 1;
        if (grid[v][j] == player) {
          inarow++;
          score[player - 1] += MULTIPLIER_IN_A_ROW * inarow;
          if (inarow == 4)
            goto won;
        } else if (grid[v][j] == 0) {
          inarow = 0;
          score[player - 1] += 1;
        } else {
          inarow = 0;
        }
      }

      // Diag down-right
      inarow = 1;
      for (int h = j + 1, v = i + 1; v < N && h < M; v++, h++) {
        visited[player - 1][v][h] = 1;
        if (grid[v][h] == player) {
          inarow++;
          score[player - 1] += MULTIPLIER_IN_A_ROW * inarow;
          if (inarow == 4)
            goto won;
        } else if (grid[v][h] == 0) {
          inarow = 0;
          score[player - 1] += 1;
        } else {
          inarow = 0;
        }
      }

      // Diag down-left
      inarow = 1;
      for (int h = j - 1, v = i + 1; v < N && h >= 0; v++, h--) {
        visited[player - 1][v][h] = 1;
        if (grid[v][h] == player) {
          inarow++;
          score[player - 1] += MULTIPLIER_IN_A_ROW * inarow;
          if (inarow == 4)
            goto won;
        } else if (grid[v][h] == 0) {
          inarow = 0;
          score[player - 1] += 1;
        } else {
          inarow = 0;
        }
      }
    }
  }

  // TODO
  return 0;

won:
  return player == 0 ? 0 : (player == 1 ? -1000 : 1000);
}

// Returns a number from -1000 to 1000.
//  - Negative number if the user is in advantage
//  - Positive number if the AI is in advantage
// |res| == 1000 if either the user or the AI has won in the grid.
int _assignScoreToGrid(int grid[N][M]) {
  int inarow = 0;
  int player = 0;

  for (int i = 0; i < N; i++) {
    for (int j = 0; j < M; j++) {
      if (grid[i][j] == 0)
        continue;

      player = grid[i][j];

      // Horiz
      inarow = 1;
      int maxh = j + 3;
      for (int h = j + 1; h <= maxh && h < M; h++) {
        if (grid[i][h] == player)
          inarow++;
      }
      if (inarow == 4)
        goto won;

      // Vert
      inarow = 1;
      int maxv = i + 3;
      for (int v = i + 1; v <= maxv && v < N; v++) {
        if (grid[v][j] == player)
          inarow++;
      }
      if (inarow == 4)
        goto won;

      // Diag down-right
      inarow = 1;
      maxv = i + 3;
      maxh = j + 3;
      for (int h = j + 1, v = i + 1; h <= maxh && v <= maxv && v < N && h < M;
           v++, h++) {
        if (grid[v][h] == player)
          inarow++;
      }
      if (inarow == 4)
        goto won;

      // Diag down-left
      inarow = 1;
      maxv = i + 3;
      int minh = j - 3;
      for (int h = j - 1, v = i + 1; h >= minh && v <= maxv && v < N && h >= 0;
           v++, h--) {
        if (grid[v][h] == player)
          inarow++;
      }
      if (inarow == 4)
        goto won;
    }
  }

  return 0;

won:
  return player == 0 ? 0 : (player == 1 ? -1000 : 1000);
}

int checkWin(int grid[N][M]) {
  int score = assignScoreToGrid(grid);
  return score == 0 ? 0 : (score == -1000 ? 1 : 2);
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
}

void update(void) {
  char c;
  Pos pos;
  int moveDone = 0;

  if (read(STDIN_FILENO, &c, 1) == 1) {
    // Reset vars
    game->failedInput = 0;
    game->invalidMove = 0;

    llog("Current input: '%s'\n", game->input);

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

    int winningPlayer = checkWin(game->grid);
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
      printf(" %c", grid[i][j] ? 'X' : '.');
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
