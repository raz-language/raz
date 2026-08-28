# Release Qualification

Raz release qualification has three tiers. The lighter tiers are useful while developing a change; the full tier is the one that matters for a release artifact.

## Source tier

```text
python tests/python/release-gate.py --tier source
```

The source tier runs repository structure checks and the RXE semantic/reference contracts. It needs no prebuilt compiler, so it is the quickest way to catch source-tree and specification mistakes on Windows or Linux.

## Runtime tier

The runtime tier requires one exact production compiler, Forge code generator, and runtime library built from the source revision being qualified:

```text
python tests/python/release-gate.py --tier runtime \
  --razc <compiler> \
  --forge-run <forge-codegen> \
  --runtime <runtime-library>
```

It requires:

- Forge ↔ LLVM maintained executable parity;
- Forge ↔ LLVM ↔ WASM/WASI maintained executable parity;
- exit status, stdout, and stderr equality;
- standard-library executable qualification;
- RXE independent decoder/reference semantic qualification.

`--skip-wasm` exists only for local development on machines without a WASI-capable Node.js installation. It is not a release configuration.

## Full tier

```text
python tests/python/release-gate.py --tier full \
  --razc <compiler> \
  --forge-run <forge-codegen> \
  --runtime <runtime-library>
```

The full tier adds automatic corpus discovery. Every `.rz` example with `fn main` that passes `raz check` is run through Forge and LLVM. Exit status, stdout, and stderr must match. Production LSP, bindgen, and C-header round trips are also required.

After normal `tools/bootstrap.py` completes its single self-host generation, the artifact paths can be resolved automatically:

```text
python tools/run-release-gate-from-bootstrap.py --tier full
```

## Release rule

A release is qualified only when the full tier succeeds using the exact compiler/runtime/Forge artifacts that will be staged. Structural source qualification alone is never sufficient evidence of backend correctness.

For releases requiring deterministic compiler fixed-point verification, run `tools/bootstrap.py --verify-reproducibility` before the release gate. CTest remains opt-in through the bootstrap test flag. The release gate complements bootstrap by checking observable language behavior across the backends users receive.
