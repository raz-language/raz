# Changelog

All notable changes to ObLink are documented here.

## Unreleased

### Correctness

- Added reachability-based section/COMDAT elimination. Unreferenced function/data COMDATs are discarded before archive closure and final layout, so relocations from dead code no longer pull otherwise-unused archive members or imports into the link. Associative COMDATs follow their live parent, and references to discarded duplicate COMDATs are redirected to the prevailing definition.
- Uninitialized sections are placed by extent instead of by file bytes. An
  object reports a `.bss` section's size in `SizeOfRawData` with a null
  `PointerToRawData`, and reading that many bytes from offset zero copied the
  object's own COFF header into `.bss`. Every zero-initialized global reached
  the image non-zero, which hung linked programs in the CRT's startup spinlock.
- Section contributions advance by logical extent rather than accumulated file
  bytes, so two uninitialized contributions no longer receive the same offset
  and alias each other.
- A reference to a COMDAT definition that lost selection now resolves to the
  copy that prevailed. It previously kept a default-constructed address, so a
  call to a deduplicated function landed at offset 0 of the first output
  section.
- Empty output sections are dropped before RVAs are assigned. Two zero-length
  sections sharing an RVA produced an image the loader rejected with
  `STATUS_INVALID_IMAGE_FORMAT`.
- ASLR flags now agree with the image: `DYNAMIC_BASE` and high-entropy ASLR are
  set only when a base relocation table exists, and `RELOCS_STRIPPED` otherwise.
- COFF-only section flags (`IMAGE_SCN_LNK_*` and the alignment field) are masked
  out of image section characteristics.
- `SECREL` against an absolute symbol resolves to the symbol's own value rather
  than failing.

### Compatibility

- Added GNU/MinGW import library support. Each per-symbol stub is recovered into
  the same import record a Microsoft short-import member produces: `.idata$6`
  supplies the hint and name, `__imp_<symbol>` the identity, and the DLL name is
  followed from the stub through the descriptor head to the tail that holds the
  string. Descriptor head and tail members are inert, because what they describe
  is exactly what ObLink synthesizes.
- Replaying a GNU library's own `.idata$2` descriptors is deliberately not done:
  the head's zero-size `.idata$4`/`.idata$5` markers make two DLLs' descriptors
  share a `FirstThunk`, and a PE has only one import directory. A `.idata$2`
  section that survives to layout is an error.
- Synthesized import descriptors append to an `.idata` an input object already
  contributed rather than emitting a second section of that name.
- An archive without a linker index is fully indexed before any member is
  classified, so whether an import stub is recognized cannot depend on where it
  sits in the archive.
- `.debug$S` / `.debug$T` are discarded when no debug output is requested, and
  the symbols only they reference no longer pull archive members into the link.
- A COMDAT whose only definitions are static is private to its object and is no
  longer treated as a cross-object duplicate.

### Tooling

- Added `--map <path>`, writing section and symbol addresses.
- Added `--verbose`, reporting which archive member satisfied which symbol and
  the final section layout.

### Testing

- Added a direct PE32+ image-builder regression and made the default strict test
  configuration cover COFF parsing, linking, PE construction, response files,
  and CLI version behavior under `-Werror`.
- Added an integrated Forge compatibility gate that emits AMD64 COFF with the
  standalone Forge CLI, links it twice with ObLink, validates the PE outputs,
  and requires byte-for-byte deterministic SHA-256 identity.
- Added `tests/pe_validate.hpp`, which checks a linked image against the rules
  the Windows loader enforces. Tests previously asserted only that a file began
  with `MZ`, which cannot distinguish a valid image from one the loader rejects.
- Every successful link in the test suite is now validated, with targeted
  regressions for uninitialized-section placement, discarded-COMDAT resolution,
  contributed `.idata` coexistence, and GNU import libraries.
