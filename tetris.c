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

#define N 20
#define M 26
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

void cleanup(void) {
  printf("%s%s%s\n", SHOW_CURSOR, CLEAR_SCREEN, REPOS_CURSOR);
  fflush(stdout);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &origterm);
}

void signal_hander(int signum) {
  cleanup();
  exit(0);
}

Pos parseInput() {
  Pos p = {-1, -1};
  int x;
  char ys;

  if (sscanf(game->input, " %d %c", &x, &ys) == 2) {
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

  atexit(cleanup);

  printf("%s%s%s", HIDE_CURSOR, CLEAR_SCREEN, REPOS_CURSOR);
  fflush(stdout);

  game = malloc(sizeof(Game));
  memset(game->grid, 0, M * N * sizeof(int));
  memset(game->input, 0, INPUT_BUF_LEN);
  game->failedInput = 0;
}

// Returns:
// - 0 if no one won
// - 1 if player won
// - 2 if ai won
int checkWin(void) {
  int inarow = 0;
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < M; j++) {
      if (game->grid[i][j] == 0)
        continue;

      int player = game->grid[i][j];

      // Horiz
      inarow = 1;
      int maxh = j + 3;
      for (int h = j + 1; h <= maxh && h < M; h++) {
        if (game->grid[i][h] == player)
          inarow++;
      }
      if (inarow == 4)
        return player;

      // Vert
      inarow = 1;
      int maxv = i + 3;
      for (int v = i + 1; v <= maxv && v < N; v++) {
        if (game->grid[v][j] == player)
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
        if (game->grid[v][h] == player)
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
        if (game->grid[v][h] == player)
          inarow++;
      }
      if (inarow == 4)
        return player;
    }
  }

  return 0;
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
    if (game->grid[pos.x][pos.y] > 0) {
      game->invalidMove = 1;
      return;
    }
    game->grid[pos.x][pos.y] = 1;

    int won = checkWin();
    if (won > 0) {
      llog("Player %d has WON!!!\n", won);
    }
  }
}

void draw(void) {
  // Draw grid
  printf("%s%s\n      ", CLEAR_SCREEN, REPOS_CURSOR);
  for (int j = 0; j < M && j < 26; j++) {
    printf("%c ", 'A' + j);
  }
  printf("\n");
  for (int i = 0; i < N; i++) {
    printf(" %02d |", i + 1);
    for (int j = 0; j < M; j++) {
      printf(" %c", game->grid[i][j] ? 'X' : '.');
    }
    printf("\n");
  }
  printf("\n");

  // Draw input buf
  printf("Choose cell (e.g. 8 D <enter>): %s\n",
         game->invalidMove
             ? "Cell already set"
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
