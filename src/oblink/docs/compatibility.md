# Forge compatibility contract

ObLink interoperates with Forge through Microsoft AMD64 COFF/BigObj, not private Forge headers or in-process data structures.

The linker accepts the relocations Forge currently emits for ordinary code and data references:

- `IMAGE_REL_AMD64_REL32` through `REL32_5`
- `IMAGE_REL_AMD64_ADDR64`
- `IMAGE_REL_AMD64_ADDR32`
- `IMAGE_REL_AMD64_ADDR32NB`
- `IMAGE_REL_AMD64_SECTION`
- `IMAGE_REL_AMD64_SECREL`

It also consumes the Windows library conventions encountered when Forge/Raz link against C++ runtime code:

- archive-index-driven lazy COFF `.lib` extraction with one prevailing provider per unresolved name; unused members are not parsed
- Microsoft short import objects
- `/DEFAULTLIB:`, `/LIBPATH:`, `/NODEFAULTLIB`, `/FAILIFMISMATCH`, `/INCLUDE:` and `/ALTERNATENAME:` directives from `.drectve`
- `__imp_<symbol>` IAT symbols
- synthesized import descriptors appended to an `.idata` an input object
  already contributed, rather than emitted as a second section of that name
- linker-generated x64 import thunks for code imports
- PE import descriptor, ILT and IAT emission
- leader-keyed COMDAT selection and associative followers; a COMDAT whose
  only definitions are static is private to its object and is never treated
  as a duplicate, and a reference to a discarded definition resolves to the
  prevailing copy rather than to whatever sits at the start of the image
- uninitialized sections, whose size an object reports in `SizeOfRawData`
  with a null `PointerToRawData`, contribute image extent and no file bytes
- `.debug$S` / `.debug$T` are discarded, and the symbols only they reference
  do not pull archive members into the link
- `$` subsection ordering for MSVC CRT/TLS tables
- AMD64 `.pdata` exception directory with runtime-function sorting, `.reloc` base relocations, and TLS directory publication
- inferred `mainCRTStartup` / `wmainCRTStartup` / GUI startup roots when the selected CRT provides them, with freestanding direct-entry fallback
- linker-synthesized `__ImageBase` and absolute COFF symbols
- extended relocation-count (`IMAGE_SCN_LNK_NRELOC_OVFL`) decoding for very large generated objects
- configurable PE stack/heap reserves; Raz uses an 8 MiB stack for the production compiler

A direct flow is:

```text
forge compile input.fir --format=coff -o input.obj
oblink input.obj runtime.lib -L <sdk-lib-dir> -l ws2_32 -o app.exe
```

GNU-style import libraries are consumed by recovering an import record from each
per-symbol stub member -- `.idata$6` supplies the hint and name, `__imp_<symbol>`
the identity, and the DLL name is followed from the stub through the descriptor
head to the tail that holds the string -- and by dropping the descriptor head and
tail members, which describe exactly what ObLink synthesizes for itself. Replaying
their `.idata$2` records instead would publish descriptors whose thunk arrays are
mis-delimited, because GNU `.idata$N` grouping is what separates one DLL's arrays
from the next. A `.idata$2` section that survives to layout is therefore an error.

Unresolved symbols remain hard errors. ObLink does not silently manufacture an import unless a Microsoft import object supplies the DLL/name/ordinal contract.
