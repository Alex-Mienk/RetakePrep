## Typical Shape

1. A server receives binary messages from UDP or TCP.
2. The message has fixed-size fields and optional binary parameters.
3. The server validates length, command, user, and parameter layout.
4. The server dispatches work to children or threads.
5. Results are gathered later with a separate command.
6. `EXIT` must clean up descriptors, FIFOs, threads, children, semaphores, and mutexes.

## Parsing Checklist

- Never assume network data is null-terminated.
- Copy fixed-size text fields into `char field[N + 1]`, then add `'\0'`.
- Convert network integers with `ntohl`.
- Validate the exact length before reading optional parameters.
- For UDP, one `recvfrom` equals one datagram.

## Concurrency Checklist

- Threads: use mutex + semaphore or mutex + condition variable.
- Processes: use pipes, FIFOs, or shared memory.
- Always close unused pipe ends after `fork`.
- Always send one termination signal/job per worker.
- Always `pthread_join` threads or `waitpid` child processes.

## Useful Commands

Build with strict flags:

```sh
cc -std=c17 -Wall -Wextra -Wshadow -Wvla -Werror file.c -o file -pthread
```

Check file descriptors:

```sh
lsof -p <pid>
```

Send UDP data:

```sh
printf 'hello' | nc -u 127.0.0.1 9000
```

## Common Lab Mistakes

- Passing `&addr.sin_addr` to `recvfrom` instead of `(struct sockaddr*)&addr`.
- Forgetting `htons` for ports and `ntohs` when printing ports.
- Reading only once from a pipe and assuming all data arrived.
- Forgetting that `read`/`write` can return fewer bytes than requested.
- Forgetting to initialize semaphores before creating worker threads.
- Destroying attributes before using them is wrong; destroying attributes after object creation is fine.
