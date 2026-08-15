// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/binary.hpp"
#include "forge/ir/verifier.hpp"
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace forge::ir {
namespace {
constexpr std::array<std::byte, 4> magic{std::byte{'F'}, std::byte{'I'}, std::byte{'R'}, std::byte{'B'}};
constexpr std::uint32_t max_collection = 1'000'000;
constexpr std::uint32_t max_string = 16 * 1024 * 1024;

std::uint32_t checksum(std::span<const std::byte> bytes) {
    std::uint32_t hash = 2166136261u;
    for (const auto byte : bytes) {
        hash ^= std::to_integer<std::uint8_t>(byte);
        hash *= 16777619u;
    }
    return hash;
}

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
    void u16(std::uint16_t value) { for (unsigned i = 0; i < 2; ++i) u8(static_cast<std::uint8_t>(value >> (i * 8))); }
    void u32(std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(value >> (i * 8))); }
    void string(std::string_view value) {
        if (value.size() > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("string too large");
        u32(static_cast<std::uint32_t>(value.size()));
        for (const char ch : value) u8(static_cast<std::uint8_t>(ch));
    }
    void type(Type value) { u8(static_cast<std::uint8_t>(value.kind())); }
    [[nodiscard]] std::vector<std::byte>& bytes() noexcept { return bytes_; }
private:
    std::vector<std::byte> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}
    std::uint8_t u8() { require(1); return std::to_integer<std::uint8_t>(bytes_[position_++]); }
    std::uint16_t u16() { std::uint16_t value{}; for (unsigned i = 0; i < 2; ++i) value |= static_cast<std::uint16_t>(u8()) << (i * 8); return value; }
    std::uint32_t u32() { std::uint32_t value{}; for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(u8()) << (i * 8); return value; }
    std::string string() {
        const auto size = u32();
        if (size > max_string) throw std::runtime_error("string exceeds format limit");
        require(size);
        std::string result(size, '\0');
        if (size != 0) std::memcpy(result.data(), bytes_.data() + position_, size);
        position_ += size;
        return result;
    }
    Type type() {
        const auto raw = u8();
        if (raw > static_cast<std::uint8_t>(TypeKind::ptr)) throw std::runtime_error("invalid type tag");
        return Type(static_cast<TypeKind>(raw));
    }
    [[nodiscard]] bool ended() const noexcept { return position_ == bytes_.size(); }
private:
    void require(std::size_t count) const { if (count > bytes_.size() - position_) throw std::runtime_error("truncated binary IR"); }
    std::span<const std::byte> bytes_;
    std::size_t position_{};
};

template<class T, class F>
void write_vector(Writer& writer, const std::vector<T>& values, F&& write) {
    if (values.size() > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("collection too large");
    writer.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) write(value);
}

template<class F>
void read_count(Reader& reader, F&& read) {
    const auto count = reader.u32();
    if (count > max_collection) throw std::runtime_error("collection exceeds format limit");
    for (std::uint32_t i = 0; i < count; ++i) read();
}

void write_decl(Writer& writer, const ValueDecl& value) {
    writer.string(value.name);
    writer.type(value.type);
    writer.u8(static_cast<std::uint8_t>(value.aggregate_kind));
    writer.string(value.aggregate_name);
    writer.u8(value.owned ? 1U : 0U);
    writer.u8(static_cast<std::uint8_t>(value.borrow_mode));
    writer.string(value.function_signature_name);
}

ValueDecl read_decl(Reader& reader, std::uint16_t minor) {
    ValueDecl value;
    value.name = reader.string();
    value.type = reader.type();
    if (minor >= 10) {
        const auto kind = reader.u8();
        if (kind > static_cast<std::uint8_t>(AggregateRefKind::array)) throw std::runtime_error("invalid aggregate value kind");
        value.aggregate_kind = static_cast<AggregateRefKind>(kind);
        value.aggregate_name = reader.string();
        if (minor >= 13) value.owned = reader.u8() != 0;
        if (minor >= 16) {
            const auto mode = reader.u8();
            if (mode > static_cast<std::uint8_t>(BorrowMode::mutable_)) throw std::runtime_error("invalid parameter borrow mode");
            value.borrow_mode = static_cast<BorrowMode>(mode);
        }
        if (minor >= 19) value.function_signature_name = reader.string();
    }
    return value;
}
}

BinaryWriteResult write_binary(const Module& module) {
    BinaryWriteResult result;
    result.diagnostics = verify_module(module);
    if (!result.diagnostics.empty()) return result;
    try {
        Writer payload;
        payload.string(module.name());
        write_vector(payload, module.structs(), [&](const StructDecl& structure) {
            payload.string(structure.name);
            payload.u8(structure.move_only ? 1U : 0U);
            write_vector(payload, structure.fields, [&](const StructField& field) {
                payload.string(field.name);
                payload.type(field.type);
                payload.u8(static_cast<std::uint8_t>(field.aggregate_kind));
                payload.string(field.aggregate_name);
            });
        });
        write_vector(payload, module.arrays(), [&](const ArrayDecl& array) {
            payload.string(array.name);
            payload.type(array.element_type);
            payload.u8(static_cast<std::uint8_t>(array.element_aggregate_kind));
            payload.string(array.element_aggregate_name);
            payload.u32(array.element_count);
            payload.u8(array.move_only ? 1U : 0U);
        });
        write_vector(payload, module.globals(), [&](const Global& global) {
            payload.string(global.name);
            payload.type(global.type);
            payload.u8(static_cast<std::uint8_t>(global.aggregate_kind));
            payload.string(global.aggregate_name);
            payload.string(global.function_signature_name);
            payload.u8(global.is_constant ? 1 : 0);
            payload.u8(global.is_external ? 1 : 0);
            payload.u8(global.is_thread_local ? 1 : 0);
            payload.u8(static_cast<std::uint8_t>(global.linkage));
            payload.u8(static_cast<std::uint8_t>(global.visibility));
            payload.string(global.initializer);
            payload.u32(global.element_count);
            payload.u32(global.alignment);
            payload.u8(global.zero_initialized ? 1 : 0);
            payload.string(std::string_view(reinterpret_cast<const char*>(global.bytes.data()), global.bytes.size()));
        });
        write_vector(payload, module.functions(), [&](const Function& function) {
            payload.string(function.name);
            payload.u8(function.is_external ? 1U : 0U);
            payload.u8(function.is_signature ? 1U : 0U);
            payload.u8(function.variadic ? 1U : 0U);
            payload.u8(static_cast<std::uint8_t>(function.calling_convention));
            payload.u8(static_cast<std::uint8_t>(function.linkage));
            payload.u8(static_cast<std::uint8_t>(function.visibility));
            payload.type(function.return_type);
            payload.u8(static_cast<std::uint8_t>(function.return_aggregate_kind));
            payload.string(function.return_aggregate_name);
            payload.u8(function.return_owned ? 1 : 0);
            payload.u8(static_cast<std::uint8_t>(function.return_borrow_mode));
            payload.u32(static_cast<std::uint32_t>(function.return_borrow_parameter));
            write_vector(payload, function.parameters, [&](const ValueDecl& value) { write_decl(payload, value); });
            write_vector(payload, function.blocks, [&](const Block& block) {
                payload.string(block.name);
                write_vector(payload, block.parameters, [&](const ValueDecl& value) { write_decl(payload, value); });
                write_vector(payload, block.operations, [&](const Operation& operation) {
                    payload.string(operation.result);
                    payload.string(operation.opcode);
                    payload.type(operation.type);
                    write_vector(payload, operation.operands, [&](const std::string& value) { payload.string(value); });
                    write_vector(payload, operation.successors, [&](const std::string& value) { payload.string(value); });
                    write_vector(payload, operation.successor_arguments, [&](const std::vector<std::string>& arguments) {
                        write_vector(payload, arguments, [&](const std::string& value) { payload.string(value); });
                    });
                    payload.u32(operation.alignment);
                });
            });
        });
        Writer file;
        for (const auto byte : magic) file.u8(std::to_integer<std::uint8_t>(byte));
        file.u16(binary_format_major);
        file.u16(binary_format_minor);
        file.u32(static_cast<std::uint32_t>(payload.bytes().size()));
        file.u32(checksum(payload.bytes()));
        file.bytes().insert(file.bytes().end(), payload.bytes().begin(), payload.bytes().end());
        result.bytes = std::move(file.bytes());
    } catch (const std::exception& error) {
        result.diagnostics.push_back({DiagnosticSeverity::error, error.what(), {}});
    }
    return result;
}

BinaryReadResult read_binary(std::span<const std::byte> bytes) {
    BinaryReadResult result{Module("invalid"), {}};
    try {
        Reader file(bytes);
        for (const auto expected : magic) if (file.u8() != std::to_integer<std::uint8_t>(expected)) throw std::runtime_error("invalid binary IR magic");
        const auto major = file.u16();
        const auto minor = file.u16();
        if (major != binary_format_major) throw std::runtime_error("unsupported binary IR major version");
        if (minor > binary_format_minor) throw std::runtime_error("binary IR requires a newer reader");
        const auto payload_size = file.u32();
        const auto expected_checksum = file.u32();
        constexpr std::size_t header_size = 16;
        if (bytes.size() < header_size || bytes.size() - header_size != payload_size) throw std::runtime_error("binary IR size mismatch");
        const auto payload_bytes = bytes.subspan(header_size, payload_size);
        if (checksum(payload_bytes) != expected_checksum) throw std::runtime_error("binary IR checksum mismatch");
        Reader payload(payload_bytes);
        Module module(payload.string());
        if (minor >= 6) {
            read_count(payload, [&] {
                StructDecl structure;
                structure.name = payload.string();
                if (minor >= 14) structure.move_only = payload.u8() != 0;
                read_count(payload, [&] {
                    StructField field;
                    field.name = payload.string();
                    field.type = payload.type();
                    if (minor >= 8) {
                        const auto kind = payload.u8();
                        if (kind > static_cast<std::uint8_t>(AggregateRefKind::array)) throw std::runtime_error("invalid aggregate field kind");
                        field.aggregate_kind = static_cast<AggregateRefKind>(kind);
                        field.aggregate_name = payload.string();
                    }
                    structure.fields.push_back(std::move(field));
                });
                module.structs().push_back(std::move(structure));
            });
        }
        if (minor >= 7) {
            read_count(payload, [&] {
                ArrayDecl array;
                array.name = payload.string();
                array.element_type = payload.type();
                if (minor >= 23) {
                    const auto kind = payload.u8();
                    if (kind > static_cast<std::uint8_t>(AggregateRefKind::array)) throw std::runtime_error("invalid array element aggregate kind");
                    array.element_aggregate_kind = static_cast<AggregateRefKind>(kind);
                    array.element_aggregate_name = payload.string();
                }
                array.element_count = payload.u32();
                if (minor >= 14) array.move_only = payload.u8() != 0;
                module.arrays().push_back(std::move(array));
            });
        }
        if (minor >= 2) {
            read_count(payload, [&] {
                Global global;
                global.name = payload.string();
                global.type = payload.type();
                if (minor >= 9) {
                    global.aggregate_kind = static_cast<AggregateRefKind>(payload.u8());
                    global.aggregate_name = payload.string();
                    if (minor >= 20) global.function_signature_name = payload.string();
                }
                global.is_constant = payload.u8() != 0;
                global.is_external = minor >= 4 ? payload.u8() != 0 : false;
                if (minor >= 24) global.is_thread_local = payload.u8() != 0;
                if (minor >= 22) {
                    const auto linkage = payload.u8();
                    const auto visibility = payload.u8();
                    if (linkage > static_cast<std::uint8_t>(SymbolLinkage::weak)) throw std::runtime_error("invalid global linkage");
                    if (visibility > static_cast<std::uint8_t>(SymbolVisibility::hidden)) throw std::runtime_error("invalid global visibility");
                    global.linkage = static_cast<SymbolLinkage>(linkage);
                    global.visibility = static_cast<SymbolVisibility>(visibility);
                }
                global.initializer = payload.string();
                if (minor >= 5) {
                    global.element_count = payload.u32();
                    global.alignment = payload.u32();
                    global.zero_initialized = payload.u8() != 0;
                    const auto bytes = payload.string();
                    global.bytes.assign(bytes.begin(), bytes.end());
                }
                module.globals().push_back(std::move(global));
            });
        }
        read_count(payload, [&] {
            Function function;
            function.name = payload.string();
            if (minor >= 18) {
                function.is_external = payload.u8() != 0;
                function.is_signature = payload.u8() != 0;
            }
            if (minor >= 22) {
                function.variadic = payload.u8() != 0;
                const auto convention = payload.u8();
                const auto linkage = payload.u8();
                const auto visibility = payload.u8();
                if (convention > static_cast<std::uint8_t>(CallingConvention::fast)) throw std::runtime_error("invalid calling convention");
                if (linkage > static_cast<std::uint8_t>(SymbolLinkage::weak)) throw std::runtime_error("invalid function linkage");
                if (visibility > static_cast<std::uint8_t>(SymbolVisibility::hidden)) throw std::runtime_error("invalid function visibility");
                function.calling_convention = static_cast<CallingConvention>(convention);
                function.linkage = static_cast<SymbolLinkage>(linkage);
                function.visibility = static_cast<SymbolVisibility>(visibility);
            }
            function.return_type = payload.type();
            if (minor >= 11) {
                const auto kind = payload.u8();
                if (kind > static_cast<std::uint8_t>(AggregateRefKind::array)) throw std::runtime_error("invalid aggregate return kind");
                function.return_aggregate_kind = static_cast<AggregateRefKind>(kind);
                function.return_aggregate_name = payload.string();
                if (minor >= 12) function.return_owned = payload.u8() != 0;
                if (minor >= 17) {
                    const auto mode = payload.u8();
                    if (mode > static_cast<std::uint8_t>(BorrowMode::mutable_)) throw std::runtime_error("invalid return borrow mode");
                    function.return_borrow_mode = static_cast<BorrowMode>(mode);
                    function.return_borrow_parameter = static_cast<std::int32_t>(payload.u32());
                }
            }
            read_count(payload, [&] { function.parameters.push_back(read_decl(payload, minor)); });
            read_count(payload, [&] {
                Block block;
                block.name = payload.string();
                read_count(payload, [&] { block.parameters.push_back(read_decl(payload, minor)); });
                read_count(payload, [&] {
                    Operation operation;
                    operation.result = payload.string();
                    operation.opcode = payload.string();
                    operation.type = payload.type();
                    read_count(payload, [&] { operation.operands.push_back(payload.string()); });
                    read_count(payload, [&] { operation.successors.push_back(payload.string()); });
                    read_count(payload, [&] {
                        std::vector<std::string> arguments;
                        read_count(payload, [&] { arguments.push_back(payload.string()); });
                        operation.successor_arguments.push_back(std::move(arguments));
                    });
                    if (minor >= 3) operation.alignment = payload.u32();
                    block.operations.push_back(std::move(operation));
                });
                function.blocks.push_back(std::move(block));
            });
            if (minor < 18 && function.blocks.empty()) function.is_external = true;
            module.functions().push_back(std::move(function));
        });
        if (!payload.ended()) throw std::runtime_error("trailing data in binary IR payload");
        result.module = std::move(module);
        result.diagnostics = verify_module(result.module);
    } catch (const std::exception& error) {
        result.diagnostics.push_back({DiagnosticSeverity::error, error.what(), {}});
    }
    return result;
}
}
