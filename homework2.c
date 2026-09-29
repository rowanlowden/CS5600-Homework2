#ifdef __linux__
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
/*This is the answers to Questions for chapter 5 Code questions in OSTEP*/
/* Question 1: Show that fork gives the child an independent copy of x. */
static int question1(void) {
    int x = 100;
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        printf("child before change: x = %d\n", x);
        x = 200;
        printf("child after change:  x = %d\n", x);
        exit(EXIT_SUCCESS);
    }

    if (waitpid(pid, NULL, 0) < 0) {
        perror("waitpid");
        return 1;
    }

    printf("parent before change: x = %d\n", x);
    x = 300;
    printf("parent after change:  x = %d\n", x);
    return 0;
}

static int write_all(int fd, const char *buffer, size_t length) {
    size_t written_total = 0;

    while (written_total < length) {
        ssize_t written = write(fd, buffer + written_total, length - written_total);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        written_total += (size_t)written;
    }

    return 0;
}

/* Question 2: The inherited descriptors share an open-file description and offset. */
static int question2(void) {
    const char *path = "fork_output.txt";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(fd);
        return 1;
    }

    if (pid == 0) {
        const char message[] = "child writes to the file\n";
        if (write_all(fd, message, sizeof(message) - 1) < 0) {
            perror("child write");
            close(fd);
            return 1;
        }
        if (close(fd) < 0) {
            perror("child close");
            _exit(EXIT_FAILURE);
        }
        _exit(EXIT_SUCCESS);
    }

    const char message[] = "parent writes to the file\n";
    int result = 0;
    if (write_all(fd, message, sizeof(message) - 1) < 0) {
        perror("parent write");
        result = 1;
    }

    int child_status;
    if (waitpid(pid, &child_status, 0) < 0) {
        perror("waitpid");
        result = 1;
    } else if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) {
        result = 1;
    }

    if (close(fd) < 0) {
        perror("parent close");
        result = 1;
    }
    printf("Both processes wrote to %s; the line order depends on scheduling.\n", path);
    return result;
}

/* Question 3: The pipe makes the parent wait for the child's hello without wait(). */
static int question3(void) {
    int order_pipe[2];
    if (pipe(order_pipe) < 0) {
        perror("pipe");
        return 1;
    }

    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(order_pipe[0]);
        close(order_pipe[1]);
        return 1;
    }

    if (pid == 0) {
        const char ready = 'x';
        close(order_pipe[0]);
        printf("hello\n");
        if (fflush(stdout) == EOF) {
            perror("fflush");
            close(order_pipe[1]);
            _exit(EXIT_FAILURE);
        }
        if (write_all(order_pipe[1], &ready, sizeof(ready)) < 0) {
            perror("child pipe write");
            close(order_pipe[1]);
            _exit(EXIT_FAILURE);
        }
        close(order_pipe[1]);
        _exit(EXIT_SUCCESS);
    }

    close(order_pipe[1]);
    char ready;
    ssize_t bytes_read;
    do {
        bytes_read = read(order_pipe[0], &ready, sizeof(ready));
    } while (bytes_read < 0 && errno == EINTR);
    close(order_pipe[0]);

    if (bytes_read < 0) {
        perror("parent pipe read");
        return 1;
    }
    if (bytes_read != sizeof(ready)) {
        fprintf(stderr, "child exited without signaling the parent\n");
        return 1;
    }

    printf("goodbye\n");
    return 0;
}

enum exec_variant {
    EXEC_EXECL,
    EXEC_EXECLE,
    EXEC_EXECLP,
    EXEC_EXECV,
    EXEC_EXECVP,
#ifdef __linux__
    EXEC_EXECVPE,
#endif
};

/* Question 4: Each successful exec replaces its child, so use a fresh child per variant. */
static int run_exec_variant(const char *name, enum exec_variant variant) {
    char *const arguments[] = {"ls", "-1", NULL};
    printf("\nQuestion 4: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        switch (variant) {
        case EXEC_EXECL:
            execl("/bin/ls", "ls", "-1", (char *)NULL);
            break;
        case EXEC_EXECLE:
            execle("/bin/ls", "ls", "-1", (char *)NULL, environ);
            break;
        case EXEC_EXECLP:
            execlp("ls", "ls", "-1", (char *)NULL);
            break;
        case EXEC_EXECV:
            execv("/bin/ls", arguments);
            break;
        case EXEC_EXECVP:
            execvp("ls", arguments);
            break;
#ifdef __linux__
        case EXEC_EXECVPE:
            execvpe("ls", arguments, environ);
            break;
#endif
        }
        perror(name);
        _exit(127);
    }

    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0) {
        perror("waitpid");
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "%s child did not exit successfully\n", name);
        return 1;
    }
    return 0;
}

/* l takes a list, v takes argv, p searches PATH, and e supplies an environment. */
static int question4(void) {
    static const struct {
        const char *name;
        enum exec_variant variant;
    } variants[] = {
        {"execl", EXEC_EXECL},
        {"execle", EXEC_EXECLE},
        {"execlp", EXEC_EXECLP},
        {"execv", EXEC_EXECV},
        {"execvp", EXEC_EXECVP},
#ifdef __linux__
        {"execvpe", EXEC_EXECVPE},
#endif
    };

    for (size_t index = 0; index < sizeof(variants) / sizeof(variants[0]); index++) {
        if (run_exec_variant(variants[index].name, variants[index].variant) != 0) {
            return 1;
        }
    }
    printf("exec variants differ in argument format, PATH lookup, and environment handling.\n");
    return 0;
}

/* Question 5: wait() returns any reaped child's PID; a child with no children gets ECHILD. */
static int question5(void) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        errno = 0;
        pid_t waited = wait(NULL);
        if (waited == -1 && errno == ECHILD) {
            printf("child: wait() returned -1 with ECHILD (no child processes)\n");
            fflush(stdout);
            _exit(0);
        }
        fprintf(stderr, "child: unexpected result from wait()\n");
        _exit(1);
    }

    int status;
    pid_t waited;
    do {
        waited = wait(&status);
    } while ((waited < 0 && errno == EINTR) || (waited > 0 && waited != pid));

    if (waited < 0) {
        perror("parent wait");
        return 1;
    }

    printf("parent: wait() returned child PID %ld\n", (long)waited);
    if (WIFEXITED(status)) {
        printf("parent: child exited with status %d\n", WEXITSTATUS(status));
        return WEXITSTATUS(status) == 0 ? 0 : 1;
    }
    printf("parent: child did not exit normally\n");
    return 1;
}

/* Question 6: waitpid() selects a specific child and can also be used with options such as WNOHANG. */
static int question6(void) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        printf("question 6 child: exiting with status 7\n");
        fflush(stdout);
        _exit(7);
    }

    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0) {
        perror("waitpid");
        return 1;
    }

    printf("question 6 parent: waitpid() returned PID %ld\n", (long)waited);
    if (WIFEXITED(status)) {
        printf("question 6 parent: child exit status is %d\n", WEXITSTATUS(status));
        return 0;
    }
    fprintf(stderr, "question 6 child did not exit normally\n");
    return 1;
}

/* Question 7: printf() uses stdout's FILE stream, but its underlying file descriptor is closed. */
static int question7(void) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        if (close(STDOUT_FILENO) < 0) {
            perror("child close stdout");
            _exit(EXIT_FAILURE);
        }

        int print_result = printf("this text cannot reach standard output\n");
        int flush_result = fflush(stdout);
        if (print_result < 0 || flush_result == EOF) {
            perror("printf after closing stdout");
            _exit(EXIT_SUCCESS);
        }

        fprintf(stderr, "printf unexpectedly succeeded after stdout was closed\n");
        _exit(EXIT_FAILURE);
    }

    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        perror("waitpid");
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "question 7 child failed\n");
        return 1;
    }

    printf("question 7 parent: child's stdout write failed as expected; see stderr\n");
    return 0;
}

/* Question 8: The first child's stdout feeds the second child's stdin through a pipe. */
static int question8(void) {
    int data_pipe[2];
    if (pipe(data_pipe) < 0) {
        perror("pipe");
        return 1;
    }

    fflush(stdout);
    pid_t writer_pid = fork();
    if (writer_pid < 0) {
        perror("first fork");
        close(data_pipe[0]);
        close(data_pipe[1]);
        return 1;
    }

    if (writer_pid == 0) {
        if (dup2(data_pipe[1], STDOUT_FILENO) < 0) {
            perror("writer dup2");
            _exit(EXIT_FAILURE);
        }
        close(data_pipe[0]);
        close(data_pipe[1]);

        const char lines[] = "first child: line one\nfirst child: line two\n";
        if (write_all(STDOUT_FILENO, lines, sizeof(lines) - 1) < 0) {
            perror("writer write");
            _exit(EXIT_FAILURE);
        }
        _exit(EXIT_SUCCESS);
    }

    pid_t reader_pid = fork();
    if (reader_pid < 0) {
        perror("second fork");
        close(data_pipe[0]);
        close(data_pipe[1]);
        while (waitpid(writer_pid, NULL, 0) < 0 && errno == EINTR) {
        }
        return 1;
    }

    if (reader_pid == 0) {
        if (dup2(data_pipe[0], STDIN_FILENO) < 0) {
            perror("reader dup2");
            _exit(EXIT_FAILURE);
        }
        close(data_pipe[0]);
        close(data_pipe[1]);
        execlp("wc", "wc", "-l", (char *)NULL);
        perror("exec wc");
        _exit(127);
    }

    close(data_pipe[0]);
    close(data_pipe[1]);

    int writer_status;
    pid_t waited;
    do {
        waited = waitpid(writer_pid, &writer_status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        perror("waitpid writer");
        return 1;
    }

    int reader_status;
    do {
        waited = waitpid(reader_pid, &reader_status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        perror("waitpid reader");
        return 1;
    }

    if (!WIFEXITED(writer_status) || WEXITSTATUS(writer_status) != 0 ||
        !WIFEXITED(reader_status) || WEXITSTATUS(reader_status) != 0) {
        fprintf(stderr, "question 8 pipeline child failed\n");
        return 1;
    }
    printf("question 8 parent: wc counted the lines sent through the pipe\n");
    return 0;
}

/* Entry point for the homework programs. */
int main(void) {
    if (question1() != 0) {
        return 1;
    }
    if (question2() != 0) {
        return 1;
    }
    if (question3() != 0) {
        return 1;
    }
    if (question4() != 0) {
        return 1;
    }
    if (question5() != 0) {
        return 1;
    }
    if (question6() != 0) {
        return 1;
    }
    if (question7() != 0) {
        return 1;
    }
    return question8();
}