# Security policy

## Supported version

Security fixes are applied to the latest released line. Older snapshots are not
maintained.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability. Report it privately to
the maintainer with:

- Affected ObLink version and host platform
- The object files, archives, or command line that reproduce it
- Expected and observed behavior
- Potential security impact
- Any proposed mitigation

Avoid including secrets, personal data, or unrelated proprietary source. A
complete report should be acknowledged, reproduced, fixed with a regression
test, and disclosed after patched artifacts are available.

## Security boundaries

ObLink treats every input as untrusted: COFF and BigObj objects, static and
import archives, `.drectve` directives, and the library search paths those
directives introduce. A malformed or hostile input must produce a diagnostic,
never a malformed image or out-of-bounds access.

Specific properties the linker is expected to hold:

- Every offset read from an object or archive header is bounds-checked against
  the mapped input before use. Truncated headers, relocation tables, symbol
  tables, and string tables are hard errors.
- Section contributions are placed by logical extent, and relocation targets are
  validated against the section that owns them. A relocation that would write
  outside its section is an error rather than a silent overwrite.
- Emitted images are structurally consistent: section RVAs are contiguous and
  within `SizeOfImage`, and ASLR flags agree with the presence of a base
  relocation table.
- Unresolved symbols are always errors. ObLink never invents an import unless a
  Microsoft short-import member or GNU import stub supplies the DLL, name, and
  ordinal contract.

ObLink does not execute input, and it does not consult the network.
