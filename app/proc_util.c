#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "log.h"
#include "proc_util.h"

/*
 * double fork.
 * first child spawns the handler process, waits for closure of STDOUT, and exits.
 * parent waits for first child using waitpid.
 */

/* this runs in the first child, exit is called after return */
static void spawn_and_wait_for_stdout(const char *exe, char *const args[])
{
	int stdout_pipe[2];
	if (pipe(stdout_pipe) == -1) {
		log_error("pipe failed: %s!", strerror(errno));
		return;
	}

	pid_t pid = fork();
	if (pid == -1) {
		log_error("fork failed: %s!", strerror(errno));
		return;
	}
	else if (pid == 0) {
		while ((dup2(stdout_pipe[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
		close(stdout_pipe[1]);
		close(stdout_pipe[0]);
		close(STDIN_FILENO);

		execvp(exe, args);
		log_error("error spawning '%s': %s", exe, strerror(errno));
		return;
	}

	close(stdout_pipe[1]);

	// wait for stdout to close
	char buf[512];
	ssize_t rlen;
	while ((rlen = read(stdout_pipe[0], buf, sizeof(buf)) > 0)) ;
	if (rlen == -1) {
		log_error("read from child stdout ouch: %s", strerror(errno));
		return;
	}

	close(stdout_pipe[0]);

	// try to get an exit-status
	int wstatus;
	int res = waitpid(pid, &wstatus, WNOHANG);
	if (res == -1) {
		log_error("inner waitpid ouch: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!WIFEXITED(wstatus))
		exit(EXIT_SUCCESS);

	exit(WEXITSTATUS(wstatus));
}

bool fork_and_wait_for_stdout(const char *exe, ...)
{
	bool res = false;

	va_list va;
	va_start(va, exe);

	// should suffice for this application
	char *args[10];

	args[0] = strdupa(exe);
	size_t arg_count = 1;

	const char *arg;
	while ((arg = va_arg(va, const char *))) {
		args[arg_count] = strdupa(arg);

		arg_count++;

		// need the last place for NULL
		if (arg_count == (sizeof(args) / sizeof(args[0]) - 1))
			goto cleanup_va;
	};

	args[arg_count] = NULL;

	pid_t pid = fork();
	if (pid == -1) {
		log_error("fork failed: %s!", strerror(errno));
		goto cleanup_va;
	}
	else if (pid == 0) {
		spawn_and_wait_for_stdout(exe, args);
		exit(EXIT_FAILURE);
	}

	int wstatus;
	if (waitpid(pid, &wstatus, 0) == -1) {
		log_error("outer waitpid ouch: %s", strerror(errno));
		goto cleanup_va;
	}

	if (!WIFEXITED(wstatus)) {
		log_error("first child did not exit");
		goto cleanup_va;
	}

	if (WEXITSTATUS(wstatus) != 0) {
		log_error("%s exit status %d", exe, WEXITSTATUS(wstatus));
		goto cleanup_va;
	}

	res = true;

cleanup_va:
	va_end(va);

	return res;
}
