# Roadmap

## PE/COFF completeness

Completed baseline:

- COFF archive (`.lib`) linker-index-driven lazy member selection and parsing
- Microsoft short import-object decoding
- `/DEFAULTLIB:` / `/LIBPATH:` library discovery
- PE import descriptors, ILT/IAT and AMD64 import thunks
- leader-based COMDAT selection plus associative COMDAT lifetime
- weak externals, `/ALTERNATENAME:` aliases, and common symbols
- automatic MSVC/Windows SDK library discovery
- CRT/startup-root selection with freestanding fallback
- linker-synthesized `__ImageBase` and absolute-symbol relocation support
- extended relocation-count decoding for large generated COFF sections
- sorted AMD64 exception (`.pdata`) output and configurable PE stack/heap reserves

- uninitialized-section placement by extent, so `.bss` globals start at zero
- debug-section discard when no PDB is requested
- static-keyed COMDATs kept per object; references to discarded COMDATs
  redirected to the prevailing definition
- `--map` link maps and `--verbose` archive-selection tracing
- GNU/MinGW import libraries via per-symbol stub recovery
- reachability-based dead-section/COMDAT elimination (`/OPT:REF`-style)

Next:

- output-section merging (`.xdata`/`.CRT`/`.rtc`/`.gfids` into `.rdata`)
- remaining uncommon auxiliary-symbol variants
- full force-include/directive edge-case semantics
- load-config / GuardCF metadata
- full TLS callback/startup semantics beyond directory publication
- DLL/export generation

## Linker architecture

- identical code folding
- deterministic parallel object parsing
- incremental symbol/section graph cache
- structured diagnostics
- PDB/debug-directory integration

## Platform growth

Windows AMD64 is first. Other object/executable formats should use the same symbol graph and placement engine rather than duplicating the linker core.
