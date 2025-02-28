extern "C" {
#include "ma/malloc.h"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
}

#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <cerrno>
#include <random>
#include <cassert>
#include <cstring>
#include <unordered_map>
#include <iterator>
#include <iostream>
#include <optional>

#ifndef MA_DUMP
#define MA_DUMP 0
#endif

static std::random_device rd;
static std::mt19937 gen(rd());
static std::unordered_map<void*, size_t> live;
static size_t nalloc = 0;
#if MA_DUMP
static FILE *out;
#endif
constexpr unsigned char fill_byte = 0xbe;

#if MA_DUMP
template <class... Args>
static void dump_print(const char *fmt, Args... args) {
	fprintf(out, fmt, args...);
}

static void dump_file(FILE *dest, FILE *file)
{
	char buffer[4096];

	while (1) {
		size_t nread = fread(buffer, 1, sizeof(buffer), file);
		
		if (nread == 0) {
			assert(feof(file));
			break;
		}
		size_t nwritten = fwrite(buffer, 1, nread, dest);
		assert(nwritten == nread);

	}
	fflush(file);
}

#else
template <class... Args>
static void dump_print(const char * /*unused*/, Args... /*unused*/) {
}
#endif

static size_t gen_malloc_size()
{
	static std::bernoulli_distribution bool_distr(0.5);
	static std::uniform_int_distribution<> small_distr(1, 1016); 
	static std::uniform_int_distribution<> large_distr(1024, 1048576); 

	if (bool_distr(gen))
		return small_distr(gen);
	return large_distr(gen);
}

static void gen_memset(void* p, size_t idx, size_t n)
{
	dump_print("\tmemset(p_%zu, %hhu, %zu);\n", idx, fill_byte, n);
	memset(p, fill_byte, n);
}

static void gen_aligned_alloc()
{
	static std::uniform_int_distribution<> align_distr(5, 16);
	size_t size = gen_malloc_size();

	size_t align = 1 << align_distr(gen);

	dump_print("\tvoid *p_%zu = ma_aligned_alloc(%zu, %zu);\n", nalloc, align, size);
	void *p = ma_aligned_alloc(align, size);
	assert(p);
	gen_memset(p, nalloc, size);
	live[p] = nalloc;

	nalloc += 1;
}

static void gen_malloc()
{
	size_t size = gen_malloc_size();

	dump_print("\tvoid *p_%zu = ma_malloc(%zu);\n", nalloc, size);

	void *p = ma_malloc(size);

	assert(p);
	gen_memset(p, nalloc, size);
	live[p] = nalloc;
	nalloc += 1;
}

static void gen_alloc()
{
	static std::bernoulli_distribution bool_distr(0.9);

	if (bool_distr(gen))
		gen_malloc();
	else
		gen_aligned_alloc();
}

static std::pair<void*, size_t> pop_random_pointer()
{
	assert(!live.empty());

	std::uniform_int_distribution<> distr(0, live.size() - 1);

	size_t idx = distr(gen);
	auto it = live.begin();
	std::advance(it, idx);

	std::pair<void*, size_t> res = *it;

	live.erase(it);

	return res;
}

static void gen_realloc()
{
	if (live.empty())
		return;

	size_t size = gen_malloc_size();
	auto ptr = pop_random_pointer();

	dump_print("\tvoid *p_%zu = ma_realloc(p_%zu, %zu);\n", nalloc, ptr.second, size);

	void *p = ma_realloc(ptr.first, size);
	assert(p);

	gen_memset(p, nalloc, size);
	live[p] = nalloc;
	nalloc += 1;
}

static void gen_free()
{
	if (live.empty())
		return;

	auto ptr = pop_random_pointer();

	dump_print("\tma_free(p_%zu);\n", ptr.second);
	ma_free(ptr.first);
}

static void gen_cmd()
{
	std::uniform_int_distribution cmd(0, 3);

	switch (cmd(gen)) {
	case 0:
		gen_alloc();
		break;
	case 1:
		gen_realloc();
		break;
	case 2:
		gen_free();
		break;
	}
}

static void sighandler(int signum)
{
	if (signum != SIGALRM)
		abort();
	_exit(0);
}

static void start_hammer(std::optional<unsigned> timeout)
{
	if (timeout) {
		struct sigaction action = {};
		action.sa_handler = sighandler,

		assert(!sigemptyset(&action.sa_mask));
		assert(!sigaction(SIGALRM, &action, NULL));
		alarm(*timeout);
	}

	dump_print("#include \"malloc.h\"\n\n");
	dump_print("#include <string.h>\n\n");
	dump_print("int main(void)\n{\n");


	while (true) {
		gen_cmd();
	}
}

#if MA_DUMP
static void dump()
{
	rewind(out);

	FILE *dump = fopen("main.c", "w");
	assert(dump);
	dump_file(dump, out);
}
#endif

int main(int argc, char **argv)
{
#if MA_DUMP
	out = tmpfile();
	setbuf(out, NULL);
#endif

	unsigned int seed = rd();

	if (std::getenv("MA_SEED"))
		seed = std::strtoul(std::getenv("MA_SEED"), NULL, 10);

	gen.seed(seed);

	std::optional<unsigned> timeout;
	if (argc > 1) {
		timeout = std::atol(argv[1]);
	}

	pid_t p = fork();
	if (p == 0) {
		start_hammer(timeout);
		return 0;
	} else if (p < 0) {
		perror("fork");
		return 1;
	}

	int status = 0;
	if (wait(&status) != p) {
		perror("wait");
		return 1;
	}

	dump_print("}\n");

	bool should_dump = false;
	if (WIFSIGNALED(status)) {
		std::cout << "KO: received a signal" << std::endl;
		std::cout << "seed: " << seed << std::endl;
		should_dump = true;
	} else if (!WIFEXITED(status)) {
		std::cout << "KO: did not exit properly" << std::endl;
		std::cout << "seed: " << seed << std::endl;
		should_dump = true;
	} else if (WEXITSTATUS(status) != 0) {
		std::cout << "KO: program did not return 0" << std::endl;
		std::cout << "seed: " << seed << std::endl;
		should_dump = true;
	}

	if (should_dump) {
#if MA_DUMP
		dump();
#endif
		return 1;
	}
	return 0;
}
