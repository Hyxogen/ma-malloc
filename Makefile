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

CFLAGS		:= -Wall -Wextra -O3 -g -DMA_TRACES=0 -MMD -Iinclude -I$(LIBFT_DIR)/include -DMA_COMPILE_AS_LIBC=0 -fPIC -DMA_SEGREGATED_BESTFIT=1 -DMA_TRACK_CHUNKS=1
LFLAGS		:= -shared -lpthread -flto

all: $(NAME)

$(NAME): $(OBJ_FILES) $(LIBFT_LIB)
	@echo $(SRC_FILES)
	$(CC) $(OBJ_FILES) $(LIBFT_LIB) $(LFLAGS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c Makefile
	@mkdir -p $(@D)
	$(CC) -c $< -o $@ $(CFLAGS)

stress: $(OBJ_FILES) tests/stress.cc Makefile
	$(CXX) tests/stress.cc -std=c++20 $(OBJ_FILES) $(LIBFT_LIB) -g -O3 -Wall -Wextra -o $@ -Iinclude -flto

debug: $(OBJ_FILES)
	@echo $(OBJ_FILES)
	$(CC) main.c $(CFLAGS) $(OBJ_FILES) $(LIBFT_LIB) -g3 -O0 -Wall -Wextra -o $@

$(LIBFT_LIB): Makefile
	@${MAKE} -C $(LIBFT_DIR) san=none

fmt:
	clang-format -i $(SRC_FILES) $(shell find include/ -type f -name '*.h')

clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(DEP_DIR)

fclean:
	@${MAKE} clean
	@${MAKE} -C $(LIBFT_LIB) fclean
	rm -f $(NAME)

re:
	@${MAKE} fclean
	@${MAKE}


-include $(DEP_FILES)
.PHONY: all clean fclean re debug
