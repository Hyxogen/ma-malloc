NAME		:= malloc.so

CC		:= clang
CXX		:= clang++

LIBFT_DIR	:= libft
LIBFT_LIB	:= $(LIBFT_DIR)/libft.a

SRC_FILES	:= malloc.c
OBJ_FILES	:= malloc.o

CFLAGS		:= -Wall -Wextra -Og -g3 -fno-inline
LFLAGS		:= -shared -lpthread

all: $(NAME)

$(NAME): $(OBJ_FILES) $(LIBFT_LIB)
	$(CC) $< $(LIBFT_LIB) $(LFLAGS) -o $@

%.o: %.c Makefile
	$(CC) -c $< -o $@ $(CFLAGS) -I$(LIBFT_DIR)/include

test: $(OBJ_FILES) hammer.cc Makefile
	$(CXX) hammer.cc -std=c++20 $< $(LIBFT_LIB) -g3 -Og -Wall -Wextra -o $@

$(LIBFT_LIB): Makefile
	@${MAKE} -C $(LIBFT_DIR) san=none

clean:
	rm -f $(OBJ_FILES)
	rm -f $(LIBFT_LIB)

fclean:
	@${MAKE} clean
	@${MAKE} -C $(LIBFT_LIB) fclean
	rm -f $(NAME)

re:
	@${MAKE} fclean
	@${MAKE}


.PHONY: all clean fclean re
