#if defined(__linux__)
#define _GNU_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sched.h>
#endif

#define DEFAULT_ITERATIONS 100000
#define TIMER_SAMPLES 10000
#define CONTEXT_WARMUP 1000
/*THIS IS THE CODE FOR CHAPTER 6 MEASUREMENT QUESTIONS*/
static int monotonic_time_ns(uint64_t *time_ns) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) < 0) {
        return -1;
    }
    *time_ns = (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
    return 0;
}

static int report_timer_resolution(void) {
    struct timespec resolution;
    if (clock_getres(CLOCK_MONOTONIC, &resolution) < 0) {
        perror("clock_getres");
        return -1;
    }

    uint64_t minimum_delta = UINT64_MAX;
    uint64_t total_delta = 0;
    uint64_t nonzero_samples = 0;
    for (unsigned int sample = 0; sample < TIMER_SAMPLES; sample++) {
        uint64_t start;
        uint64_t end;
        if (monotonic_time_ns(&start) < 0 || monotonic_time_ns(&end) < 0) {
            perror("clock_gettime");
            return -1;
        }
        uint64_t delta = end - start;
        if (delta > 0) {
            if (delta < minimum_delta) {
                minimum_delta = delta;
            }
            total_delta += delta;
            nonzero_samples++;
        }
    }

    uint64_t reported_resolution_ns = (uint64_t)resolution.tv_sec * UINT64_C(1000000000) +
                                      (uint64_t)resolution.tv_nsec;
    printf("CLOCK_MONOTONIC reported resolution: %" PRIu64 " ns\n", reported_resolution_ns);
    if (nonzero_samples > 0) {
        printf("Smallest observed back-to-back timer delta: %" PRIu64 " ns\n", minimum_delta);
        printf("Average back-to-back timer delta: %.2f ns\n",
               (double)total_delta / (double)nonzero_samples);
    } else {
        printf("No nonzero timer delta observed across %d samples.\n", TIMER_SAMPLES);
    }
    return 0;
}

static int benchmark_zero_byte_read(uint64_t iterations, uint64_t *average_ns) {
    int read_pipe[2];
    if (pipe(read_pipe) < 0) {
        perror("pipe for read benchmark");
        return -1;
    }

    char byte;
    for (uint64_t index = 0; index < iterations; index++) {
        if (read(read_pipe[0], &byte, 0) < 0) {
            perror("zero-byte read");
            close(read_pipe[0]);
            close(read_pipe[1]);
            return -1;
        }
    }

    uint64_t start;
    uint64_t end;
    if (monotonic_time_ns(&start) < 0) {
        perror("clock_gettime");
        close(read_pipe[0]);
        close(read_pipe[1]);
        return -1;
    }
    for (uint64_t index = 0; index < iterations; index++) {
        if (read(read_pipe[0], &byte, 0) < 0) {
            perror("zero-byte read");
            close(read_pipe[0]);
            close(read_pipe[1]);
            return -1;
        }
    }
    if (monotonic_time_ns(&end) < 0) {
        perror("clock_gettime");
        close(read_pipe[0]);
        close(read_pipe[1]);
        return -1;
    }

    close(read_pipe[0]);
    close(read_pipe[1]);
    *average_ns = (end - start) / iterations;
    return 0;
}

static int write_byte(int fd, char byte) {
    ssize_t result;
    do {
        result = write(fd, &byte, sizeof(byte));
    } while (result < 0 && errno == EINTR);
    if (result == 1) {
        return 0;
    }
    if (result == 0) {
        errno = EIO;
    }
    return -1;
}

static int read_byte(int fd, char *byte) {
    ssize_t result;
    do {
        result = read(fd, byte, sizeof(*byte));
    } while (result < 0 && errno == EINTR);
    if (result == 1) {
        return 0;
    }
    if (result == 0) {
        errno = EPIPE;
    }
    return -1;
}

static int exchange_byte(int write_fd, int read_fd) {
    const char byte = 'x';
    char received;
    if (write_byte(write_fd, byte) < 0 || read_byte(read_fd, &received) < 0) {
        return -1;
    }
    return received == byte ? 0 : -1;
}

static void try_pin_to_one_cpu(void) {
#ifdef __linux__
    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof(allowed), &allowed) < 0) {
        perror("sched_getaffinity");
        return;
    }

    int selected_cpu = -1;
    for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
        if (CPU_ISSET(cpu, &allowed)) {
            selected_cpu = cpu;
            break;
        }
    }
    if (selected_cpu < 0) {
        fprintf(stderr, "No available CPU found for affinity.\n");
        return;
    }

    cpu_set_t selected;
    CPU_ZERO(&selected);
    CPU_SET(selected_cpu, &selected);
    if (sched_setaffinity(0, sizeof(selected), &selected) < 0) {
        perror("sched_setaffinity");
        return;
    }
    printf("Processes will use CPU %d for the context-switch test.\n", selected_cpu);
#else
    printf("CPU affinity is not set on this platform; context-switch results may vary.\n");
#endif
}

static int benchmark_pipe_ping_pong(uint64_t iterations, uint64_t *round_trip_ns) {
    int parent_to_child[2];
    int child_to_parent[2];
    if (pipe(parent_to_child) < 0) {
        perror("parent-to-child pipe");
        return -1;
    }
    if (pipe(child_to_parent) < 0) {
        perror("child-to-parent pipe");
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        return -1;
    }

    try_pin_to_one_cpu();
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);
        return -1;
    }

    uint64_t total_exchanges = iterations + CONTEXT_WARMUP;
    if (pid == 0) {
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        for (uint64_t index = 0; index < total_exchanges; index++) {
            char byte;
            if (read_byte(parent_to_child[0], &byte) < 0 ||
                write_byte(child_to_parent[1], byte) < 0) {
                perror("child pipe exchange");
                _exit(EXIT_FAILURE);
            }
        }
        close(parent_to_child[0]);
        close(child_to_parent[1]);
        _exit(EXIT_SUCCESS);
    }

    close(parent_to_child[0]);
    close(child_to_parent[1]);
    for (uint64_t index = 0; index < CONTEXT_WARMUP; index++) {
        if (exchange_byte(parent_to_child[1], child_to_parent[0]) < 0) {
            perror("warm-up pipe exchange");
            close(parent_to_child[1]);
            close(child_to_parent[0]);
            waitpid(pid, NULL, 0);
            return -1;
        }
    }

    uint64_t start;
    uint64_t end;
    if (monotonic_time_ns(&start) < 0) {
        perror("clock_gettime");
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }
    for (uint64_t index = 0; index < iterations; index++) {
        if (exchange_byte(parent_to_child[1], child_to_parent[0]) < 0) {
            perror("timed pipe exchange");
            close(parent_to_child[1]);
            close(child_to_parent[0]);
            waitpid(pid, NULL, 0);
            return -1;
        }
    }
    if (monotonic_time_ns(&end) < 0) {
        perror("clock_gettime");
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }

    close(parent_to_child[1]);
    close(child_to_parent[0]);
    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        perror("waitpid");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "pipe ping-pong child failed.\n");
        return -1;
    }

    *round_trip_ns = (end - start) / iterations;
    return 0;
}

static int parse_iterations(int argc, char **argv, uint64_t *iterations) {
    if (argc == 1) {
        *iterations = DEFAULT_ITERATIONS;
        return 0;
    }
    if (argc != 2 || argv[1][0] == '-') {
        return -1;
    }

    errno = 0;
    char *end;
    unsigned long long parsed = strtoull(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' || parsed == 0 ||
        parsed > UINT64_MAX - CONTEXT_WARMUP) {
        return -1;
    }
    *iterations = (uint64_t)parsed;
    return 0;
}

int main(int argc, char **argv) {
    uint64_t iterations;
    if (parse_iterations(argc, argv, &iterations) < 0) {
        fprintf(stderr, "Usage: %s [positive-iteration-count]\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("Iterations per benchmark: %" PRIu64 "\n", iterations);
    if (report_timer_resolution() < 0) {
        return EXIT_FAILURE;
    }

    uint64_t read_average_ns;
    if (benchmark_zero_byte_read(iterations, &read_average_ns) < 0) {
        return EXIT_FAILURE;
    }
    printf("Average zero-byte read() time: %" PRIu64 " ns per call\n", read_average_ns);

    uint64_t round_trip_ns;
    if (benchmark_pipe_ping_pong(iterations, &round_trip_ns) < 0) {
        return EXIT_FAILURE;
    }
    printf("Average pipe ping-pong round trip: %" PRIu64 " ns\n", round_trip_ns);
    printf("Approximate context-switch component: %.2f ns per switch\n",
           (double)round_trip_ns / 2.0);
    printf("The context-switch estimate includes pipe and scheduling overhead.\n");
    return EXIT_SUCCESS;
}
