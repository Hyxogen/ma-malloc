extern "C" {
#include "malloc.h"
#include <unistd.h>
#include <sys/wait.h>
}

#include <vector>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <cerrno>
#include <random>
#include <cassert>
#include <cstring>

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

int main()
{
	FILE *out = tmpfile();
	setbuf(out, NULL);

	pid_t p = fork();
	if (p == 0) {
		fprintf(out, "#include \"malloc.h\"\n\n");
		fprintf(out, "int main(void)\n{\n");

		std::random_device rd;
		std::mt19937 gen(rd());

		std::bernoulli_distribution bool_distr(0.5);
		std::uniform_int_distribution<> small_distr(1, 1016); 
		std::uniform_int_distribution<> large_distr(1024, 1048576); 

		std::vector<std::pair<void*, int>> live;
		int num = 0;
		int count = large_distr(gen);

		for (int i = 0; i < count; ++i) {
			if (bool_distr(gen)) {
				size_t s = 0;

				if (bool_distr(gen)) {
					s = small_distr(gen);
				} else {
					s = large_distr(gen);
				}

				fprintf(out, "\tvoid *p_%i = ft_malloc(%zu);\n", num, s);

				void *p = ft_malloc(s);

				const unsigned char fill_byte = 0xbe;

				fprintf(out, "\tmemset(p_%i, %hhu, %zu);\n", num, fill_byte, s);
				memset(p, fill_byte, s);
				if (p) {
					live.push_back({p, num});

				}
				num += 1;
			} else if (!live.empty()) {
				std::uniform_int_distribution<> free_distr(0, live.size() - 1);
				size_t idx = free_distr(gen);
				auto [p, tofree] = live[idx];

				fprintf(out, "\tft_free(p_%i);\n", tofree);

				live.erase(live.begin() + idx);
				ft_free(p);

			}
		}

		return 0;
	} else if (p < 0) {
		perror("fork");
		return 1;
	}

	int status = 0;
	if (wait(&status) != p && errno != ECHILD) {
		perror("wait");
		return 1;
	}

	fprintf(out, "}\n");

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		rewind(out);

		FILE *dump = fopen("main.c", "w");
		assert(dump);
		dump_file(dump, out);
		return 1;
	}
	return 0;
}
