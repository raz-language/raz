# Raz compiler

This directory contains the production Raz compiler, written in Raz.

The compiler source is organized by responsibility under `compiler/src/` and is compiled as ordinary semantic Raz modules. Each compiler module owns an explicit `raz_compiler_*` namespace and an import edge; production builds do not use physical source concatenation. No compiler source-order file is retained; deterministic qualification materialization is derived from discovered modules, while `src/main.rz` remains the small semantic entrypoint.

## Source layout

```text
src/
  frontend/
    lexer.rz
    parser.rz
  hir/
    core/
      model.rz
      builder.rz
      symbols.rz
      types.rz
    generics/
      instantiate.rz
      const_generics.rz
      type_instantiation.rz
    traits/
      matching.rz
      solver.rz
    semantic/
      ownership.rz
      reflection.rz
      expressions.rz
      statements.rz
      declarations.rz
      comptime.rz
  mir/
    core/
      model.rz
      builder.rz
    analysis/
      cfg.rz
      dataflow.rz
      liveness.rz
      dominance.rz
    ownership/
      places.rz
      moves.rz
      borrows.rz
      drops.rz
    transform/
      simplify_cfg.rz
      const_prop.rz
      remap.rz
      copy_prop.rz
      scalar.rz
      dce.rz
      cfg_cleanup.rz
      pipeline.rz
    verify/
      verifier.rz
    lowering.rz
    interpreter.rz
  backend/
    forge/
      writer.rz
      symbol_writer.rz
      tls_codegen.rz
      native_shape.rz
      native_support.rz
      native_types.rz
      native_operations.rz
      native_functions.rz
      native_aggregate.rz
      globals_codegen.rz
      function_codegen.rz
      codegen.rz
    llvm/
      writer.rz
      target.rz
      globals_codegen.rz
      codegen.rz
  driver/
    backend.rz
    cli.rz
    package.rz
    project.rz
    registry.rz
    registry_install.rz
    tooling.rz
  main.rz
```

## Boundaries

- `frontend/` owns source tokenization and syntax parsing.
- `hir/core/` owns HIR storage, builder state, namespaces/symbols, and core type operations.
- `hir/generics/` owns substitutions and concrete generic instantiation.
- `hir/traits/` owns generic trait matching and trait solving.
- `hir/semantic/` owns semantic construction, ownership, reflection, declarations, statements, expressions, and comptime.
- `mir/` owns HIR-to-MIR lowering, CFG/dataflow analysis, ownership facts, structural verification, backend-neutral scalar/CFG optimization, MIR execution/qualification, and MIR lifetime management.
- `backend/forge/` owns Forge textual/structured emission and native Forge lowering.
- `backend/llvm/` owns the LLVM compatibility/production backend implementation retained by this tree.
- `driver/` owns CLI parsing, backend selection, project loading, formatting/docs/test tooling, package manifests, registry resolution, and dependency locking.
- `main.rz` only orchestrates phases and process exit status.

The compiler is split at stable responsibility boundaries so semantic analysis, lowering, backend code generation, and project tooling can evolve independently.

## Source ordering

Raz packages use deterministic semantic module discovery and explicit imports. The generic project loader still recognizes `source-order.txt` for legacy packages, but the canonical compiler intentionally does not provide one. The compiler keeps no ordering metadata at all. Host-side qualification discovers `compiler/src/**/*.rz` directly and treats `src/main.rz` as the entrypoint; repository checks prevent compiler source-order files from returning.
