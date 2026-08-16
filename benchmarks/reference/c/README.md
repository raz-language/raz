# C baseline benchmarks

These fixtures provide deliberately small native-C/C++ baselines for hot paths that also exist in the Raz standard library. They are not framework comparisons. Keep the input, validation work, iteration count, optimization level, and target architecture matched when comparing them with Raz.

## HTTP request parser

`http_parse/baseline.c` parses a 142-byte HTTP/1.1 request into borrowed slices, validates the request line, scans and validates headers, interprets `Content-Length`, `Connection`, and `Transfer-Encoding`, and exposes the body without allocation.

Compile on Linux with:

```sh
clang -O3 -DNDEBUG benchmarks/reference/c/http_parse/baseline.c -o http-parse-c
```

The matching Raz workload should call `std::net::http::parse_request` on the same 142-byte request and consume the returned method, target, body, and content length so the optimizer cannot discard the parse.

## CRC-32

`crc32/baseline.cpp` uses the same IEEE table recurrence and eight-byte unroll as Raz's bulk CRC runtime primitive. Compare one 64 MiB pass at a time; do not repeat an identical pure call in a C loop because an optimizing compiler may common the calls and invalidate the timing.

Compile on Linux with:

```sh
clang++ -O3 -DNDEBUG benchmarks/reference/c/crc32/baseline.cpp -o crc32-c
```

Benchmark results are machine- and compiler-specific and should be reported with CPU, OS, compiler, optimization level, workload size, and correctness checksum. Do not treat a result from one machine as a language-wide constant.
