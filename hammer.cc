extern "C" {
#include "malloc.h"
#include <unistd.h>
#include <sys/wait.h>
}

#include <vector>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <cstdio>
#include <cerrno>

int main(int argc, char **argv)
{
	int seed = 0;

	if (argc < 2)
		seed = std::time(NULL);
	else
		seed = std::atoi(argv[1]);

	std::srand(seed);
	std::cout << "#include \"malloc.h\"" << std::endl << std::endl;

	std::cout << "int main()" << std::endl << "{" << std::endl;

	pid_t p = fork();

	if (p == 0) {
		std::vector<std::pair<void*, int>> live;
		int num = 0;
		int count = std::rand() % 1024;

		for (int i = 0; i < count; ++i) {
			if (((unsigned) std::rand()) & 1) {
				std::cout << "\t";

				size_t s = std::rand() % 1048576;
				std::cout << "void *p_" << num << " = ft_malloc(" << s << ");" << std::endl;

				void *p = ft_malloc(s);
				if (p) {
					live.push_back({p, num});

				}
				num += 1;
			} else if (!live.empty()) {
				size_t idx = std::rand() % live.size();
				auto [p, tofree] = live[idx];

				std::cout << "\tft_free(p_" << tofree << ");" << std::endl;

				live.erase(live.begin() + idx);
				ft_free(p);

			}
		}
	} else if (p < 0) {
		perror("fork");
		return 1;
	}

	int status = 0;
	if (wait(&status) != p && errno != ECHILD) {
		perror("wait");
		return 1;
	}

	std::cout << "}" << std::endl;
	std::cout << "//seed: " << seed << std::endl;

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		return 1;
	}
	return 0;
}
