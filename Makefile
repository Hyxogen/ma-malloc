ifeq ($(HOSTTYPE),)
	HOSTTYPE := $(shell uname -m)_$(shell uname -s)
endif

CFLAGS		:= -Wall -Wextra -MMD -Iinclude -fPIC -DMA_TRACES=0
LFLAGS		:=

NAME		:= libmamalloc
SUBJECT_NAME	:= libft_malloc_$(HOSTTYPE)

SRC_DIR		:= src
OBJ_DIR		:= build
DEP_DIR		:= build

SRC_FILES	:= \
		   src/aligned_alloc.c src/arena.c src/calloc.c src/chunk.c src/common.c src/debug.c src/extensions.c \
		   src/free.c src/libc.c src/malloc.c src/opts.c src/realloc.c src/state.c src/thread.c
HDR_FILES	:= $(shell find include/ -name '*.h')

LIBFT_DIR	:= libft
LIBFT_LIB	:= $(LIBFT_DIR)/libft.a

PREFIX		?= /usr

ifndef threads:
	threads	:= c11
endif

ifndef config
	config	:= distr
endif

ifndef platform:
	platform := posix
endif

ifeq ($(platform),posix)
	SRC_FILES += src/sysdeps/linux.c
else ifeq ($(platform),baremetal)
	CFLAGS += -DMA_PLATFORM_BARE=1
else
$(error "Unknown platform '$(platfrom)'. Available: posix, baremetal")
endif

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
	CFLAGS	+= -DMA_CHECK_SELF=0
else
$(error "Unknown check '$(check)'. Available: all, simple, none")
endif

OBJ_FILES	:= $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC_FILES))
DEP_FILES	:= $(patsubst $(SRC_DIR)/%.c,$(DEP_DIR)/%.d,$(SRC_FILES))

$(NAME).so: $(OBJ_FILES)
	$(CC) $(OBJ_FILES) $(LFLAGS) -shared -o $@

$(NAME).a: $(OBJ_FILES)
	ar rcs $@ $(OBJ_FILES)

# It's better not to use this rule. It's just so it fits the requirements of the
# subject.
mandatory: CFLAGS	+= \
	-DMA_SEGREGATED_BESTFIT=1 -DMA_TRACK_CHUNKS=1 -DMA_COMPILE_AS_LIBC=1 \
	-DMA_USE_LIBFT=1 -DMA_USE_MUTEX=MA_PTHREAD_MUTEX
mandatory: LFLAGS	+= -lpthread
mandatory: NAME		= $(SUBJECT_NAME)
mandatory: CFLAGS	+= -I$(LIBFT_DIR)/include
mandatory: LFLAGS	+= -L$(LIBFT_DIR) -lft -shared
mandatory: $(LIBFT_LIB) $(OBJ_FILES)
	$(CC) $(OBJ_FILES) $(LFLAGS) -shared -o $(SUBJECT_NAME).so

# You found the bonus rule! Don't use it. It's useless.
# It only really adds a few debug features (like dumping allocations) and just
# adds more overhead.
# Don't use it.
bonus: CFLAGS += -DFT_BONUS=1
bonus: mandatory

$(PREFIX)/%.h: %.h
	install -Dm644 $< $@

install-headers: $(addprefix $(PREFIX)/,$(HDR_FILES))

install: $(NAME).a
	${MAKE} install-headers
	install -Dm644 $< $(PREFIX)/lib/libmamalloc.a

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c Makefile
	@mkdir -p $(@D)
	$(CC) -c $< -o $@ $(CFLAGS)

stress: $(OBJ_FILES) $(LIBFT_LIB) tests/stress.cc Makefile
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
	rm -f $(NAME).so
	rm -f $(NAME).a
	rm -f $(NAME).a
	rm -f $(SUBJECT_NAME).so
	rm -f stress

re:
	@${MAKE} fclean
	@${MAKE}

help:
	@echo "rules:"
	@echo "  help        this help message"
	@echo "  normal      build fully functional malloc library (recommended)"
	@echo "  mandatory   build malloc as required by the 42 subject (not recommended)"
	@echo "  stress      build malloc stress tester"
	@echo "  fmt         format the source code according to .clang-format"
	@echo "  clean       remove all intermediary files"
	@echo "  fclean      remove all intermediary files as well as built programs/libraries"
	@echo "  re          fclean and then make"
	@echo ""
	@echo "arguments:"
	@echo "  config="
	@echo "    debug     few optimizations enabled, with debug symbols, useful for debugging"
	@echo "    release   optimizations enabled, with debug symbols, useful for testing"
	@echo "    distr     all optimizations enabled, no debug symbols, meant for deployment"
	@echo "  check="
	@echo "    all       enable all self tests, really slow, useful for debugging"
	@echo "    simple    enable only cheap self tests, slightly slower, useful for testing"
	@echo "    none      disable all self tests, fastest, useful for deployment"

-include $(DEP_FILES)
.PHONY: all clean fclean re debug bonus mandatory normal help
