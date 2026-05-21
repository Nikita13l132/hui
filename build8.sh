#!/bin/bash
gcc -Wall -Wextra -o server server.c -lpthread
gcc -Wall -Wextra -o client client.c -lpthread
