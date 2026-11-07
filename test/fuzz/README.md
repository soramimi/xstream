# Reader fuzzing

This optional target uses [LLVM libFuzzer](https://llvm.org/docs/LibFuzzer.html)
with ASan and UBSan. The library and the normal GoogleTest executable do not
depend on libFuzzer. Commands below run from the repository root and put generated
corpus entries, executables, and failure artifacts in `/tmp`.

## Build and run

Requires Clang with its libFuzzer and sanitizer runtimes:

```bash
clang++ -std=c++17 -g -O1 -Wall -Wextra -Werror \
    -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
    -fno-omit-frame-pointer -Iinclude test/fuzz/reader_fuzz.cpp \
    -o /tmp/xstream-reader-fuzz

mkdir -p /tmp/xstream-fuzz-corpus /tmp/xstream-fuzz-artifacts
cp test/fuzz/corpus/* /tmp/xstream-fuzz-corpus/
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
    /tmp/xstream-reader-fuzz /tmp/xstream-fuzz-corpus \
    -dict=test/fuzz/xml.dict -max_len=4112 -len_control=0 -runs=20000 -seed=20261107 \
    -timeout=2 -rss_limit_mb=1024 -detect_leaks=0 \
    -artifact_prefix=/tmp/xstream-fuzz-artifacts/
```

`-runs` bounds the campaign. `-timeout` detects an individual input that does not
finish, including a hang inside `advance()` that an external event counter cannot
interrupt. `-rss_limit_mb` bounds process memory. Leak detection is disabled here
because LeakSanitizer cannot run under ptrace in the development environment;
ASan's access checks and UBSan remain enabled.

To replay the checked-in seeds without generating new inputs:

```bash
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
    /tmp/xstream-reader-fuzz -detect_leaks=0 test/fuzz/corpus/*
```

On a failure, libFuzzer saves a `crash-*`, `timeout-*`, or `oom-*` input under the
artifact directory. Preserve that file, along with the compiler version and
command. Replay it by passing the file instead of a corpus directory:

```bash
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
    /tmp/xstream-reader-fuzz -detect_leaks=0 -timeout=2 \
    /tmp/xstream-fuzz-artifacts/crash-REPLACE_WITH_SAVED_NAME
```

The normal GoogleTest `Fuzzing.*` tests reuse the same checks with fixed seeds,
byte substitutions, every truncation point, long/deep inputs, and each limit
profile. They run in both C++17 and C++20, without requiring Clang or libFuzzer.

## Input format

Each input of at least 16 bytes consists of 16 control bytes followed by up to
4096 bytes of XML. Shorter inputs are treated entirely as XML with default
controls. Inputs larger than 4112 bytes are skipped; use `-max_len=4112`.
The controls are arbitrary bytes; printable ASCII `4` selects the largest limit
profile and makes seeds easy to inspect. The `corpus` files contain this header,
so they are complete reproducer inputs, rather than plain XML documents.

| Control byte | Meaning |
| --- | --- |
| 0 | Low bit enables permissive comments |
| 1–10 | Depth, name bytes, attribute count, value bytes, token bytes, per-element text, total text, fragment count, total attribute bytes, and path bytes |
| 11 | Chunk mode: entire input, one byte, or chunks up to 7/64 bytes, selected modulo 4 |
| 11–15 | Seed for the deterministic chunk generator |
| 12–13 | Document/buffer limits for the separate ingestion checks |
| 15 | Low bit enables an additional byte-at-a-time pass |

For controls 1–10 and 12–13, `(byte XOR '0') modulo 5` selects one of five finite
limits, including zero. The exact values are in `Config` in `reader_fuzz.h`.
Mutating the same input changes both XML bytes and how they are supplied.

## Checks and limits of the oracle

- Borrowed whole-document input, pushed chunks, and callback chunks must return
  the same event types, names, paths, attribute order/values, text fragments,
  accumulated text, final result, and first error code/offset/message.
- Callback runs alternate data delivery with either an explicit empty `input()`
  call or no input call. Both return `NeedInput`, preserve partial parsing state,
  and resume; the waiting flag must be clear for explicit empty input and set
  when no input was supplied.
- Every parsing step checks buffer capacity, retained text/attributes/fragments,
  path size, name/value/count/depth limits, and bounded scan/copy counters.
- Outside-root whitespace must produce no accumulated text or retained text
  counters, while each character event remains bounded by the token limit.
- EOF must terminate, end events must match earlier start events, terminal
  results must remain stable, and later I/O errors must preserve the first error.
- Ingestion checks vary document and streaming-buffer limits, verify rejection
  before copying an oversized initial chunk, and exercise small buffers through
  incremental input. These runs intentionally allow different results from a
  borrowed document: borrowed input allocates no streaming buffer, and an
  oversized borrowed document is rejected before any events are parsed.

The oracle detects inconsistencies and bound violations, rather than comparing
against a different XML implementation. A grammar mistake shared by every input
mode can remain undetected; the separate expected-result tests are still needed.
A finite successful campaign is evidence for the tested inputs, not proof that
all inputs are safe.
