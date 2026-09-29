#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
            return 1;
        }
        return 0;
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

/* Entry point for the homework programs. */
int main(void) {
    if (question1() != 0) {
        return 1;
    }
    return question2();
}