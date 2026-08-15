// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/binary.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/printer.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

int main() {
    try {
        constexpr auto source = R"(module @binary {
  array @Words = i16[8]
  struct @Inner { value: i64, words: array @Words }
  struct @Record { tag: i8, inner: struct @Inner, code: i16 }
  global @counter: i64 = 7
  constant @limit: i32 = 9
  constant @message: i8[8] align 8 = "Forge!\0\0"
  global @scratch: i8[32] align 16 = zero
  thread_local global @tls_counter: i64 = 5
  global @record_storage: struct @Record = { tag: 3, inner: { value: 99, words: [1, 2, 3, 4, 5, 6, 7, 8] }, code: 17 }
  constant @word_storage: array @Words = [8, 7, 6, 5, 4, 3, 2, 1]
  global @reader_slot: callback @RecordReader[3] align 8 = zero
  signature @RecordReader(%record: borrow struct @Record) -> i64
  func @borrow_record() -> owned struct @Record {
  entry:
    %record = global.address ptr @record_storage
    return %record
  }
  func @borrow_code(%record: borrow struct @Record) -> borrow struct @Record from 0 {
  entry:
    return %record
  }
  func @inspect_record(%record: borrow struct @Record) -> i64 {
  entry:
    %address = struct.field.name.address ptr %record @Record code
    %small = load i16 %address align 2
    %value = zero_extend i64 %small
    return %value
  }
  func @mutate_record(%record: borrow mut struct @Record, %replacement: i16) -> void {
  entry:
    %address = struct.field.name.address ptr %record @Record code
    store i16 %replacement %address align 2
    return
  }
  func @read_record(%record: owned struct @Record) -> i64 {
  entry:
    %address = struct.field.name.address ptr %record @Record code
    %small = load i16 %address align 2
    %value = zero_extend i64 %small
    return %value
  }
  func @choose(%condition: i1, %left: i32, %right: i32) -> i32 {
  entry:
    %scratch = stack.alloc ptr 8 align 8
    store i32 %left %scratch align 4
    branch %condition, yes(%left), no(%right)
  yes(%value: i32):
    return %value
  no(%value2: i32):
    return %value2
  }
}
)";
        auto parsed = forge::ir::parse_module(source);
        require(parsed.ok(), "test IR did not parse");
        auto encoded = forge::ir::write_binary(*parsed.module);
        require(encoded.ok(), "binary encoding failed");
        require(encoded.bytes.size() > 16, "binary output is missing payload");
        auto decoded = forge::ir::read_binary(encoded.bytes);
        require(decoded.ok(), "binary decoding failed");
        require(forge::ir::print_module(decoded.module) == forge::ir::print_module(*parsed.module), "binary round-trip changed the IR");
        require(decoded.module.globals().back().function_signature_name == "RecordReader",
                "binary round-trip lost callback global signature metadata");
        bool found_tls = false;
        for (const auto& global : decoded.module.globals())
            found_tls = found_tls || (global.name == "tls_counter" && global.is_thread_local);
        require(found_tls, "binary round-trip lost TLS global storage metadata");
        require(decoded.module.functions().front().is_signature, "binary round-trip lost signature declaration metadata");
        require(decoded.module.functions()[1].return_owned, "binary round-trip lost owned return metadata");
        require(decoded.module.functions()[2].return_borrow_mode == forge::ir::BorrowMode::immutable &&
                decoded.module.functions()[2].return_borrow_parameter == 0,
                "binary round-trip lost borrowed return metadata");
        require(decoded.module.functions()[3].parameters.front().borrow_mode == forge::ir::BorrowMode::immutable,
                "binary round-trip lost immutable borrow parameter metadata");
        require(decoded.module.functions()[4].parameters.front().borrow_mode == forge::ir::BorrowMode::mutable_,
                "binary round-trip lost mutable borrow parameter metadata");
        require(decoded.module.functions()[5].parameters.front().owned, "binary round-trip lost owned parameter metadata");

        auto corrupted = encoded.bytes;
        corrupted.back() ^= std::byte{0x01};
        require(!forge::ir::read_binary(corrupted).ok(), "corrupted binary unexpectedly decoded");

        auto truncated = encoded.bytes;
        truncated.resize(truncated.size() - 3);
        require(!forge::ir::read_binary(truncated).ok(), "truncated binary unexpectedly decoded");
        std::cout << "Forge binary IR tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
