# CS5600 Homework 2: Processes and Scheduling

This repository contains C exercises on process management and operating-system performance measurement.

Here's the pdf to the simulation question answers:  https://www.overleaf.com/read/nfqvjjywgcwd#4d62f5

## Process Exercises

`homework2.c` demonstrates:

- How memory changes are isolated between a parent and child after `fork()`.
- How a file descriptor opened before `fork()` is inherited and shared.
- How a pipe can synchronize parent and child output.
- How `exec()` variants launch `/bin/ls`.
- How `wait()` and `waitpid()` collect child-process status.
- What happens when a child closes standard output.
- How two children can communicate through a pipe.

Build and run:

```sh
cc -Wall -Wextra -std=c11 homework2.c -o homework2
./homework2
```

The file exercise creates or truncates `fork_output.txt` in the current
directory. The `exec()` exercise lists that directory. `execvpe()` is included
on Linux; macOS runs the other listed variants.

## Measurement Exercise

`measurement.c` measures the average time for a zero-byte `read()` and a
two-process pipe ping-pong. It also reports the clock's stated resolution and
observed back-to-back timer deltas. The ping-pong result is an estimate: it
includes pipe and scheduling overhead, not just context-switch cost.

Build and run with the default 100,000 iterations:

```sh
cc -Wall -Wextra -std=c11 measurement.c -o measurement
./measurement
```

An optional positive argument changes the iteration count:

```sh
./measurement 10000
```

On Linux, the measurement program attempts to pin both processes to one CPU for the context-switch test. On macOS, CPU affinity is not set. Measurements vary with the machine, operating system, virtual-machine configuration, and system load; repeat runs and record the environment when comparing results.
