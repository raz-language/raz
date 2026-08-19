# ObLink

ObLink is a standalone native linker designed to consume object files emitted by Forge. The first production target is Windows x86-64: Microsoft COFF relocatable objects and libraries in, deterministic PE32+ executables out.

ObLink is intentionally a separate repository. Forge owns code generation and object emission; ObLink owns symbol resolution, relocation, executable image construction, libraries, imports, and future incremental linking.

## Current baseline

- AMD64 Microsoft COFF + BigObj parser
- archive-index-driven lazy COFF archive (`.lib`) extraction; unused members remain unparsed
- Microsoft short-import object decoding
- GNU/MinGW import libraries, whose per-symbol stub members are recovered into
  the same import records and whose descriptor head/tail members are dropped
- `/DEFAULTLIB:`, `/LIBPATH:`, `/NODEFAULTLIB`, `/FAILIFMISMATCH`, `/INCLUDE:` and `/ALTERNATENAME:` handling from `.drectve` sections
- explicit `-L` / `-l` library search
- deterministic section coalescing/layout with MSVC `$` subsection ordering (`.CRT$*`, `.tls$*`, etc.)
- external/global symbol resolution across multiple `.obj` and `.lib` inputs
- leader-based MSVC COMDAT section-definition selection (`ANY`, `SAME_SIZE`, `EXACT_MATCH`, `ASSOCIATIVE`, `LARGEST`, `NEWEST`) for code and data
- weak-external fallback resolution, `/ALTERNATENAME:` aliases, and common-symbol `.bss` allocation
- automatic Windows MSVC/Windows Kits x64 library discovery with MinGW/Strawberry filtering
- AMD64 `ADDR64`, `ADDR32`, `ADDR32NB`, `REL32..REL32_5`, `SECTION`, and `SECREL` relocations
- PE32+ executable headers and section table
- AMD64 exception-directory publication from `.pdata` / `.xdata`
- PE base-relocation (`.reloc`) blocks for absolute AMD64 addresses
- sorted AMD64 exception tables and CRT startup-root inference
- linker-synthesized `__ImageBase` and extended relocation-count decoding
- configurable PE stack/heap reserves
- TLS-directory publication from `_tls_used`
- PE import descriptors, ILT/IAT, hint/name records, and x64 import thunks
- console/windows subsystem selection
- uninitialized (`.bss`) sections placed by extent rather than by file bytes, so
  zero-initialized globals reach the image as zeros
- `.debug$S` / `.debug$T` discarded when no debug output is requested
- COMDAT sections keyed only by static symbols kept per object instead of
  treated as cross-object duplicates
- references to discarded COMDAT definitions redirected to the prevailing copy
- empty output sections dropped, and ASLR flags stated consistently with the
  presence of a base relocation table
- `--map` link maps and `--verbose` archive-selection tracing
- direct Forge-compatible CLI shape: `oblink file.obj runtime.lib -o app.exe`
- no third-party dependencies

ObLink links and runs the Raz test-example corpus and the 6 MB self-hosted
`raz-compiler.exe` against the MSVC CRT with no external linker involved.

The remaining Windows compatibility work is primarily complete CRT/load-config/CFG semantics, DLL/export generation, debug/PDB output, and less-common COFF directive/metadata cases. Output sections are not yet merged the way link.exe folds `.xdata`/`.CRT`/`.rtc` into `.rdata`, so images reserve more address space than they need.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

## Forge integration

Forge already emits AMD64 COFF objects. A direct flow is:

```text
Forge IR -> Forge COFF .obj -> ObLink -> PE32+ .exe
```

```sh
forge compile program.fir --format=coff -o program.obj
oblink program.obj -L path/to/libs -l ws2_32 -o program.exe
```

For MSVC/clang-cl generated library members, ObLink also consumes `/DEFAULTLIB:` directives and Microsoft short-import objects so `__imp_*` references become real PE IAT entries instead of unresolved symbols.
