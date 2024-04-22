#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "include/snaketongs_subproc.h"

#if __STDC_VERSION__ >= 201112L
#define noreturn _Noreturn
#else
#ifdef __cplusplus
#define noreturn [[noreturn]]
#else
#warning "no noreturn"
#define noreturn
#endif
#endif

#ifdef __GNUC__
#define noinline __attribute__((noinline))
#else
#define noinline
#endif

static const char python_script[] =
#include "entry.py.str.h"
;

struct snaketongs_impl {
	pid_t pid;
	FILE *cpp_to_py;
	FILE *py_to_cpp;
	bool err;
};

enum {
	ReadEnd,
	WriteEnd,
};

enum {
	ForkError = -1,
	ForkChild = 0,
};

static noinline noreturn void exec_python(const char *python, int cpp_to_py, int py_to_cpp, int int_size) {
	if(!python || !*python)
		python = getenv("PYTHON");
	if(!python || !*python)
		python = "python3";

	char cpp_to_py_decimal[3 * sizeof cpp_to_py];
	char py_to_cpp_decimal[3 * sizeof py_to_cpp];
	char int_size_decimal[3 * sizeof int_size];

	sprintf(cpp_to_py_decimal, "%i", cpp_to_py);
	sprintf(py_to_cpp_decimal, "%i", py_to_cpp);
	sprintf(int_size_decimal, "%i", int_size);

	execlp(python, python, "-c", python_script, cpp_to_py_decimal, py_to_cpp_decimal, int_size_decimal, NULL);
	perror("Cannot execute Python interpreter");
	exit(127);
}

static bool wait_for_python(pid_t pid) {
	siginfo_t info;
	if(waitid(P_PID, pid, &info, WEXITED)) {
		perror("wait_for_python: waitid");
		return false;
	}
	if(info.si_code == CLD_EXITED) {
		if(info.si_status == 0) {
			return true;
		} else {
			fprintf(stderr, "Python interpreter (pid %i) exited with status %i\n", (int) pid, (int) info.si_status);
			return false;
		}
	} else { // signal
		fprintf(stderr, "Python interpreter (pid %i) killed by signal %i", (int) pid, (int) info.si_status);
		return false;
	}
}

struct snaketongs_impl *snaketongs_impl_start(const char *python, int int_size) {
	struct snaketongs_impl *self = (struct snaketongs_impl *) malloc(sizeof *self);
	if(!self) {
		// avoid using stdio in case of oom
		static const char msg[] = "snaketongs_impl_start: out of memory\n";
		write(STDERR_FILENO, msg, sizeof msg - 1);
		goto error0;
	}
	int cpp_to_py[2], py_to_cpp[2];
	if(pipe(cpp_to_py)) {
		perror("snaketongs_impl_start: pipe");
		goto error1;
	}
	if(pipe(py_to_cpp)) {
		perror("snaketongs_impl_start: pipe");
		goto error2;
	}
	switch(self->pid = fork()) {
		case ForkChild:
			if(close(cpp_to_py[WriteEnd]) | close(py_to_cpp[ReadEnd]))
				perror("snaketongs_impl_start: close"), _exit(127);
			exec_python(python, cpp_to_py[ReadEnd], py_to_cpp[WriteEnd], int_size);
			// noreturn
		case ForkError:
			perror("snaketongs_impl_start: fork");
			goto error3;
	}
	// parent continues executing
	if(close(cpp_to_py[ReadEnd]) | close(py_to_cpp[WriteEnd])) {
		perror("snaketongs_impl_start: close");
		goto error4;
	}
	// check our python script started correctly
	{
		char c;
		switch(read(py_to_cpp[ReadEnd], &c, 1)) {
			case -1:
				perror("snaketongs_impl_start: read");
				goto error4;
			case 0:
				// message probably printed by child - we will make sure later
				goto error4;
			case 1:
				if(c == '+')
					break; // ok
				fputs("snaketongs_impl_start: unexpected subprocess output\n", stderr);
				goto error4;
			default:
				abort();
		}
	}
	self->cpp_to_py = fdopen(cpp_to_py[WriteEnd], "wb");
	if(!self->cpp_to_py) {
		perror("snaketongs_impl_start: fdopen wb");
		goto error4;
	}
	self->py_to_cpp = fdopen(py_to_cpp[ReadEnd], "rb");
	if(!self->py_to_cpp) {
		perror("snaketongs_impl_start: fdopen rb");
		goto error5;
	}
	self->err = false;
	return self;
error5:
	// close the parent end of each pipe
	fclose(self->cpp_to_py);
	close(py_to_cpp[ReadEnd]);
error4:
	// close the parent end of each pipe
	close(cpp_to_py[WriteEnd]);
	close(py_to_cpp[ReadEnd]);
	if(!wait_for_python(self->pid)) {
		// message already printed by wait_for_python, do nothing
	} else {
		// subprocess returned zero, so wait_for_python was quiet
		fputs("snaketongs_impl_start: subprocess terminated unexpectedly\n", stderr);
	}
	goto error1;
error3:
	// close both ends of the second pipe
	close(py_to_cpp[0]);
	close(py_to_cpp[1]);
error2:
	// close both ends of the first pipe
	close(cpp_to_py[0]);
	close(cpp_to_py[1]);
error1:
	free(self);
error0:
	return NULL;
}

bool snaketongs_impl_send(struct snaketongs_impl *self, const void *src, size_t size) {
	if(self->err)
		return false;
	if(!size)
		return true;
	errno = 0;
	switch(fwrite(src, size, 1, self->cpp_to_py)) {
		case 1:
			return true;
		case 0:
			if(errno)
				perror("snaketongs_impl_send");
			else
				fputs("snaketongs_impl_send failed\n", stderr);
			self->err = true;
			return false;
		default:
			abort();
	}
}

bool snaketongs_impl_flush(struct snaketongs_impl *self) {
	if(self->err)
		return false;
	switch(fflush(self->cpp_to_py)) {
		case 0:
			return true;
		case -1:
			perror("snaketongs_impl_flush");
			self->err = true;
			return false;
		default:
			abort();
	}
}

bool snaketongs_impl_recv(struct snaketongs_impl *self, void *dest, size_t size) {
	if(self->err)
		return false;
	if(!size)
		return true;
	errno = 0;
	switch(fread(dest, size, 1, self->py_to_cpp)) {
		case 1:
			return true;
		case 0:
			if(errno)
				perror("snaketongs_impl_recv");
			else
				fputs("snaketongs_impl_recv failed\n", stderr);
			self->err = true;
			return false;
		default:
			abort();
	}
}

bool snaketongs_impl_quit(struct snaketongs_impl *self) {
	bool ok = true;
	if(fclose(self->cpp_to_py))
		perror("snaketongs_impl_quit cpp_to_py"), ok = false;
	if(fclose(self->py_to_cpp))
		perror("snaketongs_impl_quit py_to_cpp"), ok = false;
	if(!wait_for_python(self->pid))
		ok = false;
	free(self);
	return ok;
}
