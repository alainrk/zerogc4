build:
	gcc -lpthread -Wall -Wextra -O2 -pedantic -o bin/game game.c 

run: build
	./bin/game 4

