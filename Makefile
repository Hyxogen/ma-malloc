NAME		:= malloc.so

LIBFT_DIR	:= libft
LIBFT_LIB	:= $(LIBFT_DIR)/libft.a

SRC_FILES	:= malloc.c
OBJ_FILES	:= malloc.o

CFLAGS		:= -Wall -Wextra -O0 -g3
LFLAGS		:= -shared

all: $(NAME)

$(NAME): $(OBJ_FILES) $(LIBFT_LIB)
	$(CC) $< $(LIBFT_LIB) $(LFLAGS) -o $@

%.o: %.c Makefile
	$(CC) -c $< -o $@ $(CFLAGS) -I$(LIBFT_DIR)/include

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
