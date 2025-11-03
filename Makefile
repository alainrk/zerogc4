build:
	gcc -lpthread -Wall -Wextra -O2 -pedantic -o bin/game game.c 

build-log:
	gcc -DLOG_ENABLED=1 -lpthread -Wall -Wextra -O0 -g3 -pedantic -o bin/game-log game.c

run: build
	./bin/game 4

run-log: build-log
	./bin/game-log 4

