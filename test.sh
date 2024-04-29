#!/usr/bin/bash
set -ex
cc main.c malloc.c libft/libft.a -Ilibft/include -g3 -O0
