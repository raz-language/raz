# Standard-library performance

Raz's standard library is designed around predictable ownership and low overhead rather than hidden global policy.

## Collections

Vectors and deques use concrete generic layout information, explicit reserve/growth policy, and ownership-aware transfer. Hash tables use power-of-two capacities and open addressing, with tombstone reclamation and primitive/string key support.

Use borrowed slices and iterators when ownership transfer is unnecessary. Bulk operations are preferred over per-element native boundary calls.

## Strings and byte processing

String and byte APIs provide borrowed views, UTF-8 validation, formatting, search, and codec helpers. Hot byte-range operations use bounded bulk runtime primitives where that removes repeated checked arena crossings without changing library policy.

## I/O

File, path, process, environment, time, and random APIs keep resource ownership in Raz. Buffered I/O amortizes system calls, and owning handles use deterministic cleanup.

## Networking

Socket APIs support IPv4/IPv6, DNS, TCP/UDP, vectored operations, buffering, framing, and reusable HTTP client/server flows. Timeout calculations use monotonic deadlines so retries or spurious wakeups do not silently extend caller limits.

## Concurrency

Channels, futures, worker scheduling, task scopes, cancellation, and timer policy are implemented in Raz over OS synchronization/thread/time boundaries. Reusable workers are preferred over thread-per-task execution.

## Application protocols

JSON and HTTP parsing emphasize bounded memory, borrowed input, validation before ownership, and streaming paths for large bodies. Encoding utilities avoid unnecessary allocation where the output size can be bounded in advance.

## Measurement

Performance claims should be measured on representative release builds for the target platform. Compiler and library regression tests are intended to detect meaningful changes, not to substitute for workload-specific benchmarking.
