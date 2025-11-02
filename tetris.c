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

struct termios origterm;

typedef struct Pos {
  int x, y;
} Pos;

typedef struct Game {
  int grid[N][M];
  char input[INPUT_BUF_LEN];
  int failedInput;
  int invalidMove;
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

// Returns:
// - 0 if no one won
// - 1 if player won
// - 2 if ai won
int checkWin(int grid[N][M]) {
  int inarow = 0;
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < M; j++) {
      if (grid[i][j] == 0)
        continue;

      int player = grid[i][j];

      // Horiz
      inarow = 1;
      int maxh = j + 3;
      for (int h = j + 1; h <= maxh && h < M; h++) {
        if (grid[i][h] == player)
          inarow++;
      }
      if (inarow == 4)
        return player;

      // Vert
      inarow = 1;
      int maxv = i + 3;
      for (int v = i + 1; v <= maxv && v < N; v++) {
        if (grid[v][j] == player)
          inarow++;
      }
      if (inarow == 4)
        return player;

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
        return player;

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
        return player;
    }
  }

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

    int won = checkWin(game->grid);
    if (won > 0) {
      llog("Player %d has WON!!!\n", won);
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

  // Draw input buf
  printf("Your move: %s\n",
         game->invalidMove
             ? "Invalid move, cell alreay set or out of bound."
             : (game->failedInput ? "Invalid input" : game->input));
}

int main(void) {
  setup();

  while (1) {
    update();
    draw();
    usleep(16666); // ~60 FPS (1/60 second = 16.666ms)
  }
}
