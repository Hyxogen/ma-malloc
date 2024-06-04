NAME		:= malloc.so

CC		:= clang
CXX		:= clang++

LIBFT_DIR	:= libft
LIBFT_LIB	:= $(LIBFT_DIR)/libft.a

SRC_DIR		:= src
OBJ_DIR		:= build
DEP_DIR		:= build

SRC_FILES	:= $(shell find $(SRC_DIR) -name '*.c')
OBJ_FILES	:= $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC_FILES))
DEP_FILES	:= $(patsubst $(SRC_DIR)/%.c,$(DEP_DIR)/%.d,$(SRC_FILES))

CFLAGS		:= -Wall -Wextra -O0 -g3 -DMA_TRACES=1 -MMD -Iinclude
LFLAGS		:= -shared -lpthread

all: $(NAME)

$(NAME): $(OBJ_FILES) $(LIBFT_LIB)
	@echo $(SRC_FILES)
	$(CC) $< $(LIBFT_LIB) $(LFLAGS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c Makefile
	@mkdir -p $(@D)
	$(CC) -c $< -o $@ $(CFLAGS) -I$(LIBFT_DIR)/include

test: CFLAGS += -DUSE_FT_PREFIX=1
test: $(OBJ_FILES) hammer.cc Makefile
	$(CXX) hammer.cc -std=c++20 $< $(LIBFT_LIB) -g3 -O3 -Wall -Wextra -o $@

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


-include $(DEP_FILES)
.PHONY: all clean fclean re
