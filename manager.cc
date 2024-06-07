extern "C" {
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
}
#include <unordered_set>
#include <random>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string.h>

#define MEAN 60.0f
#define DEV 10.0f

extern char **environ;
const static char *tester_name;

static unsigned random_timeout()
{
	static std::random_device rd;
	static std::mt19937 gen(rd());

	static std::normal_distribution distr(MEAN, DEV); 

	return (unsigned) std::clamp(distr(gen), 10.0f, 300.0f);
}

static pid_t spawn_tester()
{
	unsigned timeout = random_timeout();

	pid_t pid = fork();
	assert(pid >= 0);
	if (pid == 0) {
		char buf[128];
		char *argv[3] = { strdup(tester_name), buf, NULL };

		snprintf(buf, sizeof(buf), "%u", timeout);
		execve(tester_name, argv, environ);
		perror("execve");
		assert(0);
	} else {
		return pid;
	}
}

int main(int argc, char **argv)
{
	if (argc <= 1) {
		std::cerr << "specify tester path" << std::endl;
		return 0;
	}
	tester_name = argv[1];

	std::unordered_set<pid_t> pids;

	size_t jobs = 12;

	while (true) {
		while (pids.size() < jobs) {
			pids.insert(spawn_tester());
		}

		pid_t pid;
		int status;
		if ((pid = waitpid(-1, &status, 0)) != -1) {
			if (WIFSIGNALED(status) || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
				for (auto pid : pids) {
					kill(pid, SIGKILL);
				}
				std::cerr << "KO" << std::endl;
				return 1;
			}
			pids.erase(pid);
		}
	}
}
