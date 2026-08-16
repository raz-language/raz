# Raz 1.0 language stability

Raz 1.0 defines the supported language contract implemented by the production compiler and conformance suite.

## Stable

The documented syntax, type system, ownership and borrowing rules, traits and generics, pattern matching, modules and visibility, error handling, package interfaces, and backend-neutral program semantics are stable within the Raz 1.x line.

Compatible 1.x releases may add diagnostics, optimizations, library APIs, targets, and language capabilities that do not invalidate existing well-formed 1.0 programs.

## Compatibility

Breaking syntax or semantic changes require a new major language version. Platform-specific standard-library APIs may vary where the underlying operating system has no equivalent facility; such differences are documented by module.

The language specification is the normative prose reference. The conformance suite is the executable compatibility reference.
