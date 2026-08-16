# Semantic Query Database

Raz compiler semantics are organized around a shared query database. Traits,
generics, method resolution, associated-type normalization, aggregate layout,
and field offsets use the same memoization, dependency, cycle-detection, and
invalidation machinery rather than subsystem-specific semantic caches.

## Query identity

A query key consists of a query kind and three numeric semantic key components.
Migrated query families use canonical compiler-local identities:

```text
Method       = (receiver TypeId, SymbolId)
Trait impl   = (TraitId, TypeId)
Trait bound  = (TypeId, TraitId or builtin bound)
Assoc type   = (base TypeId, TraitId, SymbolId)
Monomorph    = canonical exact GenericRequestId
```

`SymbolId` values are interned from exact identifier bytes. `TypeId` values are
interned from complete HIR type shapes. Generic request IDs contain the exact
entity/template/argument tuple. Hashes may accelerate lookup, but hashes and
source offsets never define semantic identity.

## Query execution

The runtime provides:

- memoized positive and safe negative results;
- semantic epochs for query-family-specific table state;
- active-query cycle detection;
- parent-to-child dependency recording;
- reverse-dependent invalidation;
- two-word query results for compound values such as size/alignment.

Nested queries record dependency edges. If query `A` executes query `B`, the
database records `A -> B`. When `B` becomes invalid, cached `A` is invalidated
transitively.

## Invalidation

Mutable semantic inputs are fingerprinted by family, including trait/impl,
method, aggregate, generic-template, function, and associated-item state. A
change invalidates only query families rooted in the changed input and then
walks reverse dependencies to invalidate affected parents. Unrelated cached
queries remain reusable.

The compiler retains a monotonically increasing semantic revision for telemetry
and sequencing, but cache validity is not determined by requiring every entry
to match one global revision.

## Module fingerprints

HIR records two deterministic fingerprints per physical module:

- **source fingerprint** — covers the entire physical module source;
- **exported-interface fingerprint** — covers public top-level declaration
  regions only.

The distinction allows project-level incremental compilation to distinguish an
implementation change from a change that can affect importing modules. The
interface fingerprint is conservative: public declaration bodies may be part
of the hashed declaration region, so some changes can invalidate more work than
strictly necessary, but it does not allow a stale semantic result to survive.

## Persistent incremental state

The compiler persists incremental state under the package-local `.raz/cache`
directory. Persistent state is an optimization only: deleting the directory must
never change program semantics.

The persistent layer stores:

- a versioned whole-build key derived from complete source contents and backend
  configuration;
- the corresponding completed backend artifact for exact-match reuse;
- per-module source and exported-interface fingerprints produced by semantic
  analysis;
- stable module ownership views for concrete HIR and MIR functions;
- per-module optimized-MIR fingerprints.

Module IDs are deterministic module ordinals rather than byte offsets in the
combined source arena. Source-length changes therefore do not renumber every
following module. HIR records each concrete function's origin module and compact
per-module function views; MIR preserves the same ownership through optimization.
These views define the serialization and code-generation boundary for
module-granular incremental compilation.

The production project loader derives deterministic ordering for acyclic
same-package module graphs from declared namespace imports. Filesystem discovery
order is not a semantic dependency. Packages with cyclic module imports retain a
conservative fallback until interface-first strongly connected component
compilation is available. The semantic snapshot also persists module package and
namespace identity plus forward dependency edges. On the next compiler process,
source and exported-interface fingerprints classify modules as fresh,
implementation-dirty, or interface-dirty; only interface changes invalidate
importing modules.

Artifact reuse is intentionally exact. Check, test, and run operations always
execute the compiler pipeline rather than short-circuiting through a cached build
artifact. Module fingerprints are stored separately from artifacts so future
module-granular HIR and MIR serialization can reuse the same source/interface
identity model.

The cache format carries an explicit schema generation. Compiler changes that
make previously produced backend artifacts incompatible must advance that
generation. Cache readers must treat unknown or malformed state as a miss.

## Bootstrap boundary

The semantic query database is implemented in Raz. The compatibility-pinned native host compiler
compiler remains a host compiler and does not acquire production query,
generic, trait, or invalidation policy.
