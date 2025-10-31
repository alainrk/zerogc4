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
typedef struct Game {
  int grid[N][M];
  char input[INPUT_BUF_LEN];
} Game;

Game *game;

void cleanup(void) {
  printf("%s%s%s\n", SHOW_CURSOR, CLEAR_SCREEN, REPOS_CURSOR);
  fflush(stdout);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &origterm);
}

void signal_hander(int signum) {
  cleanup();
  exit(0);
}

void setup(void) {
  struct termios raw;
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
}

void update(void) {
  char c;
  if (read(STDIN_FILENO, &c, 1) == 1) {
    // Handle input character c
    int s = strlen(game->input);
    if (s < INPUT_BUF_LEN - 1) {
      game->input[s] = c;
      game->input[s + 1] = '\0';
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
      printf(" %d", game->grid[i][j]);
    }
    printf("\n");
  }
  printf("\n");

  // Draw input buf
  printf("%s\n", game->input);
}

int main(void) {
  setup();

  while (1) {
    update();
    draw();
    usleep(16666); // ~60 FPS (1/60 second = 16.666ms)
  }
}
