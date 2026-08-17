# Common diagnostics

Extended explanations for the diagnostics people hit most often. Each entry says what
the compiler observed, why the rule exists, and how to resolve it.

The complete list of codes is the [diagnostic index](DIAGNOSTIC-INDEX.md). Output
formats, warning policy, and the machine-readable catalog are covered in the
[CLI reference](CLI.md#diagnostics).

## Ownership and moves

### `D2054` — use of moved value

```text
error[D2054]: use of moved value 'buffer'
```

Ownership of the value was transferred elsewhere — by a `move`, by passing it to a
function that takes it by value, or by returning it — and the original binding is no
longer valid. `D2053` is the same situation reported at the second move rather than
at a use.

Resolve it by deciding what the code actually needs:

- **The callee only needs to read it** — pass a shared reference, `T&`.
- **The callee needs to modify it** — pass a mutable reference, `T&mut`.
- **Both sides genuinely need a value** — clone it, if the type implements `Clone`.
- **The original is genuinely finished** — move the use after the move, or restructure
  so the last use precedes the transfer.

### `D2052` — moving a `Copy` value

```text
warning[D2052]: moving Copy value 'count' is equivalent to copying it
```

The type satisfies `Copy`, so `move` has no effect. The diagnostic is a lint rather
than an error: delete the `move`. It carries the "unnecessary" tag, so editors present
it as a redundancy hint.

### `D2055`, `D2056`, `D2057` — overlapping borrows

```text
error[D2056]: cannot mutably borrow 'state' while an overlapping borrow is active
```

Raz allows either any number of shared borrows or exactly one mutable borrow of the
same storage at a time, and enforces it so that a mutable reference is genuinely
exclusive.

Loans are **non-lexical**: a borrow stops constraining its source after its final use,
not at the closing brace. In practice this means the fix is usually to finish with one
borrow before starting the next, rather than to introduce a scope:

- Move the last read of the shared borrow above the mutable borrow.
- Copy the value out of the borrow if only a scalar is needed afterwards.
- Split the data so the two borrows touch different fields; field borrows are tracked
  independently.

### `D2061` — reference bindings cannot be rebound

```text
error[D2061]: reference and slice bindings cannot be rebound; create a new binding instead
```

A reference or slice binding is an alias for particular storage, and reassigning it
would silently change what a live alias designates. Introduce a new binding for the
new target instead.

### `D2064`, `D2066`, `D2067` — references escaping their origin

```text
error[D2064]: cannot return a reference to local storage 'scratch'
```

The returned reference points at storage that dies when the function returns. Raz also
tracks provenance through aggregates, so a borrow cannot escape indirectly inside a
returned struct, tuple, or array — including when the aggregate is assigned to a local
first and returned later.

`D2066` and `D2067` are the related cases where the origin cannot be proven, or where a
return borrows from several parameters and the relationship between them has to be
stated explicitly. Return an owned value, or take the destination as a caller-provided
`&mut` parameter.

## Types

### `D2009` — incompatible binary operands

```text
error[D2009]: binary operands have incompatible types 'i64' and 'f64'
```

Raz does not insert numeric coercions. The frontend materializes typed conversions
before backend lowering, so a mixed-width or mixed-kind operation must say what it
means with `as`:

```raz
f64 ratio = count as f64 / total;
```

`D2005` and `D2010` are the initialization and assignment forms of the same rule.

### `D2017` — bitwise and shift operators require integer operands

A frequent cause is precedence rather than types. Bitwise operators bind **more
loosely than comparison** in Raz, so:

```raz
if (flags & MASK == MASK) { }     // parses as flags & (MASK == MASK)
if ((flags & MASK) == MASK) { }   // intended
```

See [§2.6 of the specification](LANGUAGE-SPECIFICATION.md#26-precedence-and-associativity).

### `D2007` — condition must have type `bool`

Conditions are not truthy. Compare explicitly: `if (count != 0)` rather than
`if (count)`.

## Pattern matching and errors

### `D2038` — non-exhaustive match

```text
error[D2038]: non-exhaustive match; missing variant 'Message.Quit'
```

Every variant must be handled, either explicitly or by a wildcard arm. Exhaustiveness
is what makes adding an enum variant a compile-time task list rather than a runtime
surprise, so prefer naming the remaining variants over adding `_` unless the enum is
genuinely open-ended.

`D2035` and `D2037` report the opposite problem: a duplicate wildcard or a variant
matched twice.

### `D2047` — `?` requires `Result<T,E>` or `Option<T>`

Postfix `?` propagates a non-success value, so it is only meaningful on those types,
and only where the enclosing function's return type can carry what is propagated.
Either change the return type, or handle the value with `match`.

## Statements

### `D2050` — deferred code cannot contain `return`, `break`, or `continue`

`defer` runs during scope exit, including early exits. Allowing it to redirect control
flow would make the exit path itself ambiguous. Keep `defer` to cleanup, and put the
control-flow decision at the point where the scope is left.

## Lexical

### `D0002` — numeric base prefix requires at least one digit

`0x`, `0o`, and `0b` must be followed by a digit in that base. Note that `_` is a
separator and does not count as a digit: `0x_` is rejected, `0xFF_FF` is fine.

### `D0001` — unterminated block comment

Block comments nest, so an unmatched `/*` inside a comment keeps the comment open.
The diagnostic points at the outermost unterminated opener.
