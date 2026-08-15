# Raz 1.0 Stable Language Scope

This document defines the compatibility promise for Raz 1.0.

## Stable

The stable surface includes typed functions and locals; fixed-width signed and unsigned integer types plus `usize`, floating-point values and explicit numeric conversion; structs, tuples, fixed arrays, slices, and payload enums; pattern matching; lexical `defer`; ownership, moves, borrows, references, lifetimes, and deterministic drop elaboration; generics and const generics; traits, supertraits, associated items, static dispatch, and object-safe dynamic dispatch; closures and function pointers; iterators and range iteration; deterministic compile-time evaluation and supported reflection; structured async/await; explicit unsafe raw-pointer operations; and the declared native `extern` ABI boundary.

The compiler, standard library, package metadata, formatter, documentation generator, test runner, specification emitter, audit/SBOM commands, deterministic fuzz driver, Forge backend, and Raz-written LLVM backend together form the supported Raz 1.0 toolchain surface.

## Deliberate exclusions

Features rejected by the compiler with stable diagnostics are not implicitly part of Raz 1.0 merely because a future implementation could support them. In particular, unstable experimental library modules and target-specific native extensions do not gain the same portability or compatibility promise as `core`, `alloc`, and `std`.

## Compatibility rule

A Raz 1.0 implementation must preserve the accepted program semantics and public package/interface contracts defined by this repository's conformance suite. Additive extensions must not reinterpret already-valid stable programs.
