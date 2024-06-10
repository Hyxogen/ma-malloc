ifeq ($(HOSTTYPE),)
	HOSTTYPE := $(shell uname -m)_$(shell uname -s)
endif

NAME		:= libft_malloc_$(HOSTTYPE).so

CC		?= cc
CXX		?= c++

LIBFT_DIR	:= libft
LIBFT_LIB	:= $(LIBFT_DIR)/libft.a

SRC_DIR		:= src
OBJ_DIR		:= build
DEP_DIR		:= build

SRC_FILES	:= $(shell find $(SRC_DIR) -name '*.c')
OBJ_FILES	:= $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC_FILES))
DEP_FILES	:= $(patsubst $(SRC_DIR)/%.c,$(DEP_DIR)/%.d,$(SRC_FILES))

ifndef config
	config	:= distr
endif

CFLAGS		:= -Wall -Wextra -MMD -Iinclude -I$(LIBFT_DIR)/include -fPIC -DMA_TRACES=0
LFLAGS		:= -shared -lpthread

ifeq ($(config),debug)
	CFLAGS	+= -O0 -g3
else ifeq ($(config),release)
	CFLAGS	+= -O1 -g -fno-inline
else ifeq ($(config),distr)
	CFLAGS	+= -O3 -g0
	LFLAGS	+= -flto
else
$(error "Unknown config '$(config)'. Available: debug, release, distr")
endif

ifndef check
	check	:= none
endif

ifeq ($(check),all)
	CFLAGS	+= -DMA_CHECK_SELF=1 -DMA_TRACK_CHUNKS=1
else ifeq ($(check),simple)
	CFLAGS	+= -DMA_CHECK_SELF=0
else ifeq ($(check),none)
	CFLAGS	+= -DMA_CHECK_SELF=0 -DFT_NDEBUG
else
$(error "Unknown check '$(check)'. Available: all, simple, none")
endif

all: mandatory

mandatory: CFLAGS += -DMA_SEGREGATED_BESTFIT=1 -DMA_TRACK_CHUNKS=1 -DMA_COMPILE_AS_LIBC=1
mandatory: $(NAME)

bonus: CFLAGS += FT_BONUS=1
bonus: mandatory

normal: CFLAGS += -DMA_SEGREGATED_BESTFIT=0 -DMA_COMPILE_AS_LIBC=1
normal: $(NAME)

$(NAME): $(OBJ_FILES) $(LIBFT_LIB)
	@echo $(SRC_FILES)
	$(CC) $(OBJ_FILES) $(LIBFT_LIB) $(LFLAGS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c Makefile
	@mkdir -p $(@D)
	$(CC) -c $< -o $@ $(CFLAGS)

stress: $(OBJ_FILES) tests/stress.cc Makefile
	$(CXX) tests/stress.cc -std=c++20 $(OBJ_FILES) $(LIBFT_LIB) -g3 -O0 -Wall -Wextra -o $@ -Iinclude -flto

debug: $(OBJ_FILES)
	@echo $(OBJ_FILES)
	$(CC) main.c $(CFLAGS) $(OBJ_FILES) $(LIBFT_LIB) -g3 -O3 -Wall -Wextra -o $@

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
.PHONY: all clean fclean re debug bonus mandatory normal
