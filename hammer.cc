extern "C" {
#include "malloc.h"
#include <unistd.h>
#include <sys/wait.h>
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

static std::random_device rd;
static std::mt19937 gen(rd());
static std::unordered_map<void*, size_t> live;
static size_t nalloc = 0;
static FILE *out;
constexpr unsigned char fill_byte = 0xbe;

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
	fprintf(out, "\tmemset(p_%zu, %hhu, %zu);\n", idx, fill_byte, n);
	memset(p, fill_byte, n);
}

static void gen_malloc()
{
	size_t size = gen_malloc_size();

	fprintf(out, "\tvoid *p_%zu = ma_malloc(%zu);\n", nalloc, size);

	void *p = ma_malloc(size);

	assert(p);
	gen_memset(p, nalloc, size);
	live[p] = nalloc;
	nalloc += 1;
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

	fprintf(out, "\tvoid *p_%zu = ma_realloc(p_%zu, %zu);\n", nalloc, ptr.second, size);

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

	fprintf(out, "\tma_free(p_%zu);\n", ptr.second);
	ma_free(ptr.first);
}

static void gen_cmd()
{
	std::uniform_int_distribution cmd(0, 3);

	switch (cmd(gen)) {
	case 0:
		gen_malloc();
		break;
	case 1:
		gen_realloc();
		break;
	case 2:
		gen_free();
		break;
	}
}

static void start_hammer()
{
	static std::uniform_int_distribution<> count_distr(10, 10480); 

	fprintf(out, "#include \"malloc.h\"\n\n");
	fprintf(out, "#include <string.h>\n\n");
	fprintf(out, "int main(void)\n{\n");

	int count = count_distr(gen);

	for (int i = 0; i < count; ++i) {
		gen_cmd();
	}
}

static void dump()
{
	rewind(out);

	FILE *dump = fopen("main.c", "w");
	assert(dump);
	dump_file(dump, out);
}

int main()
{
	out = tmpfile();
	setbuf(out, NULL);


	pid_t p = fork();
	if (p == 0) {
		start_hammer();
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

	fprintf(out, "}\n");

	if (WIFSIGNALED(status)) {
		std::cout << "KO: received a signal" << std::endl;
		dump();
		return 1;
	} else if (!WIFEXITED(status)) {
		std::cout << "KO: did not exit properly" << std::endl;
		dump();
		return 1;
	} else if (WEXITSTATUS(status) != 0) {
		std::cout << "KO: program did not return 0" << std::endl;
		dump();
		return 1;
	}
	return 0;
}
