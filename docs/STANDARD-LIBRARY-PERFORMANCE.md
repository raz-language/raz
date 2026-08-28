# Standard-library performance

Raz's standard library is designed around predictable ownership, contiguous storage, batching, and narrow native boundaries. Performance-sensitive policy stays in Raz; the runtime is reserved for raw memory, atomics, operating-system calls, and other permanent ABI boundaries.

## Collections

`Vector<T>` and the raw vector layer use geometric growth. For ordinary alignments, capacity changes use an in-place-capable aligned reallocation path; over-aligned values retain the allocate/copy/free fallback required to preserve alignment. `reserve_exact` and `shrink_to_fit` keep their exact-capacity semantics.

Deques use a power-of-two ring on the normal geometric-growth path, so index wrapping becomes a mask instead of integer division. Exact-capacity operations are still supported; non-power-of-two capacities use the general wrap path. When a wrapped deque must move, it is relinearized with at most two bulk copies rather than one element at a time.

Generic hash maps and sets use open addressing with one-byte control metadata. Empty and tombstone states are distinct from compact hash fingerprints, so a probe normally rejects nonmatching slots before invoking potentially expensive key equality. Control arrays are cleared in bulk. The raw integer-key hash containers normalize capacity to a power of two and use masked probing.

Use `reserve` when the approximate final size is known. It avoids repeated growth and is especially useful before bulk insertion.

## Strings and byte processing

Checked scalar byte access is implemented directly in Raz. Parsers, codecs, paths, buffers, and UTF-8 loops therefore do not cross the native ABI once per byte. Bulk equality, comparison, search, copy, move, and fill operations still use optimized host memory primitives where the operation is large enough to justify the boundary.

Owned strings use geometric growth and an in-place-capable reallocation path. Self-appends retain an offset across growth instead of allocating a temporary buffer when the source is safely inside the same logical string. Reverse substring search uses a bulk reverse-byte scan to skip impossible candidates before comparing the full needle.

String and borrowed-byte hashing use the same chunked non-cryptographic runtime hash. It consumes full machine words with a final avalanche rather than hashing one byte through an ABI call at a time. The hash is for in-process hash-table use, not persistent formats or cryptographic authentication.

Hex, Base64, binary decoding, JSON, HTTP, URL/query/form parsing, address parsing, and path manipulation keep their scalar hot loops local. IEEE CRC-32 crosses the native boundary once per complete span and uses an unrolled table-driven fold in the runtime primitive, avoiding both per-byte ABI calls and the former eight polynomial steps per byte.

## I/O

The default file buffer is 64 KiB. Small reads and writes are accumulated in Raz so system calls are amortized; transfers at least as large as the buffer bypass it directly.

A buffered writer distinguishes **draining** from **flushing**. Filling the user-space buffer drains bytes to the underlying file without forcing an OS-visible `fflush`. The public `flush` operation drains and then explicitly flushes the stream. This keeps sequential streaming throughput from paying a durability/visibility synchronization cost at every buffer rollover.

Prefer bulk `read`/`write` operations over byte-at-a-time APIs for large payloads. The byte APIs remain useful for parsers because they consume already-buffered memory without an OS call per byte.

## Networking

Socket APIs support IPv4/IPv6, DNS, TCP/UDP, vectored operations, buffering, framing, and reusable HTTP client/server flows. Vectored descriptor construction writes the caller-owned descriptor block directly in Raz, and a single send/receive operation crosses the socket boundary for the whole vector.

For readiness-driven servers, `std::net::poll_set::PollSet` stores socket records in one reusable contiguous allocation and can scale to 1,048,576 records. `wait` passes the complete set through one `poll`/`WSAPoll` call and writes readiness bits back into the same records, avoiding one blocking task or one allocation per watched socket. The runtime keeps the <=256-descriptor path entirely on the stack; larger waits use a thread-local descriptor vector that retains its high-water capacity across calls. Reuse the same `PollSet` across loop iterations and call `clear` when rebuilding a set.

The higher-level reactor remains useful when task integration is more important than direct readiness-loop control. `PollSet` is the lower-overhead building block for connection-heavy loops that already own their scheduling policy.

## Concurrency

The general bounded `std::thread::channel` is an MPMC queue. Its power-of-two ring storage uses direct memory access while the queue mutex is held; OS synchronization is used only for the synchronization itself rather than for individual payload loads/stores.

For one-producer/one-consumer pipelines, `std::thread::spsc::SpscI64` is the specialized fast path:

- no mutex or condition variable;
- acquire/release atomic head and tail publication;
- producer and consumer counters are separated by cache lines to reduce false sharing;
- power-of-two masked ring indexing; and
- `try_send_many` / `try_receive_many` synchronize once per batch and copy a wrapped batch in at most two contiguous spans.

Use SPSC when its ownership topology is true. For many-producer/many-consumer nonblocking hand-off, `std::thread::mpmc::MpmcI64` uses cache-line-separated enqueue/dequeue positions and one sequence word per slot, avoiding a global mutex while preserving bounded storage. Use the condition-variable-backed general channel when producers or consumers need blocking waits, close/cancellation semantics, or timed receive.

Ordered atomic exchange and compare/exchange operations are available in `core::atomic`, together with a target-specific `cpu_relax` hint for short spin phases. Algorithms should still prefer bounded spinning and fall back to blocking/wake primitives when waits may be long.

## Allocation and ownership

`alloc::arena::BumpArena` provides fixed-capacity monotonic allocation for request scopes, parsers, compiler passes, packet batches, and other scratch lifetimes. After construction an allocation is alignment arithmetic plus an offset update: there is no allocator call, lock, or freelist traversal. `mark`/`rewind` and `reset` release groups of allocations in O(1) while retaining the backing block. Because the arena never grows, returned pointers remain stable.

`alloc::pool::FixedPool` serves reusable fixed-size objects from an intrusive freelist. It performs one aligned backing allocation at construction; steady-state allocate/release is O(1) and does not cross the host allocator, making it suitable for packet, task, connection, AST-node, and other object-reuse hot paths.

Allocation APIs preserve requested alignment. Ordinary alignments share the host `malloc`/`realloc` path so growth can remain in place; over-aligned storage uses aligned allocation and copies only when a resize requires a new block.

Resource-owning types retain deterministic `Drop` behavior. Performance APIs do not trade away ownership correctness: buffers, poll sets, channels, files, and sockets still have explicit lifetimes and cleanup.

## Choosing the fast path

Prefer these patterns in throughput-sensitive code:

- reserve collection capacity before known bulk insertion;
- pass borrowed views rather than cloning strings or byte buffers;
- batch socket I/O with vectored operations;
- reuse `PollSet` rather than rebuilding readiness state around individual waits;
- use SPSC batch operations for one-way pipeline stages and lock-free MPMC for shared nonblocking worker queues;
- use bump arenas for scoped scratch memory and fixed pools for high-reuse same-sized objects;
- use buffered file I/O for streams and explicitly flush only at visibility/durability boundaries; and
- parse directly from borrowed/buffered memory before allocating owned results.

## Measurement

Performance claims should be measured on representative release builds for the target platform. Record throughput and latency together with optimization level, target CPU/OS/toolchain, allocation behavior where relevant, and correctness results from the same build.

The repository's `raz-stdlib-performance-audit` protects structural hot-path guarantees, while runtime fixtures exercise the optimized collection, codec, networking, polling, and channel behavior. Those tests prevent architectural regressions; application benchmarks remain the source of workload-specific performance numbers.


## Filesystem, processes, CLI, and random workloads

Directory iteration reuses caller-owned filename storage. Once the buffer has reached the longest filename seen so far, `ReadDir::next` fills it directly and consumes the directory entry in one native transition. An oversized entry returns the required length without consuming it; Raz grows the buffer geometrically and retries only for that new high-water mark. Reuse one `String` while walking large trees.

`std::process::command::Command` stores its argument blob and optional working directory for reuse across invocations. Working-directory execution uses the direct process ABI rather than a shell, preserving argument boundaries and avoiding command-line reparsing. `std::process::args::next_into` similarly lets long-running CLI tools scan argv through one retained `String` allocation.

`std::cli::parser` operates on borrowed argument views and performs no allocation. Long-option splitting uses the bulk byte scanner to locate `=` and returns borrowed name/value slices. It is intended as the low-level parser used underneath higher-level application CLIs.

The deterministic `std::random::Rng` is for simulations, schedulers, randomized algorithms, tests, and benchmarks; operating-system entropy remains the source for security-sensitive randomness. `rng_fill_u64` emits directly into contiguous caller storage, and `rng_shuffle_raw` performs Fisher-Yates swaps with one caller-owned scratch slot instead of allocating during the shuffle.

## DNS and HTTP connection reuse

`std::net::resolve::DnsCache` is a bounded four-host hot cache intended for RPC, HTTP, and service clients whose active origin set is small. Cache hits compare retained host bytes, check monotonic TTL expiry, and return the retained first address without allocating or entering the OS resolver. Miss replacement is round-robin to keep the steady-state path branch-light and allocation-bounded.

`std::net::http::client::HttpClientPool` retains four complete HTTP clients rather than one mutable origin. Each slot owns its TCP/TLS stream and reusable request/receive buffers, so alternating between a few services does not force repeated connection teardown and handshakes. Origin selection is a fixed direct scan and miss replacement is round-robin; the pool has no hash-table metadata or per-request pool allocation.

HTTP server workers should retain their `ServerConnection`. Accept clears and reuses the existing receive allocation, growing only when the configured server buffer exceeds the connection's high-water capacity. The normal accept/process/close/accept cycle therefore avoids repeated receive-buffer allocation.

For request-heavy servers, `read_request_view` exposes method, target, header, and fixed-length body slices directly from the retained connection input buffer. The worker processes those borrowed views and then calls `release_request`, which advances the buffer in O(1) without copying request fields. `buffered_request_ready` performs a socket-free completeness check so a readiness loop can drain HTTP pipelining already resident in memory before returning to `PollSet`. Chunked bodies retain the same API but point at the connection-owned reusable decode scratch after framing removal. The owning `read_request` convenience API is implemented on top of this borrowed path for callers that prefer owned request data.

## Structured logging

`std::log::LogBuffer` builds compact structured lines in one retained `String`. Message bytes and key/value fields append directly into that storage; integer formatting writes into the same buffer. Reuse one logger per worker/thread and send its borrowed final byte view to the selected output sink in one write.

## TLS session resumption and compression

TLS clients share the process-wide OpenSSL client context and retain a bounded eight-host cache of opaque `SSL_SESSION` objects. A reconnect to a recently used host offers the cached session before the handshake; completed sessions are refreshed both when the handshake finishes and again at stream teardown so TLS 1.3 tickets received after the initial handshake are retained. The cache is synchronized only around cache lookup/update and does not add a new Raz/native ABI call.

`std::compress::lz4` is an allocation-reusing LZ4 block codec implemented in Raz. `Encoder` owns one 4,096-slot hash table, clears it in one bulk fill between blocks, and writes compressed bytes directly to caller-owned output memory. Decompression also writes directly to caller-owned memory and supports overlapping LZ4 match copies without temporary buffers. This keeps compression state worker-local and makes repeated message/block compression allocator-free after encoder construction.

HTTP `ServerConnection` retains chunk-decoding scratch alongside its receive and response buffers. Keep-alive workers therefore avoid a per-request 4 KiB scratch allocation, including for chunked requests. Each connection also retains a two-slice `IoVector`; normal responses send the completed header block and caller-owned body with scatter/gather I/O, avoiding an O(body-size) copy into the response `String` and its associated capacity growth. Partial vectored sends fall back to exact `send_all` completion.


## Filesystem tree operations

Recursive tree removal retains one mutable full-path buffer for the complete traversal. Each directory level records the current path length, appends the child component, descends, and truncates back to the saved length. The traversal therefore avoids per-entry joined-path allocation while preserving symlink safety and deterministic handle cleanup.

## Batched readiness reactor

`std::net::reactor::BatchReactor` is the preferred event-loop primitive for many nonblocking sockets. Watches are stored in one reusable `PollSet`, which grows geometrically when required, and one `batch_wait` crosses the operating-system boundary for the entire set.

The reactor also owns a retained min-heap of monotonic timers. Scheduling and expiry are O(log n), the timer allocation grows geometrically, and `batch_wait` automatically clamps its OS poll timeout to the nearest deadline. Keep-alive expiry, retries, idle connection timeouts, and similar timers can therefore share the socket event loop rather than occupying sleeping worker tasks. Expired timer tokens are drained with `batch_pop_expired`.

Readiness interests are mutable in place with `batch_set_interests`, which lets a connection switch between readable and writable states when applying backpressure without rebuilding the watch set. `batch_remove_swap` removes a closed connection in O(1) by moving the final poll record into the vacated slot. The executor-backed one-watch API remains useful when a blocking watch must be adapted into a task; high-throughput servers should prefer the batched reactor.
