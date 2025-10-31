#include <math.h>
#include <signal.h>
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
} Game;

Game *game;
FILE *logfile;

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
  game->failedInput = 0;
}

void update(void) {
  char c;
  Pos pos;
  int moveDone = 0;

  if (read(STDIN_FILENO, &c, 1) == 1) {
    // Reset vars
    game->failedInput = 0;

    if (c == '\n') {
      fprintf(logfile, "-----\n");
      pos = parseInput();
      fprintf(logfile, "Pos %d, %d\n", pos.x, pos.y);
      if (game->failedInput == 1) {
        fprintf(logfile, "Failed pos\n");
      }
      moveDone = 1;
      fflush(logfile);
      memset(game->input, 0, INPUT_BUF_LEN);
    } else {
      int s = strlen(game->input);
      if (s < INPUT_BUF_LEN - 1) {
        game->input[s] = c;
        game->input[s + 1] = '\0';
      }
    }
  }

  if (moveDone) {
    game->grid[pos.x][pos.y] = 1;
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
         game->failedInput ? "Invalid input" : game->input);
}

int main(void) {
  setup();

  while (1) {
    update();
    draw();
    usleep(16666); // ~60 FPS (1/60 second = 16.666ms)
  }
}
