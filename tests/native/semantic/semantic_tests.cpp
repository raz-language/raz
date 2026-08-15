// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/semantic/semantic_analyzer.hpp"
#include "compiler/source/source_manager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool check(std::string source, bool should_succeed) {
  raz::compiler::SourceManager sources;
  raz::compiler::DiagnosticEngine diagnostics;
  const auto file = sources.add_virtual_file("semantic-test.rz", std::move(source));
  raz::compiler::Lexer lexer(sources, diagnostics, file);
  raz::compiler::Parser parser(sources, diagnostics, lexer.lex_all());
  const auto syntax = parser.parse();
  if (!diagnostics.has_errors()) {
    raz::compiler::SemanticAnalyzer analyzer(diagnostics);
    const auto hir = analyzer.analyze(syntax);
    (void)hir;
  }
  return diagnostics.has_errors() != should_succeed;
}

}  // namespace

int main() {
  if (!check("fn add(i64 a, i64 b) -> i64 { i64 sum = a + b; return sum; }", true)) {
    std::cerr << "valid semantic program failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn main() { Missing value; }", false)) {
    std::cerr << "unknown type was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn main() { i64 value = missing; }", false)) {
    std::cerr << "unknown name was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn main() { i64 value = 1; i64 value = 2; }", false)) {
    std::cerr << "duplicate local was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn valid() -> i64 { i64 value = 41; f64 wide = value as f64; wide += 1.75; return wide as i64; }", true)) {
    std::cerr << "valid numeric casts failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid() -> i64 { i64 value = 1; f64 other = 2.0; f64 result = value + other; return 0; }", false)) {
    std::cerr << "mixed numeric arithmetic without a cast was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn takes(f64 value) -> f64 { return value; } fn invalid() -> i64 { f64 value = takes(1); return 0; }", false)) {
    std::cerr << "mixed numeric call argument without a cast was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid(u64 value) -> f64 { return value as f64; }", false)) {
    std::cerr << "unsupported u64 to float cast was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn valid(i64 a, i64 b) -> bool { return (a & b) != 0 && a < b; }", true)) {
    std::cerr << "valid logical and bitwise operators failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid(i64 value) -> bool { return value && true; }", false)) {
    std::cerr << "non-bool logical operand was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid(f64 value) -> f64 { return value << 1; }", false)) {
    std::cerr << "floating-point shift was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Box { i64 value; } fn consume() -> i64 { Box first = Box(1); Box second = move first; return second.value; }", true)) {
    std::cerr << "valid explicit move failed\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Box { i64 value; } fn invalid() -> i64 { Box first = Box(1); Box second = move first; return first.value; }", false)) {
    std::cerr << "use after move was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Box { i64 value; } fn valid() -> i64 { Box first = Box(1); Box second = move first; first = Box(2); return first.value + second.value; }", true)) {
    std::cerr << "assignment did not reinitialize moved value\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Box { i64 value; } fn invalid() { Box first = Box(1); Box second = move first; Box third = move first; }", false)) {
    std::cerr << "double move was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Pair { i64 left; i64 right; } fn valid() -> i64 { Pair pair = Pair(1, 2); i64& mut left = &mut pair.left; i64& mut right = &mut pair.right; *left += 10; *right += 20; return pair.left + pair.right; }", true)) {
    std::cerr << "disjoint mutable field borrows failed\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Pair { i64 left; i64 right; } fn invalid() { Pair pair = Pair(1, 2); i64& mut first = &mut pair.left; i64& mut second = &mut pair.left; }", false)) {
    std::cerr << "overlapping mutable field borrows were not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Pair { i64 left; i64 right; } fn invalid() { Pair pair = Pair(1, 2); i64& field = &pair.left; Pair& mut whole = &mut pair; }", false)) {
    std::cerr << "whole-object borrow did not conflict with field borrow\n";
    return EXIT_FAILURE;
  }

  if (!check("fn valid() -> i64 { i64 values[2] = [1, 2]; i64& mut first = &mut values[0]; i64& mut second = &mut values[1]; *first += 1; *second += 1; return values[0] + values[1]; }", true)) {
    std::cerr << "disjoint constant array element borrows failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid(i64 index) { i64 values[2] = [1, 2]; i64& dynamic = &values[index]; i64& mut fixed = &mut values[0]; }", false)) {
    std::cerr << "dynamic array borrow did not conservatively overlap fixed element\n";
    return EXIT_FAILURE;
  }

  if (!check("fn valid(i64& mut input) -> i64 { { i64& mut child = &mut *input; *child += 1; } return *input; }", true)) {
    std::cerr << "mutable reborrow failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid(i64& mut input) { i64& mut child = &mut *input; *input += 1; }", false)) {
    std::cerr << "use of suspended parent reference was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid(i64& input) { i64& mut child = &mut *input; }", false)) {
    std::cerr << "mutable reborrow through shared reference was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn identity(i64& input) -> i64& { return input; }", true)) {
    std::cerr << "returning a parameter reference failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid() -> i64& { i64 value = 1; return &value; }", false)) {
    std::cerr << "returning a local reference was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Box { i64 value; } fn invalid(bool take) -> i64 { Box value = Box(1); if take { Box consumed = move value; } return value.value; }", false)) {
    std::cerr << "conditional move was not joined after if\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Box { i64 value; } fn valid(bool take) -> i64 { Box value = Box(1); Box consumed = move value; if take { value = Box(2); } else { value = Box(3); } return value.value; }", true)) {
    std::cerr << "reinitialization in every branch did not restore ownership\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Box { i64 value; } fn invalid(bool keep) -> i64 { Box value = Box(1); while keep { Box consumed = move value; break; } return value.value; }", false)) {
    std::cerr << "loop-carried possible move was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("enum Choice { First, Second } struct Box { i64 value; } fn invalid(Choice choice) -> i64 { Box value = Box(1); match choice { Choice.First => { Box consumed = move value; }, Choice.Second => { } } return value.value; }", false)) {
    std::cerr << "match-arm move was not joined\n";
    return EXIT_FAILURE;
  }

  if (!check("fn valid() -> i64 { i64 value = 1; i64& mut edit = &mut value; *edit += 1; i64& view = &value; return *view; }", true)) {
    std::cerr << "nonlexical borrow shortening failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn choose(bool first, i64& left, i64& right) -> i64& { if first { return left; } return right; }", false)) {
    std::cerr << "ambiguous reference return origin was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn pass(i64& value) -> i64& { return value; } fn relay(i64& value) -> i64& { return pass(value); }", false)) {
    std::cerr << "unproven call-return lifetime was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Marker { fn read(Self& self) -> i64; } struct Value { i64 raw; } impl Marker for Value { fn read(Value& self) -> i64 { return self.raw; } } fn keep<T: Marker>(T value) -> T { return value; } fn main() -> i64 { Value value = Value(7); Value kept = keep<Value>(value); return kept.raw; }", true)) {
    std::cerr << "valid trait bound or implementation failed\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Marker { fn read(Self& self) -> i64; } struct Value { i64 raw; } fn keep<T: Marker>(T value) -> T { return value; } fn main() { Value value = Value(7); Value kept = keep<Value>(value); }", false)) {
    std::cerr << "unsatisfied trait bound was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Marker { fn read(Self& self) -> i64; } struct Value { i64 raw; } impl Marker for Value { fn read(Value& self) -> bool { return true; } }", false)) {
    std::cerr << "invalid trait implementation signature was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn sum() -> i64 { i64 values[3] = [1, 2, 3]; i64 total = 0; for value in values { total += value; } return total; }", true)) {
    std::cerr << "fixed-array for loop failed semantic analysis\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid() { i64 value = 1; for item in value { item; } }", false)) {
    std::cerr << "non-array for loop was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn main() -> i64 { i64 factor = 3; auto scale = fn(i64 value) -> i64 { return value * factor; }; factor = 9; return scale(14); }", true)) {
    std::cerr << "stored scalar closure capture failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn main() -> i64 { auto twice = fn(i64 value) -> i64 { return value * 2; }; return twice(21); }", true)) {
    std::cerr << "stored noncapturing closure failed\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Resource { i64 handle; } fn main() -> i64 { Resource resource = Resource(7); auto read = fn() -> i64 { return resource.handle; }; return read(); }", false)) {
    std::cerr << "resource capture was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Pair { i64 left; i64 right; } impl Copy for Pair { } fn main() -> i64 { Pair pair = Pair(1, 2); auto sum = fn() -> i64 { return pair.left + pair.right; }; pair.left = 9; return sum(); }", true)) {
    std::cerr << "Copy aggregate closure capture failed\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Measure { @pure @no_panic fn get(Self& self) -> i64; } struct Value { i64 raw; } impl Measure for Value { @pure @no_panic fn get(Value& self) -> i64 { return self.raw; } } @pure @no_panic fn read(Value& value) -> i64 { return value.get(); }", true)) {
    std::cerr << "trait effect contract propagation failed\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Measure { @pure fn get(Self& self) -> i64; } struct Value { i64 raw; } impl Measure for Value { fn get(Value& self) -> i64 { return self.raw; } }", false)) {
    std::cerr << "trait implementation was allowed to weaken an effect contract\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Measure { fn get(Self& self) -> i64; } struct Value { i64 raw; } impl Measure for Value { fn get(Value& self) -> i64 { return self.raw; } } @pure fn invalid(Value& value) -> i64 { return value.get(); }", false)) {
    std::cerr << "pure caller was allowed to call an unqualified trait method\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Value { i64 raw; } impl Value { @pure fn get(Value& self) -> i64 { return self.raw; } } @pure fn read(Value& value) -> i64 { return value.get(); }", true)) {
    std::cerr << "inherent effect contract propagation failed\n";
    return EXIT_FAILURE;
  }

  if (!check("@abi(C) @link_name(raz_rt_time_unix_millis) extern fn clock_now() -> i64; fn main() -> i64 { return clock_now(); }", true)) {
    std::cerr << "C ABI link-name declaration failed\n";
    return EXIT_FAILURE;
  }

  if (!check("@abi(fastcall) extern fn invalid() -> i64;", false)) {
    std::cerr << "unsupported ABI was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("@link_name(native_symbol) fn invalid() { }", false)) {
    std::cerr << "link_name on non-extern function was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("@target_feature(avx2) fn invalid() { }", false)) {
    std::cerr << "safe target-feature function was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("@target_feature(avx2) unsafe fn valid() { }", true)) {
    std::cerr << "unsafe target-feature function failed\n";
    return EXIT_FAILURE;
  }

  if (!check("async fn compute() -> i64 { return 42; } async fn pipeline() -> i64 { i64 started = spawn compute(); return await started; }", true)) {
    std::cerr << "async/await/spawn semantics failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid() -> i64 { return await 1; }", false)) {
    std::cerr << "await outside async function was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("async fn child() -> i64 { return 1; } async fn invalid() -> i64 { i64 local = 40; i64& view = &local; i64 task = spawn child(); i64 ready = await task; return *view + ready; }", false)) {
    std::cerr << "local reference live across await was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("async fn child() -> i64 { return 1; } async fn valid(i64& input) -> i64 { i64 task = spawn child(); i64 ready = await task; return *input + ready; }", true)) {
    std::cerr << "parameter reference across await was rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("async fn child() -> i64 { return 1; } async fn valid() -> i64 { i64 local = 40; { i64& view = &local; i64 copied = *view; } i64 task = spawn child(); i64 ready = await task; return local + ready; }", true)) {
    std::cerr << "dead local reference before await remained active\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid() -> i64 { return spawn 1; }", false)) {
    std::cerr << "spawn outside async function was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn add(i64 a, i64 b) -> i64 { return a + b; } fn apply(fn(i64,i64)->i64 op, i64 a, i64 b) -> i64 { return op(a, b); } fn main() -> i64 { fn(i64,i64)->i64 op = add; return apply(op, 20, 22); }", true)) {
    std::cerr << "typed function pointer semantics failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn unary(i64 value) -> i64 { return value; } fn main() -> i64 { fn(i64,i64)->i64 op = unary; return op(1, 2); }", false)) {
    std::cerr << "incompatible function pointer assignment was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Value { i64 raw; } impl Value { @pure fn get(Value& self) -> i64 { return self.raw; } fn set(Value& mut self, i64 next) { self.raw = next; } } comptime { assert(method_count<Value>() == 2); assert(method_name<Value>(0) == \"get\"); assert(method_return_type_name<Value>(0) == \"i64\"); assert(method_parameter_count<Value>(1) == 2); assert(method_parameter_type_name<Value>(1, 1) == \"i64\"); assert(method_has_attribute<Value>(0, \"pure\")); }", true)) {
    std::cerr << "compile-time inherent method reflection failed\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Value { i64 raw; } impl Value { fn get(Value& self) -> i64 { return self.raw; } } comptime { assert(method_name<Value>(1) == \"missing\"); }", false)) {
    std::cerr << "out-of-range reflected method index was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Storage { type Key; const CAPACITY: usize; @pure fn get(Self& self, i64 index) -> i64; fn set(Self& mut self, i64 index, i64 value); } comptime { assert(trait_method_count<Storage>() == 2); assert(trait_method_name<Storage>(0) == \"get\"); assert(trait_method_parameter_type_name<Storage>(1, 2) == \"i64\"); assert(trait_method_has_attribute<Storage>(0, \"pure\")); assert(associated_type_name<Storage>(0) == \"Key\"); assert(associated_const_name<Storage>(0) == \"CAPACITY\"); assert(associated_const_type_name<Storage>(0) == \"usize\"); }", true)) {
    std::cerr << "compile-time trait reflection failed\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Storage { fn get(Self& self) -> i64; } comptime { assert(trait_method_name<Storage>(1) == \"missing\"); }", false)) {
    std::cerr << "out-of-range reflected trait method index was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Storage { type Key; const CAPACITY: usize; fn get(Self& self) -> i64; } struct Buffer { i64 value; } impl Storage for Buffer { type Key = i32; const CAPACITY: usize = 64; fn get(Buffer& self) -> i64 { return self.value; } } comptime { assert(implements_trait<Buffer, Storage>()); assert(impl_associated_type_count<Buffer, Storage>() == 1); assert(impl_associated_type_name<Buffer, Storage>(0) == \"Key\"); assert(impl_associated_type_binding_name<Buffer, Storage>(0) == \"i32\"); assert(impl_associated_const_count<Buffer, Storage>() == 1); assert(impl_associated_const_name<Buffer, Storage>(0) == \"CAPACITY\"); assert(impl_associated_const_type_name<Buffer, Storage>(0) == \"usize\"); assert(impl_associated_const_value<Buffer, Storage>(0) == 64); }", true)) {
    std::cerr << "compile-time concrete trait implementation reflection failed\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Storage { type Key; fn get(Self& self) -> i64; } struct Buffer { i64 value; } comptime { assert(impl_associated_type_count<Buffer, Storage>() == 0); }", false)) {
    std::cerr << "reflection over a missing concrete trait implementation was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Provider { type Item; const WIDTH: usize; } struct Box<T> { T value; } impl<T: Copy> Provider for Box<T> { type Item = T; const WIDTH: usize = 16; } comptime { assert(implements_trait<Box<i64>, Provider>()); assert(impl_associated_type_binding_name<Box<i64>, Provider>(0) == \"i64\"); assert(impl_associated_const_value<Box<i64>, Provider>(0) == 16); }", true)) {
    std::cerr << "generic implementation associated binding reflection failed\n";
    return EXIT_FAILURE;
  }

  if (!check("trait Provider { type Item; const WIDTH: usize; } struct Box<T> { T value; } impl<T: Copy> Provider for Box<T> { type Item = T; }", false)) {
    std::cerr << "missing generic associated constant binding was not rejected\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
