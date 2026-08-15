# RXE malformed-module cases

These cases document hostile module mutations that the Raz RXE verifier must reject before serialization or reference execution.

- `bad-opcode.case`: opcode outside the declared ISA ranges.
- `bad-branch.case`: branch leaves its containing function.
- `bad-register.case`: operand register is >= 32.
- `bad-call-signature.case`: call argument count disagrees with callable metadata.
- `bad-layout.case`: typed allocation names an invalid/incompatible layout.
- `bad-slice-store.case`: slice-store value-register immediate is >= 32.
- `bad-export.case`: export identity disagrees with its function descriptor.
- `bad-fingerprint.case`: fingerprint lane is outside the canonical u32 range.
