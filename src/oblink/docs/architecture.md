# Architecture

ObLink keeps the linker boundary deliberately narrow.

1. `coff` validates and decodes relocatable object files without depending on Forge internals.
2. `link` uses COFF archive symbol indexes for true lazy member parsing, resolves COMDAT leaders and associative followers, coalesces/sorts COFF subsections, establishes deterministic placement, resolves globals/imports/synthetic Windows symbols, infers CRT startup roots when present, and applies relocations.
3. `pe` serializes a Windows PE32+ image from already-linked sections.
4. the CLI is a thin frontend over the library API.

The separation matters because Forge and ObLink are independent repositories: interoperability is defined by standard object formats and command-line/library contracts, not shared private data structures.
