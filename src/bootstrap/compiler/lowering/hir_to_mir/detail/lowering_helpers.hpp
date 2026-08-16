// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

namespace {

std::optional<std::uint32_t> decode_character_literal(std::string_view text) {
  if (text.size() < 3 || text.front() != '\'' || text.back() != '\'') return std::nullopt;
  if (text[1] != '\\') {
    if (text.size() != 3) return std::nullopt;
    return static_cast<std::uint32_t>(static_cast<unsigned char>(text[1]));
  }

  if (text.size() != 4) return std::nullopt;
  switch (text[2]) {
    case 'n': return static_cast<std::uint32_t>('\n');
    case 'r': return static_cast<std::uint32_t>('\r');
    case 't': return static_cast<std::uint32_t>('\t');
    case '0': return 0u;
    case '\\': return static_cast<std::uint32_t>('\\');
    case '\'': return static_cast<std::uint32_t>('\'');
    case '"': return static_cast<std::uint32_t>('"');
    default: return static_cast<std::uint32_t>(static_cast<unsigned char>(text[2]));
  }
}

std::optional<std::pair<std::string, std::string>> primitive_integer_constant(std::string_view name) {
  const auto separator = name.find("::");
  if (separator == std::string_view::npos || name.find("::", separator + 2) != std::string_view::npos) return std::nullopt;
  const auto type = name.substr(0, separator);
  const auto member = name.substr(separator + 2);
  if (member != "MIN" && member != "MAX") return std::nullopt;
  const bool minimum = member == "MIN";
  if (type == "i8") return std::pair{std::string("i8"), std::string(minimum ? "-128" : "127")};
  if (type == "i16") return std::pair{std::string("i16"), std::string(minimum ? "-32768" : "32767")};
  if (type == "i32") return std::pair{std::string("i32"), std::string(minimum ? "-2147483648" : "2147483647")};
  if (type == "i64" || type == "int" || type == "isize")
    return std::pair{std::string(type), std::string(minimum ? "-9223372036854775808" : "9223372036854775807")};
  if (type == "u8" || type == "byte") return std::pair{std::string(type), std::string(minimum ? "0" : "255")};
  if (type == "u16") return std::pair{std::string("u16"), std::string(minimum ? "0" : "65535")};
  if (type == "u32") return std::pair{std::string("u32"), std::string(minimum ? "0" : "4294967295")};
  if (type == "u64" || type == "uint" || type == "usize") {
    if (minimum) return std::pair{std::string(type), std::string("0")};
    return std::nullopt;
  }
  return std::nullopt;
}

std::uint64_t stable_dispatch_id(std::string_view text) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const unsigned char byte : text) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  hash &= 0x7fffffffffffffffull;
  return hash == 0 ? 1 : hash;
}

std::uint64_t align_up(std::uint64_t value, std::uint32_t alignment) {
  const auto mask = static_cast<std::uint64_t>(std::max<std::uint32_t>(1, alignment) - 1);
  return (value + mask) & ~mask;
}

std::pair<std::string, std::vector<std::string>> split_generic_name(const std::string& name) {
  const auto open = name.find('<');
  if (open == std::string::npos || name.empty() || name.back() != '>') return {name, {}};
  std::vector<std::string> arguments;
  std::string current;
  int depth = 0;
  for (std::size_t index = open + 1; index + 1 < name.size(); ++index) {
    const char value = name[index];
    if (value == '<' || value == '[' || value == '(') ++depth;
    else if (value == '>' || value == ']' || value == ')') --depth;
    if (value == ',' && depth == 0) { arguments.push_back(current); current.clear(); }
    else current += value;
  }

  if (!current.empty()) arguments.push_back(current);
  return {name.substr(0, open), arguments};
}

std::vector<std::string> decode_constant_aggregate(const std::string& encoded) {
  std::vector<std::string> values;
  std::size_t cursor = 0;
  while (cursor < encoded.size()) {
    const auto colon = encoded.find(':', cursor);
    if (colon == std::string::npos) return {};
    std::size_t length = 0;
    try {
      length = static_cast<std::size_t>(std::stoull(encoded.substr(cursor, colon - cursor)));
    } catch (...) {
      return {};
    }
    const auto begin = colon + 1;
    if (length > encoded.size() - begin) return {};
    values.push_back(encoded.substr(begin, length));
    cursor = begin + length;
  }
  return values;
}

struct FunctionSignature final {
  std::string return_type;
  std::vector<std::string> parameter_types;
};

struct EnumVariantLayout final {
  std::int64_t discriminant = 0;
  std::vector<std::string> payload_types;
  std::vector<std::uint64_t> payload_offsets;
};

struct EnumLayout final {
  std::uint64_t size = 4;
  std::uint32_t alignment = 4;
  std::uint64_t payload_offset = 4;
  bool tagged = false;
  std::unordered_map<std::string, EnumVariantLayout> variants;
};

struct DynamicMethodLayout final {
  std::uint32_t slot = 0;
  std::string return_type;
  std::vector<std::string> parameter_types;
};

struct AggregateLayout final {
  std::uint64_t size = 0;
  std::uint32_t alignment = 1;
  std::unordered_map<std::string, HirField> fields;
  std::vector<HirField> ordered_fields;
};

class FunctionLowerer final {
 public:
  FunctionLowerer(DiagnosticEngine& diagnostics, const HirFunction& hir, MirFunction& mir,
                  const std::unordered_map<std::string, FunctionSignature>& signatures,
                  const std::unordered_map<std::string, AggregateLayout>& layouts,
                  const std::unordered_map<std::string, EnumLayout>& enums,
                  const std::unordered_map<std::string, std::string>& drop_functions,
                  const std::unordered_map<std::string, std::string>& clone_functions,
                  const std::unordered_map<std::string, std::string>& trait_methods,
                  const std::unordered_map<std::string, DynamicMethodLayout>& dynamic_methods,
                  const std::unordered_map<std::string, std::vector<std::string>>& dynamic_vtables,
                  const std::unordered_map<std::string, std::string>& associated_types,
                  const std::unordered_set<std::string>& copy_types,
                  const std::unordered_map<std::string, std::pair<std::string, std::string>>& constants)
      : diagnostics_(diagnostics), hir_(hir), mir_(mir), signatures_(signatures), layouts_(layouts), enums_(enums),
        drop_functions_(drop_functions), clone_functions_(clone_functions), trait_methods_(trait_methods),
        dynamic_methods_(dynamic_methods), dynamic_vtables_(dynamic_vtables),
        associated_types_(associated_types), copy_types_(copy_types), constants_(constants) {
    current_block_ = create_block("entry");
    for (const auto& parameter : hir.parameters) {
      types_.emplace(parameter.name, parameter.type_name);
      if (layouts_.contains(parameter.type_name) || is_tagged_enum(parameter.type_name)) {
        places_.emplace(parameter.name, parameter.name);
        emit_place_path(parameter.name, parameter.name, parameter.type_name, hir.range);
      } else {
        const auto slot = temporary();
        emit({MirOpcode::stack_allocate, slot, parameter.type_name,
              {std::to_string(type_size(parameter.type_name)), std::to_string(type_alignment(parameter.type_name))}, hir.range});
        emit({MirOpcode::store, {}, parameter.type_name, {parameter.name, slot}, hir.range});
        places_.emplace(parameter.name, slot);
        emit_place_path(slot, parameter.name, parameter.type_name, hir.range);
        if (parse_reference_type(parameter.type_name)) {
          emit({MirOpcode::borrow_bind, {}, parameter.type_name,
                {slot, parameter.name, "@caller." + parameter.name, parameter.name}, hir.range});
        }
      }
    }
  }

  void lower() {
    if (hir_.body.has_value()) lower_statement(*hir_.body);
    if (!terminated()) {
      if (hir_.return_type == "void") emit({MirOpcode::return_void, {}, {}, {}, hir_.range});
      else diagnostics_.error("D3001", hir_.range, "function '" + hir_.name + "' may exit without returning a value");
    }
    collect_async_projection_flags();
  }

 private:
  void collect_async_projection_flags() {
    if (!hir_.is_async) return;
    for (const auto& [name, place] : places_) {
      const auto type = types_.find(name);
      if (type == types_.end() || !needs_drop(type->second)) continue;
      std::uint32_t projection = 0;
      std::function<void(const std::string&, const std::string&)> collect;
      collect = [&](const std::string& current_type, const std::string& path) {
        if (!needs_drop(current_type)) return;
        if (parse_callable_type(current_type) || parse_dynamic_trait_type(current_type)) {
          if (const auto flag = drop_flags_.find(path); flag != drop_flags_.end())
            mir_.async_projection_flags.push_back({place, projection, flag->second});
          ++projection;
          return;
        }
        if (const auto layout = layouts_.find(current_type); layout != layouts_.end()) {
          bool any = false;
          for (const auto& field : layout->second.ordered_fields) {
            if (!needs_drop(field.type_name)) continue;
            any = true;
            collect(field.type_name, path + "." + field.name);
          }
          if (!any) {
            if (const auto flag = drop_flags_.find(path); flag != drop_flags_.end())
              mir_.async_projection_flags.push_back({place, projection, flag->second});
            ++projection;
          }
          return;
        }
        if (const auto array = parse_fixed_array_type(current_type)) {
          if (needs_drop(array->element_type)) {
            for (std::uint64_t index = 0; index < array->length; ++index)
              collect(array->element_type, path + "[" + std::to_string(index) + "]");
          }
          return;
        }
        if (const auto enumeration = enums_.find(current_type);
            enumeration != enums_.end() && enumeration->second.tagged) {
          std::vector<std::pair<std::string, const EnumVariantLayout*>> variants;
          for (const auto& [variant_name, variant] : enumeration->second.variants)
            variants.push_back({variant_name, &variant});
          std::sort(variants.begin(), variants.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.second->discriminant < rhs.second->discriminant;
          });
          bool any = false;
          for (const auto& [variant_name, variant] : variants) {
            for (std::size_t index = 0; index < variant->payload_types.size(); ++index) {
              if (!needs_drop(variant->payload_types[index])) continue;
              any = true;
              collect(variant->payload_types[index], enum_payload_path(path, variant_name, index));
            }
          }
          if (!any) {
            if (const auto flag = drop_flags_.find(path); flag != drop_flags_.end())
              mir_.async_projection_flags.push_back({place, projection, flag->second});
            ++projection;
          }
          return;
        }
        if (const auto flag = drop_flags_.find(path); flag != drop_flags_.end())
          mir_.async_projection_flags.push_back({place, projection, flag->second});
        ++projection;
      };
      collect(type->second, name);
    }
  }

  std::uint64_t type_size(const std::string& type) const {
    if (const auto array = parse_fixed_array_type(type)) return type_size(array->element_type) * array->length;
    if (const auto enumeration = enums_.find(type); enumeration != enums_.end()) return enumeration->second.size;
    const auto layout = layouts_.find(type);
    if (layout != layouts_.end()) return std::max<std::uint64_t>(1, layout->second.size);
    if (type == "bool" || type == "i8" || type == "u8" || type == "byte") return 1;
    if (type == "i16" || type == "u16") return 2;
    if (type == "i32" || type == "u32" || type == "f32" || type == "char") return 4;
    return 8;
  }

  std::uint32_t type_alignment(const std::string& type) const {
    if (const auto array = parse_fixed_array_type(type)) return type_alignment(array->element_type);
    if (const auto enumeration = enums_.find(type); enumeration != enums_.end()) return enumeration->second.alignment;
    const auto layout = layouts_.find(type);
    return layout == layouts_.end() ? static_cast<std::uint32_t>(type_size(type)) : layout->second.alignment;
  }

  bool is_tagged_enum(const std::string& type) const {
    const auto found = enums_.find(type);
    return found != enums_.end() && found->second.tagged;
  }

  bool is_aggregate(const std::string& type) const {
    if (parse_reference_type(type) || parse_raw_pointer_type(type) || parse_dynamic_trait_type(type)) return false;
    return layouts_.contains(type) || parse_fixed_array_type(type).has_value() || is_tagged_enum(type);
  }

  std::string materialize_constant_value(const std::string& type, const std::string& encoded, SourceRange range) {
    if (!is_aggregate(type)) {
      const auto value = temporary();
      emit({MirOpcode::constant, value, type, {encoded}, range});
      return value;
    }

    const auto storage = temporary();
    emit({MirOpcode::stack_allocate, storage, type,
          {std::to_string(type_size(type)), std::to_string(type_alignment(type))}, range});
    const auto values = decode_constant_aggregate(encoded);

    if (const auto array = parse_fixed_array_type(type)) {
      if (values.size() != array->length) {
        diagnostics_.error("D3011", range, "malformed aggregate constant for '" + type + "'");
        return storage;
      }
      for (std::size_t index = 0; index < values.size(); ++index) {
        const auto element_place = temporary();
        emit({MirOpcode::pointer_offset, element_place, array->element_type,
              {storage, std::to_string(index * type_size(array->element_type))}, range});
        const auto element = materialize_constant_value(array->element_type, values[index], range);
        if (is_aggregate(array->element_type)) copy_aggregate(array->element_type, element, element_place, range);
        else emit({MirOpcode::store, {}, array->element_type, {element, element_place}, range});
      }
      return storage;
    }

    if (const auto enumeration = enums_.find(type); enumeration != enums_.end()) {
      if (values.size() < 2) {
        diagnostics_.error("D3011", range, "malformed enum constant for '" + type + "'");
        return storage;
      }
      const auto variant = enumeration->second.variants.find(values[1]);
      if (variant == enumeration->second.variants.end() || values.size() != variant->second.payload_types.size() + 2) {
        diagnostics_.error("D3011", range, "malformed enum constant for '" + type + "'");
        return storage;
      }
      const auto tag_place = temporary();
      emit({MirOpcode::pointer_offset, tag_place, "i32", {storage, "0"}, range});
      const auto tag = temporary();
      emit({MirOpcode::constant, tag, "i32", {values[0]}, range});
      emit({MirOpcode::store, {}, "i32", {tag, tag_place}, range});
      for (std::size_t index = 0; index < variant->second.payload_types.size(); ++index) {
        const auto& payload_type = variant->second.payload_types[index];
        const auto payload_place = temporary();
        emit({MirOpcode::pointer_offset, payload_place, payload_type,
              {storage, std::to_string(enumeration->second.payload_offset + variant->second.payload_offsets[index])}, range});
        const auto payload = materialize_constant_value(payload_type, values[index + 2], range);
        if (is_aggregate(payload_type)) copy_aggregate(payload_type, payload, payload_place, range);
        else emit({MirOpcode::store, {}, payload_type, {payload, payload_place}, range});
      }
      return storage;
    }

    const auto layout = layouts_.find(type);
    if (layout == layouts_.end() || values.size() != layout->second.ordered_fields.size()) {
      diagnostics_.error("D3011", range, "malformed aggregate constant for '" + type + "'");
      return storage;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
      const auto& field = layout->second.ordered_fields[index];
      const auto field_place = temporary();
      emit({MirOpcode::pointer_offset, field_place, field.type_name,
            {storage, std::to_string(field.offset)}, range});
      const auto field_value = materialize_constant_value(field.type_name, values[index], range);
      if (is_aggregate(field.type_name)) copy_aggregate(field.type_name, field_value, field_place, range);
      else emit({MirOpcode::store, {}, field.type_name, {field_value, field_place}, range});
    }
    return storage;
  }

  std::size_t create_block(std::string prefix) {
    auto name = std::move(prefix);
    if (name != "entry") name += std::to_string(next_block_++);
    mir_.blocks.push_back({std::move(name), {}});
    return mir_.blocks.size() - 1;
  }

  MirBlock& block() { return mir_.blocks[current_block_]; }
  const MirBlock& block() const { return mir_.blocks[current_block_]; }
  bool terminated() const { return !block().instructions.empty() && mir_is_terminator(block().instructions.back().opcode); }
  std::string temporary() { return "t" + std::to_string(next_value_++); }

  void emit(MirInstruction instruction) {
    const auto opcode = instruction.opcode;
    const auto result = instruction.result;
    const auto type_name = instruction.type_name;
    const auto operands = instruction.operands;
    const auto range = instruction.range;
    block().instructions.push_back(std::move(instruction));
    if (opcode == MirOpcode::stack_allocate && !result.empty()) {
      block().instructions.push_back({MirOpcode::storage_live, {}, type_name, {result}, range});
    } else if (opcode == MirOpcode::drop && !operands.empty()) {
      block().instructions.push_back({MirOpcode::storage_dead, {}, type_name, {operands.front()}, range});
    }
  }

  void emit_place_path(const std::string& place, const std::string& logical_path,
                       const std::string& type_name, SourceRange range) {
    if (place.empty() || logical_path.empty()) return;
    emit({MirOpcode::place_path, {}, type_name, {place, logical_path}, range});
  }

  void emit_move_metadata(const std::string& logical_path, const std::string& type_name, SourceRange range) {
    if (logical_path.empty()) return;
    const auto boundary = logical_path.find_first_of(".[");
    const auto root_name = logical_path.substr(0, boundary);
    const auto place = places_.find(root_name);
    if (place == places_.end()) return;
    emit({MirOpcode::move_value, {}, type_name, {place->second, logical_path}, range});
  }

  std::string block_name(std::size_t index) const { return mir_.blocks[index].name; }

  void emit_deferred_statement(const SyntaxNode& statement) {
    // A defer executed from another defer owns a fresh lexical defer scope.
    // Nested defers therefore run when the enclosing deferred statement exits,
    // in the same LIFO order as ordinary lexical scopes.
    defer_scopes_.emplace_back();
    lower_statement(statement);
    auto nested = std::move(defer_scopes_.back());
    defer_scopes_.pop_back();
    for (auto iterator = nested.rbegin(); iterator != nested.rend(); ++iterator) {
      if (terminated()) break;
      emit_deferred_statement(**iterator);
    }
  }

  void emit_deferred_from(std::size_t minimum_depth) {
    if (emitting_defer_) return;
    emitting_defer_ = true;
    for (std::size_t scope_index = defer_scopes_.size(); scope_index > minimum_depth; --scope_index) {
      // Copy the list because executing a deferred statement may register
      // nested defers in a fresh scope.
      const auto deferred = defer_scopes_[scope_index - 1];
      for (auto iterator = deferred.rbegin(); iterator != deferred.rend(); ++iterator) {
        if (terminated()) break;
        emit_deferred_statement(**iterator);
      }
      if (terminated()) break;
    }
    emitting_defer_ = false;
  }

  struct DropLocal final {
    std::string name;
    std::string type;
    std::string place;
    SourceRange range{};
  };

  void write_drop_flag(const std::string& flag_place, bool value, SourceRange range) {
    const auto constant = temporary();
    emit({MirOpcode::constant, constant, "bool", {value ? "1" : "0"}, range});
    emit({MirOpcode::store, {}, "bool", {constant, flag_place}, range});
  }

  static std::string enum_payload_path(const std::string& logical_path,
                                       const std::string& variant_name,
                                       std::size_t payload_index) {
    if (logical_path.empty()) return {};
    return logical_path + ".variant$" + variant_name + "[" +
           std::to_string(payload_index) + "]";
  }

  void register_drop_flags(const std::string& type, const std::string& logical_path,
                           SourceRange range, bool initialized) {
    if (logical_path.empty() || !needs_drop(type) || parse_callable_type(type) ||
        parse_dynamic_trait_type(type) || drop_flags_.contains(logical_path)) return;
    const auto flag = temporary();
    emit({MirOpcode::stack_allocate, flag, "bool", {"1", "1"}, range});
    write_drop_flag(flag, initialized, range);
    drop_flags_[logical_path] = flag;
    if (const auto layout = layouts_.find(type); layout != layouts_.end()) {
      for (const auto& field : layout->second.ordered_fields) {
        if (!needs_drop(field.type_name)) continue;
        register_drop_flags(field.type_name, logical_path + "." + field.name, range, initialized);
      }
    } else if (const auto array = parse_fixed_array_type(type)) {
      if (needs_drop(array->element_type)) {
        for (std::uint64_t index = 0; index < array->length; ++index) {
          register_drop_flags(array->element_type,
                              logical_path + "[" + std::to_string(index) + "]", range, initialized);
        }
      }
    } else if (const auto enumeration = enums_.find(type);
               enumeration != enums_.end() && enumeration->second.tagged) {
      for (const auto& [variant_name, variant] : enumeration->second.variants) {
        for (std::size_t index = 0; index < variant.payload_types.size(); ++index) {
          if (!needs_drop(variant.payload_types[index])) continue;
          register_drop_flags(variant.payload_types[index],
                              enum_payload_path(logical_path, variant_name, index),
                              range, initialized);
        }
      }
    }
  }

  void set_drop_flags(const std::string& logical_path, bool value, SourceRange range) {
    if (logical_path.empty()) return;
    for (const auto& [path, flag] : drop_flags_) {
      if (path == logical_path || path.starts_with(logical_path + ".") ||
          path.starts_with(logical_path + "[")) {
        write_drop_flag(flag, value, range);
      }
    }
  }

  void erase_drop_flags(const std::string& logical_path) {
    if (logical_path.empty()) return;
    for (auto iterator = drop_flags_.begin(); iterator != drop_flags_.end();) {
      const auto& path = iterator->first;
      if (path == logical_path || path.starts_with(logical_path + ".") ||
          path.starts_with(logical_path + "[")) {
        iterator = drop_flags_.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }

  void mark_runtime_moved(const std::string& logical_path, SourceRange range) {
    if (logical_path.empty()) return;
    const auto boundary = logical_path.find_first_of(".[");
    const auto root_name = logical_path.substr(0, boundary);
    const auto type = types_.find(root_name);
    emit_move_metadata(logical_path, type == types_.end() ? std::string{} : type->second, range);
    moved_locals_.insert(logical_path);
    set_drop_flags(logical_path, false, range);
  }

  void emit_drop_value(const std::string& type, const std::string& place, SourceRange range,
                       const std::string& logical_path = {}, bool bypass_flag = false) {
    if (!bypass_flag) {
      if (const auto flag = drop_flags_.find(logical_path); flag != drop_flags_.end()) {
        const auto active = temporary();
        emit({MirOpcode::load, active, "bool", {flag->second}, range});
        const auto drop_block = create_block("drop.flag.active");
        const auto skip_block = create_block("drop.flag.skip");
        const auto merge_block = create_block("drop.flag.end");
        emit({MirOpcode::branch, {}, "bool",
              {active, block_name(drop_block), block_name(skip_block)}, range});
        current_block_ = drop_block;
        emit_drop_value(type, place, range, logical_path, true);
        if (!terminated()) emit({MirOpcode::jump, {}, {}, {block_name(merge_block)}, range});
        current_block_ = skip_block;
        emit({MirOpcode::jump, {}, {}, {block_name(merge_block)}, range});
        current_block_ = merge_block;
        return;
      }
      if (moved_covers(logical_path)) return;
    }
    if (parse_callable_type(type)) {
      const auto handle = temporary();
      emit({MirOpcode::load, handle, type, {place}, range});
      emit({MirOpcode::callable_destroy, {}, type, {handle}, range});
      emit({MirOpcode::drop, {}, type, {place}, range});
      return;
    }
    if (parse_dynamic_trait_type(type)) {
      const auto handle = temporary();
      emit({MirOpcode::load, handle, type, {place}, range});
      emit({MirOpcode::trait_object_destroy, {}, type, {handle}, range});
      emit({MirOpcode::drop, {}, type, {place}, range});
      return;
    }
    if (const auto destructor = drop_functions_.find(type); destructor != drop_functions_.end()) {
      emit({MirOpcode::call, {}, "void", {destructor->second, place}, range});
    }
    // Fields are destroyed after the user Drop body, in reverse declaration order.
    if (const auto layout = layouts_.find(type); layout != layouts_.end()) {
      for (auto field = layout->second.ordered_fields.rbegin(); field != layout->second.ordered_fields.rend(); ++field) {
        if (!needs_drop(field->type_name)) continue;
        const auto field_place = temporary();
        emit({MirOpcode::pointer_offset, field_place, field->type_name, {place, std::to_string(field->offset)}, range});
        const auto field_path = logical_path.empty() ? std::string{} : logical_path + "." + field->name;
        emit_place_path(field_place, field_path, field->type_name, range);
        emit_drop_value(field->type_name, field_place, range, field_path);
      }
    } else if (const auto array = parse_fixed_array_type(type)) {
      if (needs_drop(array->element_type)) {
        for (std::uint64_t index = array->length; index > 0; --index) {
          const auto element_place = temporary();
          emit({MirOpcode::pointer_offset, element_place, array->element_type,
                {place, std::to_string((index - 1) * type_size(array->element_type))}, range});
          const auto element_path = logical_path.empty() ? std::string{} :
              logical_path + "[" + std::to_string(index - 1) + "]";
          emit_place_path(element_place, element_path, array->element_type, range);
          emit_drop_value(array->element_type, element_place, range, element_path);
        }
      }
    } else if (const auto enumeration = enums_.find(type);
               enumeration != enums_.end() && enumeration->second.tagged) {
      std::vector<const EnumVariantLayout*> active_variants;
      active_variants.reserve(enumeration->second.variants.size());
      for (const auto& [_, variant] : enumeration->second.variants) {
        if (std::any_of(variant.payload_types.begin(), variant.payload_types.end(),
                        [&](const std::string& payload_type) { return needs_drop(payload_type); })) {
          active_variants.push_back(&variant);
        }
      }
      std::sort(active_variants.begin(), active_variants.end(),
                [](const EnumVariantLayout* left, const EnumVariantLayout* right) {
                  return left->discriminant < right->discriminant;
                });
      if (!active_variants.empty()) {
        const auto tag_place = temporary();
        emit({MirOpcode::pointer_offset, tag_place, "i32", {place, "0"}, range});
        if (!logical_path.empty()) emit_place_path(tag_place, logical_path + ".$tag", "i32", range);
        const auto tag = temporary();
        emit({MirOpcode::load, tag, "i32", {tag_place}, range});
        const auto merge_block = create_block("drop.enum.end");
        const auto merge_name = block_name(merge_block);
        for (std::size_t variant_index = 0; variant_index < active_variants.size(); ++variant_index) {
          const auto* variant = active_variants[variant_index];
          const auto variant_entry = std::find_if(
              enumeration->second.variants.begin(), enumeration->second.variants.end(),
              [&](const auto& entry) { return &entry.second == variant; });
          const auto variant_name = variant_entry == enumeration->second.variants.end()
              ? std::string{} : variant_entry->first;
          const auto payload_block = create_block("drop.enum.payload");
          const auto next_block = variant_index + 1 == active_variants.size()
              ? merge_block : create_block("drop.enum.next");
          const auto expected = temporary();
          emit({MirOpcode::constant, expected, "i32", {std::to_string(variant->discriminant)}, range});
          const auto matches = temporary();
          emit({MirOpcode::equal, matches, "i32", {tag, expected}, range});
          emit({MirOpcode::branch, {}, "bool",
                {matches, block_name(payload_block), block_name(next_block)}, range});

          current_block_ = payload_block;
          for (std::size_t payload_index = variant->payload_types.size(); payload_index > 0; --payload_index) {
            const auto index = payload_index - 1;
            const auto& payload_type = variant->payload_types[index];
            if (!needs_drop(payload_type)) continue;
            const auto payload_place = temporary();
            emit({MirOpcode::pointer_offset, payload_place, payload_type,
                  {place, std::to_string(enumeration->second.payload_offset + variant->payload_offsets[index])}, range});
            const auto payload_path = enum_payload_path(logical_path, variant_name, index);
            emit_place_path(payload_place, payload_path, payload_type, range);
            emit_drop_value(payload_type, payload_place, range, payload_path);
          }
          if (!terminated()) emit({MirOpcode::jump, {}, {}, {merge_name}, range});
          current_block_ = next_block;
        }
        current_block_ = merge_block;
      }
    }
    emit({MirOpcode::drop, {}, type, {place}, range});
  }

  void emit_drops_from(std::size_t minimum_depth) {
    for (std::size_t scope_index = drop_scopes_.size(); scope_index > minimum_depth; --scope_index) {
      const auto& locals = drop_scopes_[scope_index - 1];
      for (auto iterator = locals.rbegin(); iterator != locals.rend(); ++iterator) {
        if (moved_locals_.contains(iterator->name) && !drop_flags_.contains(iterator->name)) continue;
        emit_drop_value(iterator->type, iterator->place, iterator->range, iterator->name);
      }
    }
  }

  bool needs_drop(const std::string& type) const {
    if (parse_callable_type(type) || parse_dynamic_trait_type(type)) return true;
    if (copy_types_.contains(type) || parse_slice_type(type)) return false;
    if (drop_functions_.contains(type)) return true;
    if (const auto array = parse_fixed_array_type(type)) return needs_drop(array->element_type);
    if (layouts_.contains(type)) return true;
    return is_tagged_enum(type) || type == "string";
  }

  bool implicit_numeric_family(const std::string& source, const std::string& target) const {
    const auto source_kind = builtin_type(source).kind;
    const auto target_kind = builtin_type(target).kind;
    const bool source_integer = source_kind == TypeKind::signed_integer || source_kind == TypeKind::unsigned_integer;
    const bool target_integer = target_kind == TypeKind::signed_integer || target_kind == TypeKind::unsigned_integer;
    if (source_integer && target_integer) return true;
    return source_kind == TypeKind::floating_point && target_kind == TypeKind::floating_point;
  }

  std::string coerce_implicit_numeric(const std::string& value, const std::string& source,
                                      const std::string& target, SourceRange range) {
    if (value.empty() || source.empty() || target.empty() || source == target ||
        !implicit_numeric_family(source, target)) return value;
    const auto converted = temporary();
    emit({MirOpcode::numeric_cast, converted, target, {value, source}, range});
    return converted;
  }

  std::string preserve_return_value(const std::string& type, const std::string& value, SourceRange range) {
    if (!is_aggregate(type)) return value;
    const auto storage = temporary();
    emit({MirOpcode::stack_allocate, storage, type,
          {std::to_string(type_size(type)), std::to_string(type_alignment(type))}, range});
    copy_aggregate(type, value, storage, range);
    return storage;
  }

  std::string expression_type(const SyntaxNode& node) const {
    if (node.kind == SyntaxKind::struct_expression) return node.label;
    if (node.kind == SyntaxKind::closure_expression) {
      const auto arrow = node.label.find(" -> ");
      return arrow == std::string::npos ? "void" : node.label.substr(arrow + 4);
    }
    if (node.kind == SyntaxKind::tuple_expression) {
      std::string type_name = "(";
      for (std::size_t index = 0; index < node.children.size(); ++index) {
        if (index != 0) type_name += ",";
        type_name += expression_type(node.children[index]);
      }
      type_name += ")";
      return type_name;
    }
    if (node.kind == SyntaxKind::array_expression) {
      const auto element_type = node.children.empty() ? std::string("i64") : expression_type(node.children.front());
      return element_type + "[" + std::to_string(node.children.size()) + "]";
    }
    if (node.kind == SyntaxKind::literal_expression) {
      if (!node.label.empty() && node.label.front() == '"') return "string";
      if (!node.label.empty() && node.label.front() == '\'') return "char";
      return node.label.find_first_of(".eE") == std::string::npos ? "i64" : "f64";
    }
    if (node.kind == SyntaxKind::name_expression) {
      if (const auto intrinsic = primitive_integer_constant(node.label)) return intrinsic->first;
      if (node.label == "true" || node.label == "false") return "bool";
      const auto found = types_.find(node.label);
      if (found != types_.end()) return found->second;
      const auto function = signatures_.find(node.label);
      if (function != signatures_.end()) return function_type_name(function->second.parameter_types, function->second.return_type);
      return "i64";
    }
    if ((node.kind == SyntaxKind::parenthesized_expression || node.kind == SyntaxKind::unary_expression) &&
        !node.children.empty()) {
      if (node.kind == SyntaxKind::unary_expression && node.label == "!") return "bool";
      if (node.kind == SyntaxKind::unary_expression && (node.label == "&" || node.label == "&mut"))
        return expression_type(node.children.front()) + (node.label == "&mut" ? "&mut" : "&");
      if (node.kind == SyntaxKind::unary_expression && node.label == "*") {
        const auto reference = expression_type(node.children.front());
        if (const auto parsed = parse_reference_type(reference)) return parsed->referent_type;
        if (const auto pointer = parse_raw_pointer_type(reference)) return pointer->pointee_type;
      }
      return expression_type(node.children.front());
    }
    if (node.kind == SyntaxKind::try_expression && !node.children.empty()) {
      const auto operand_type = expression_type(node.children.front());
      const auto open = operand_type.find('<');
      const auto comma = operand_type.find(',', open == std::string::npos ? 0 : open + 1);
      const auto close = operand_type.rfind('>');
      if (open != std::string::npos && close != std::string::npos) {
        return operand_type.substr(open + 1, (comma == std::string::npos ? close : comma) - open - 1);
      }
      return "i64";
    }
    if (node.kind == SyntaxKind::cast_expression) return node.label;
    if (node.kind == SyntaxKind::binary_expression) {
      if (node.label == "==" || node.label == "!=" || node.label == "<" || node.label == "<=" ||
          node.label == ">" || node.label == ">=" || node.label == "&&" || node.label == "||") return "bool";
      return node.children.empty() ? "i64" : expression_type(node.children.front());
    }
    if (node.kind == SyntaxKind::assignment_expression && !node.children.empty()) {
      return expression_type(node.children.front());
    }
    if (node.kind == SyntaxKind::index_expression && !node.children.empty()) {
      const auto base_type = expression_type(node.children.front());
      if (const auto array = parse_fixed_array_type(base_type)) return array->element_type;
      if (const auto slice = parse_slice_type(base_type)) return slice->element_type;
    }
    if (node.kind == SyntaxKind::member_expression && !node.children.empty()) {
      if (node.modifier == "scoped" && node.children.front().kind == SyntaxKind::name_expression) {
        if (const auto intrinsic = primitive_integer_constant(node.children.front().label + "::" + node.label))
          return intrinsic->first;
      }
      if (node.children.front().kind == SyntaxKind::name_expression &&
          enums_.contains(node.children.front().label)) return node.children.front().label;
      auto base_type = expression_type(node.children.front());
      if (const auto reference = parse_reference_type(base_type)) base_type = reference->referent_type;
      const auto layout = layouts_.find(base_type);
      if (layout != layouts_.end()) {
        const auto field = layout->second.fields.find(node.label);
        if (field != layout->second.fields.end()) return field->second.type_name;
      }
    }
    if (node.kind == SyntaxKind::call_expression && !node.children.empty()) {
      if (node.children.front().kind == SyntaxKind::member_expression &&
          !node.children.front().children.empty()) {
        const auto& receiver_node = node.children.front().children.front();
        const bool type_qualified = receiver_node.kind == SyntaxKind::name_expression &&
                                    (layouts_.contains(receiver_node.label) || enums_.contains(receiver_node.label));
        auto receiver_type = type_qualified ? receiver_node.label : expression_type(receiver_node);
        if (const auto reference = parse_reference_type(receiver_type)) receiver_type = reference->referent_type;
        const auto method = trait_methods_.find(receiver_type + "::" + node.children.front().label);
        if (method != trait_methods_.end()) {
          const auto signature = signatures_.find(method->second);
          if (signature != signatures_.end()) return signature->second.return_type;
        }
      }
      if (node.children.front().kind == SyntaxKind::member_expression &&
          !node.children.front().children.empty() &&
          node.children.front().children.front().kind == SyntaxKind::name_expression &&
          enums_.contains(node.children.front().children.front().label)) {
        return node.children.front().children.front().label;
      }
      if (layouts_.contains(node.children.front().label)) return node.children.front().label;
      if (node.children.front().kind == SyntaxKind::name_expression) {
        const auto [builtin_base, builtin_arguments] = split_generic_name(node.children.front().label);
        if ((builtin_base == "size_of" || builtin_base == "align_of") && builtin_arguments.size() == 1) return "usize";
      }
      if (node.children.front().label == "clone" && node.children.size() == 2) return expression_type(node.children[1]);
      const auto found = signatures_.find(node.children.front().label);
      return found == signatures_.end() ? "i64" : found->second.return_type;
    }
    return "i64";
  }

  MirOpcode binary_opcode(const std::string& operation, SourceRange range) {
    if (operation == "+" || operation == "+=") return MirOpcode::add;
    if (operation == "-" || operation == "-=") return MirOpcode::subtract;
    if (operation == "*" || operation == "*=") return MirOpcode::multiply;
    if (operation == "/" || operation == "/=") return MirOpcode::divide;
    if (operation == "%" || operation == "%=") return MirOpcode::remainder;
    if (operation == "&" || operation == "&=") return MirOpcode::bit_and;
    if (operation == "|" || operation == "|=") return MirOpcode::bit_or;
    if (operation == "^" || operation == "^=") return MirOpcode::bit_xor;
    if (operation == "<<" || operation == "<<=") return MirOpcode::shift_left;
    if (operation == ">>" || operation == ">>=") return MirOpcode::shift_right;
    if (operation == "==") return MirOpcode::equal;
    if (operation == "!=") return MirOpcode::not_equal;
    if (operation == "<") return MirOpcode::less;
    if (operation == "<=") return MirOpcode::less_equal;
    if (operation == ">") return MirOpcode::greater;
    if (operation == ">=") return MirOpcode::greater_equal;
    diagnostics_.error("D3008", range, "operator '" + operation + "' is not lowered yet");
    return MirOpcode::copy;
  }

  static std::string logical_place_path(const SyntaxNode& node) {
    if (node.kind == SyntaxKind::name_expression) return node.label;
    if (node.kind == SyntaxKind::member_expression && !node.children.empty()) {
      const auto base = logical_place_path(node.children.front());
      return base.empty() ? std::string{} : base + "." + node.label;
    }
    if (node.kind == SyntaxKind::index_expression && node.children.size() >= 2) {
      const auto base = logical_place_path(node.children.front());
      if (base.empty()) return {};
      const auto& index = node.children[1];
      return base + "[" + (index.kind == SyntaxKind::literal_expression ? index.label : "*") + "]";
    }
    if (node.kind == SyntaxKind::unary_expression && !node.children.empty() &&
        (node.label == "&" || node.label == "&mut" || node.label == "*")) {
      return logical_place_path(node.children.front());
    }
    return {};
  }

  bool moved_covers(const std::string& path) const {
    if (path.empty()) return false;
    return std::any_of(moved_locals_.begin(), moved_locals_.end(), [&](const std::string& moved) {
      if (moved == path) return true;
      if (!path.starts_with(moved) || path.size() == moved.size()) return false;
      const auto boundary = path[moved.size()];
      return boundary == '.' || boundary == '[';
    });
  }

  void mark_reinitialized_path(const std::string& path, SourceRange range = {}) {
    if (path.empty()) return;
    for (auto iterator = moved_locals_.begin(); iterator != moved_locals_.end();) {
      if (*iterator == path || iterator->starts_with(path + ".") || iterator->starts_with(path + "["))
        iterator = moved_locals_.erase(iterator);
      else
        ++iterator;
    }
    set_drop_flags(path, true, range);
  }

  std::vector<std::string> closure_capture_names(const SyntaxNode& closure) const {
    std::unordered_set<std::string> explicit_parameters;
    for (const auto& child : closure.children) {
      if (child.kind != SyntaxKind::parameter) continue;
      const auto separator = child.label.find(' ');
      if (separator != std::string::npos) explicit_parameters.insert(child.label.substr(separator + 1));
    }
    std::vector<std::string> captures;
    std::unordered_set<std::string> seen;
    std::function<void(const SyntaxNode&)> walk = [&](const SyntaxNode& current) {
      if (current.kind == SyntaxKind::name_expression && types_.contains(current.label) &&
          !explicit_parameters.contains(current.label) && seen.insert(current.label).second) {
        captures.push_back(current.label);
      }
      for (const auto& child : current.children) walk(child);
    };
    walk(closure);
    return captures;
  }

  std::string lower_short_circuit(const SyntaxNode& node) {
    const auto result_slot = temporary();
    emit({MirOpcode::stack_allocate, result_slot, "bool", {"1", "1"}, node.range});
    const auto left = lower_expression(node.children[0]);
    const auto rhs_block = create_block(node.label == "&&" ? "logic.and.rhs" : "logic.or.rhs");
    const auto shortcut_block = create_block(node.label == "&&" ? "logic.and.false" : "logic.or.true");
    const auto merge_block = create_block("logic.end");
    const auto rhs_name = block_name(rhs_block);
    const auto shortcut_name = block_name(shortcut_block);
    const auto merge_name = block_name(merge_block);
    if (node.label == "&&") {
      emit({MirOpcode::branch, {}, "bool", {left, rhs_name, shortcut_name}, node.range});
    } else {
      emit({MirOpcode::branch, {}, "bool", {left, shortcut_name, rhs_name}, node.range});
    }

    current_block_ = rhs_block;
    const auto right = lower_expression(node.children[1]);
    emit({MirOpcode::store, {}, "bool", {right, result_slot}, node.range});
    emit({MirOpcode::jump, {}, {}, {merge_name}, node.range});

    current_block_ = shortcut_block;
    const auto shortcut = temporary();
    emit({MirOpcode::constant, shortcut, "bool", {node.label == "&&" ? "false" : "true"}, node.range});
    emit({MirOpcode::store, {}, "bool", {shortcut, result_slot}, node.range});
    emit({MirOpcode::jump, {}, {}, {merge_name}, node.range});

    current_block_ = merge_block;
    const auto result = temporary();
    emit({MirOpcode::load, result, "bool", {result_slot}, node.range});
    return result;
  }

  void emit_bounds_check(const std::string& index_value, const std::string& index_type,
                         std::uint64_t length, SourceRange range) {
    const bool unsigned_index = index_type == "u8" || index_type == "u16" || index_type == "u32" ||
                                index_type == "u64" || index_type == "usize" || index_type == "byte";
    const auto upper_block = unsigned_index ? current_block_ : create_block("bounds.upper");
    const auto valid_block = create_block("bounds.valid");
    const auto panic_block = create_block("bounds.panic");
    const auto valid_name = block_name(valid_block);
    const auto panic_name = block_name(panic_block);

    if (!unsigned_index) {
      const auto zero = temporary();
      emit({MirOpcode::constant, zero, index_type, {"0"}, range});
      const auto nonnegative = temporary();
      emit({MirOpcode::less_equal, nonnegative, index_type, {zero, index_value}, range});
      emit({MirOpcode::branch, {}, "bool", {nonnegative, block_name(upper_block), panic_name}, range});
      current_block_ = upper_block;
    }

    const auto limit = temporary();
    emit({MirOpcode::constant, limit, index_type, {std::to_string(length)}, range});
    const auto in_range = temporary();
    emit({MirOpcode::less, in_range, index_type, {index_value, limit}, range});
    emit({MirOpcode::branch, {}, "bool", {in_range, valid_name, panic_name}, range});

    current_block_ = panic_block;
    emit({MirOpcode::unreachable, {}, {}, {}, range});
    current_block_ = valid_block;
  }

  void emit_dynamic_bounds_check(const std::string& index_value,
                                 const std::string& index_type,
                                 const std::string& length_value,
                                 SourceRange range) {
    const bool unsigned_index = index_type == "u8" || index_type == "u16" || index_type == "u32" ||
                                index_type == "u64" || index_type == "usize" || index_type == "byte";
    const auto upper_block = unsigned_index ? current_block_ : create_block("bounds.upper");
    const auto valid_block = create_block("bounds.valid");
    const auto panic_block = create_block("bounds.panic");
    const auto valid_name = block_name(valid_block);
    const auto panic_name = block_name(panic_block);
    if (!unsigned_index) {
      const auto zero = temporary();
      emit({MirOpcode::constant, zero, index_type, {"0"}, range});
      const auto nonnegative = temporary();
      emit({MirOpcode::less_equal, nonnegative, index_type, {zero, index_value}, range});
      emit({MirOpcode::branch, {}, "bool", {nonnegative, block_name(upper_block), panic_name}, range});
      current_block_ = upper_block;
    }
    const auto in_range = temporary();
    emit({MirOpcode::less, in_range, index_type, {index_value, length_value}, range});
    emit({MirOpcode::branch, {}, "bool", {in_range, valid_name, panic_name}, range});
    current_block_ = panic_block;
    emit({MirOpcode::unreachable, {}, {}, {}, range});
    current_block_ = valid_block;
  }

  void clone_value_into(const std::string& type, const std::string& source_place,
                        const std::string& destination_place, SourceRange range) {
    if (copy_types_.contains(type) || parse_reference_type(type) || parse_slice_type(type) ||
        parse_raw_pointer_type(type)) {
      if (is_aggregate(type)) copy_aggregate(type, source_place, destination_place, range);
      else {
        const auto value = temporary();
        emit({MirOpcode::load, value, type, {source_place}, range});
        emit({MirOpcode::store, {}, type, {value, destination_place}, range});
      }
      return;
    }
    if (const auto implementation = clone_functions_.find(type); implementation != clone_functions_.end()) {
      const auto result = temporary();
      emit({MirOpcode::call, result, type, {implementation->second, source_place}, range});
      if (is_aggregate(type)) copy_aggregate(type, result, destination_place, range);
      else emit({MirOpcode::store, {}, type, {result, destination_place}, range});
      return;
    }
    if (parse_dynamic_trait_type(type)) {
      const auto handle = temporary();
      emit({MirOpcode::load, handle, type, {source_place}, range});
      const auto cloned = temporary();
      emit({MirOpcode::trait_object_clone, cloned, type, {handle}, range});
      emit({MirOpcode::store, {}, type, {cloned, destination_place}, range});
      return;
    }
    if (const auto callable = parse_callable_type(type)) {
      if (callable->kind == CallableKind::once) {
        diagnostics_.error("D3032", range, "FnOnce callable environments cannot be cloned");
        return;
      }
      const auto handle = temporary();
      emit({MirOpcode::load, handle, type, {source_place}, range});
      const auto cloned = temporary();
      emit({MirOpcode::callable_clone, cloned, type, {handle}, range});
      emit({MirOpcode::store, {}, type, {cloned, destination_place}, range});
      return;
    }
    if (is_aggregate(type)) {
      clone_aggregate(type, source_place, destination_place, range);
      return;
    }
    const auto value = temporary();
    emit({MirOpcode::load, value, type, {source_place}, range});
    emit({MirOpcode::store, {}, type, {value, destination_place}, range});
  }

  void clone_aggregate(const std::string& type, const std::string& source,
                       const std::string& destination, SourceRange range) {
    if (const auto array = parse_fixed_array_type(type)) {
      const auto stride = type_size(array->element_type);
      for (std::uint64_t index = 0; index < array->length; ++index) {
        const auto offset = std::to_string(index * stride);
        const auto source_element = temporary();
        const auto destination_element = temporary();
        emit({MirOpcode::pointer_offset, source_element, array->element_type, {source, offset}, range});
        emit({MirOpcode::pointer_offset, destination_element, array->element_type, {destination, offset}, range});
        clone_value_into(array->element_type, source_element, destination_element, range);
      }
      return;
    }
    if (const auto enumeration = enums_.find(type); enumeration != enums_.end() && enumeration->second.tagged) {
      const auto source_tag_place = temporary();
      const auto destination_tag_place = temporary();
      emit({MirOpcode::pointer_offset, source_tag_place, "i32", {source, "0"}, range});
      emit({MirOpcode::pointer_offset, destination_tag_place, "i32", {destination, "0"}, range});
      const auto tag = temporary();
      emit({MirOpcode::load, tag, "i32", {source_tag_place}, range});
      emit({MirOpcode::store, {}, "i32", {tag, destination_tag_place}, range});

      std::vector<const EnumVariantLayout*> variants;
      variants.reserve(enumeration->second.variants.size());
      for (const auto& [_, variant] : enumeration->second.variants) variants.push_back(&variant);
      std::sort(variants.begin(), variants.end(), [](const EnumVariantLayout* left, const EnumVariantLayout* right) {
        return left->discriminant < right->discriminant;
      });
      if (variants.empty()) return;
      const auto merge_block = create_block("clone.enum.end");
      const auto merge_name = block_name(merge_block);
      for (std::size_t variant_index = 0; variant_index < variants.size(); ++variant_index) {
        const auto* variant = variants[variant_index];
        const auto payload_block = create_block("clone.enum.payload");
        const auto next_block = variant_index + 1 == variants.size()
            ? merge_block : create_block("clone.enum.next");
        const auto expected = temporary();
        emit({MirOpcode::constant, expected, "i32", {std::to_string(variant->discriminant)}, range});
        const auto matches = temporary();
        emit({MirOpcode::equal, matches, "i32", {tag, expected}, range});
        emit({MirOpcode::branch, {}, "bool",
              {matches, block_name(payload_block), block_name(next_block)}, range});
        current_block_ = payload_block;
        for (std::size_t index = 0; index < variant->payload_types.size(); ++index) {
          const auto& payload_type = variant->payload_types[index];
          const auto offset = enumeration->second.payload_offset + variant->payload_offsets[index];
          const auto source_payload = temporary();
          const auto destination_payload = temporary();
          emit({MirOpcode::pointer_offset, source_payload, payload_type,
                {source, std::to_string(offset)}, range});
          emit({MirOpcode::pointer_offset, destination_payload, payload_type,
                {destination, std::to_string(offset)}, range});
          clone_value_into(payload_type, source_payload, destination_payload, range);
        }
        emit({MirOpcode::jump, {}, {}, {merge_name}, range});
        current_block_ = next_block;
      }
      current_block_ = merge_block;
      return;
    }
    const auto layout = layouts_.find(type);
    if (layout == layouts_.end()) return;
    for (const auto& field : layout->second.ordered_fields) {
      const auto source_field = temporary();
      const auto destination_field = temporary();
      emit({MirOpcode::pointer_offset, source_field, field.type_name,
            {source, std::to_string(field.offset)}, range});
      emit({MirOpcode::pointer_offset, destination_field, field.type_name,
            {destination, std::to_string(field.offset)}, range});
      clone_value_into(field.type_name, source_field, destination_field, range);
    }
  }

  void copy_aggregate(const std::string& type, const std::string& source,
                      const std::string& destination, SourceRange range) {
    if (const auto array = parse_fixed_array_type(type)) {
      const auto stride = type_size(array->element_type);
      for (std::uint64_t index = 0; index < array->length; ++index) {
        const auto offset = std::to_string(index * stride);
        const auto source_element = temporary();
        const auto destination_element = temporary();
        emit({MirOpcode::pointer_offset, source_element, array->element_type, {source, offset}, range});
        emit({MirOpcode::pointer_offset, destination_element, array->element_type, {destination, offset}, range});
        if (is_aggregate(array->element_type)) {
          copy_aggregate(array->element_type, source_element, destination_element, range);
        } else {
          const auto value = temporary();
          emit({MirOpcode::load, value, array->element_type, {source_element}, range});
          emit({MirOpcode::store, {}, array->element_type, {value, destination_element}, range});
        }
      }
      return;
    }
    if (const auto enumeration = enums_.find(type); enumeration != enums_.end() && enumeration->second.tagged) {
      for (std::uint64_t offset = 0; offset < enumeration->second.size; ++offset) {
        const auto source_byte = temporary();
        const auto destination_byte = temporary();
        emit({MirOpcode::pointer_offset, source_byte, "u8", {source, std::to_string(offset)}, range});
        emit({MirOpcode::pointer_offset, destination_byte, "u8", {destination, std::to_string(offset)}, range});
        const auto value = temporary();
        emit({MirOpcode::load, value, "u8", {source_byte}, range});
        emit({MirOpcode::store, {}, "u8", {value, destination_byte}, range});
      }
      return;
    }
    const auto layout = layouts_.find(type);
    if (layout == layouts_.end()) return;
    for (const auto& field : layout->second.ordered_fields) {
      const auto source_field = temporary();
      const auto destination_field = temporary();
      emit({MirOpcode::pointer_offset, source_field, field.type_name, {source, std::to_string(field.offset)}, range});
      emit({MirOpcode::pointer_offset, destination_field, field.type_name, {destination, std::to_string(field.offset)}, range});
      if (is_aggregate(field.type_name)) {
        copy_aggregate(field.type_name, source_field, destination_field, range);
      } else {
        const auto value = temporary();
        emit({MirOpcode::load, value, field.type_name, {source_field}, range});
        emit({MirOpcode::store, {}, field.type_name, {value, destination_field}, range});
      }
    }
  }

  void initialize_place(const std::string& type, const std::string& destination,
                        const SyntaxNode& initializer) {
    if (const auto slice = parse_slice_type(type);
        slice && initializer.kind == SyntaxKind::unary_expression &&
        (initializer.label == "&" || initializer.label == "&mut") &&
        !initializer.children.empty()) {
      const auto [source_place, source_type] = lower_place(initializer.children.front());
      const auto array = parse_fixed_array_type(source_type);
      if (!source_place.empty() && array) {
        const auto data_place = temporary();
        const auto data_type = slice->element_type + (slice->mutable_slice ? "&mut" : "&");
        emit({MirOpcode::pointer_offset, data_place, data_type, {destination, "0"}, initializer.range});
        emit({MirOpcode::store, {}, data_type, {source_place, data_place}, initializer.range});
        const auto length_place = temporary();
        emit({MirOpcode::pointer_offset, length_place, "usize", {destination, "8"}, initializer.range});
        const auto length_value = temporary();
        emit({MirOpcode::constant, length_value, "usize", {std::to_string(array->length)}, initializer.range});
        emit({MirOpcode::store, {}, "usize", {length_value, length_place}, initializer.range});
        return;
      }
    }
    if (const auto array = parse_fixed_array_type(type); array && initializer.kind == SyntaxKind::array_expression) {
      const auto stride = type_size(array->element_type);
      for (std::size_t index = 0; index < initializer.children.size() && index < array->length; ++index) {
        const auto element_place = temporary();
        emit({MirOpcode::pointer_offset, element_place, array->element_type,
              {destination, std::to_string(index * stride)}, initializer.children[index].range});
        if (is_aggregate(array->element_type)) {
          const auto source = lower_expression(initializer.children[index]);
          copy_aggregate(array->element_type, source, element_place, initializer.children[index].range);
        } else {
          const auto source_type = expression_type(initializer.children[index]);
          auto value = lower_expression(initializer.children[index]);
          value = coerce_implicit_numeric(value, source_type, array->element_type, initializer.children[index].range);
          emit({MirOpcode::store, {}, array->element_type, {value, element_place}, initializer.children[index].range});
        }
      }
      return;
    }
    auto source = lower_expression(initializer);
    if (is_aggregate(type)) copy_aggregate(type, source, destination, initializer.range);
    else {
      source = coerce_implicit_numeric(source, expression_type(initializer), type, initializer.range);
      emit({MirOpcode::store, {}, type, {source, destination}, initializer.range});
    }
  }

  std::pair<std::string, std::string> lower_place(const SyntaxNode& node) {
    if (node.kind == SyntaxKind::unary_expression && node.label == "*" && !node.children.empty()) {
      const auto reference = lower_expression(node.children.front());
      const auto reference_type = expression_type(node.children.front());
      if (const auto parsed = parse_reference_type(reference_type)) return {reference, parsed->referent_type};
      if (const auto pointer = parse_raw_pointer_type(reference_type)) return {reference, pointer->pointee_type};
      return {};
    }
    if (node.kind == SyntaxKind::name_expression) {
      const auto place = places_.find(node.label);
      const auto type = types_.find(node.label);
      if (place != places_.end() && type != types_.end()) return {place->second, type->second};
      return {};
    }
    if (node.kind == SyntaxKind::index_expression && node.children.size() >= 2) {
      auto [base_place, base_type] = lower_place(node.children.front());
      if (base_place.empty()) return {};
      const auto array = parse_fixed_array_type(base_type);
      const auto slice = parse_slice_type(base_type);
      if (!array && !slice) return {};
      const auto index_value = lower_expression(node.children[1]);
      const auto index_type = expression_type(node.children[1]);
      std::string element_type;
      std::string data_base = base_place;
      if (array) {
        element_type = array->element_type;
        emit_bounds_check(index_value, index_type, array->length, node.children[1].range);
      } else {
        element_type = slice->element_type;
        const auto data_field = temporary();
        const auto data_type = element_type + (slice->mutable_slice ? "&mut" : "&");
        emit({MirOpcode::pointer_offset, data_field, data_type, {base_place, "0"}, node.range});
        data_base = temporary();
        emit({MirOpcode::load, data_base, data_type, {data_field}, node.range});
        const auto length_field = temporary();
        emit({MirOpcode::pointer_offset, length_field, "usize", {base_place, "8"}, node.range});
        const auto length_value = temporary();
        emit({MirOpcode::load, length_value, index_type, {length_field}, node.range});
        emit_dynamic_bounds_check(index_value, index_type, length_value, node.children[1].range);
      }
      const auto element_size_value = temporary();
      emit({MirOpcode::constant, element_size_value, "usize", {std::to_string(type_size(element_type))}, node.range});
      const auto byte_offset = temporary();
      emit({MirOpcode::multiply, byte_offset, "usize", {index_value, element_size_value}, node.range});
      const auto address = temporary();
      emit({MirOpcode::pointer_offset, address, element_type, {data_base, byte_offset}, node.range});
      emit_place_path(address, logical_place_path(node), element_type, node.range);
      return {address, element_type};
    }
    if (node.kind == SyntaxKind::member_expression && !node.children.empty()) {
      auto [base_place, base_type] = lower_place(node.children.front());
      if (base_place.empty()) return {};
      if (const auto reference = parse_reference_type(base_type)) {
        const auto pointer = temporary();
        emit({MirOpcode::load, pointer, base_type, {base_place}, node.children.front().range});
        base_place = pointer;
        base_type = reference->referent_type;
      }
      const auto layout = layouts_.find(base_type);
      if (layout == layouts_.end()) return {};
      const auto field = layout->second.fields.find(node.label);
      if (field == layout->second.fields.end()) return {};
      const auto address = temporary();
      emit({MirOpcode::pointer_offset, address, field->second.type_name,
            {base_place, std::to_string(field->second.offset)}, node.range});
      emit_place_path(address, logical_place_path(node), field->second.type_name, node.range);
      return {address, field->second.type_name};
    }
    // Fallback: an aggregate-valued rvalue (an array/tuple literal, a call
    // that returns an aggregate, etc.) has no storage of its own yet. Every
    // aggregate is already represented by its storage address once lowered,
    // so materializing it through lower_expression yields a usable place.
    const auto type = expression_type(node);
    if (is_aggregate(type)) {
      const auto value = lower_expression(node);
      if (!value.empty()) return {value, type};
    }
    return {};
  }

  std::string lower_expression(const SyntaxNode& node) {
    if (node.kind == SyntaxKind::struct_expression) {
      const auto layout = layouts_.find(expression_type(node));
      if (layout == layouts_.end()) {
        diagnostics_.error("D3012", node.range, "struct layout is unavailable for '" + expression_type(node) + "'");
        return {};
      }
      SyntaxNode rewritten;
      rewritten.kind = SyntaxKind::call_expression;
      rewritten.range = node.range;
      SyntaxNode callee;
      callee.kind = SyntaxKind::name_expression;
      callee.label = expression_type(node);
      callee.range = node.range;
      rewritten.children.push_back(std::move(callee));
      for (const auto& expected : layout->second.ordered_fields) {
        const auto initializer = std::find_if(node.children.begin(), node.children.end(),
            [&](const SyntaxNode& candidate) { return candidate.label == expected.name; });
        if (initializer != node.children.end() && !initializer->children.empty())
          rewritten.children.push_back(initializer->children.front());
      }
      return lower_expression(rewritten);
    }
    if (node.kind == SyntaxKind::closure_expression) {
      const auto arrow = node.label.find(" -> ");
      const auto function_name = arrow == std::string::npos ? node.label : node.label.substr(0, arrow);
      const auto captures = closure_capture_names(node);
      const auto signature = signatures_.find(function_name);
      if (signature == signatures_.end()) {
        diagnostics_.error("D3029", node.range, "closure function metadata is unavailable");
        return {};
      }
      if (captures.empty()) {
        const auto result = temporary();
        emit({MirOpcode::function_address, result,
              function_type_name(signature->second.parameter_types, signature->second.return_type),
              {function_name}, node.range});
        return result;
      }
      struct EscapingCapture final {
        std::string type;
        std::string value;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        std::uint32_t alignment = 1;
        bool inline_storage = false;
        std::string cleanup_symbol;
      };
      std::vector<EscapingCapture> escaping_captures;
      escaping_captures.reserve(captures.size());
      std::uint64_t environment_size = 0;
      const auto align_up = [](std::uint64_t value, std::uint32_t alignment) {
        const auto mask = static_cast<std::uint64_t>(std::max<std::uint32_t>(1, alignment) - 1);
        return (value + mask) & ~mask;
      };
      for (std::size_t capture_index = 0; capture_index < captures.size(); ++capture_index) {
        const auto& capture = captures[capture_index];
        SyntaxNode capture_node; capture_node.kind = SyntaxKind::name_expression; capture_node.label = capture; capture_node.range = node.range;
        const auto source_type = types_.contains(capture) ? types_.at(capture) : expression_type(capture_node);
        const auto parameter_type = capture_index < signature->second.parameter_types.size()
            ? signature->second.parameter_types[capture_index] : source_type;
        EscapingCapture item;
        item.type = parameter_type;
        item.inline_storage = is_aggregate(source_type) && !parse_reference_type(parameter_type);
        item.size = item.inline_storage ? type_size(source_type) : type_size(parameter_type);
        item.alignment = item.inline_storage ? type_alignment(source_type) : type_alignment(parameter_type);
        environment_size = align_up(environment_size, item.alignment);
        item.offset = environment_size;
        environment_size += std::max<std::uint64_t>(1, item.size);
        if (node.modifier == "ref" || node.modifier == "mut") {
          item.value = lower_place(capture_node).first;
        } else {
          item.value = lower_expression(capture_node);
        }
        if (node.modifier == "move" && needs_drop(source_type)) {
          item.cleanup_symbol = "__raz_async_cleanup_";
          for (const unsigned char character : source_type)
            item.cleanup_symbol += std::isalnum(character) != 0 ? static_cast<char>(character) : '_';
        }
        escaping_captures.push_back(std::move(item));
        if (node.modifier == "move") mark_runtime_moved(capture, node.range);
      }
      environment_size = std::max<std::uint64_t>(8, environment_size);
      std::vector<std::string> operands{function_name, node.modifier == "move" ? "FnOnce" : (node.modifier == "mut" ? "FnMut" : "Fn")};
      const auto capture_count = captures.size();
      std::vector<std::string> explicit_parameters(signature->second.parameter_types.begin() + static_cast<std::ptrdiff_t>(capture_count),
                                                   signature->second.parameter_types.end());
      const auto kind = node.modifier == "move" ? CallableKind::once : (node.modifier == "mut" ? CallableKind::mutable_call : CallableKind::shared);
      const auto callable_type = callable_type_name(kind, explicit_parameters, signature->second.return_type);
      std::string canonical = kind == CallableKind::shared ? "Fn" : (kind == CallableKind::mutable_call ? "FnMut" : "FnOnce");
      canonical += '(';
      for (std::size_t index = 0; index < explicit_parameters.size(); ++index) {
        if (index != 0) canonical += ',';
        canonical += explicit_parameters[index];
      }
      canonical += ")->" + signature->second.return_type;
      operands.insert(operands.begin() + 2, std::to_string(stable_dispatch_id(canonical)));
      operands.insert(operands.begin() + 3, std::to_string(environment_size));
      for (const auto& capture : escaping_captures) {
        operands.push_back(capture.type);
        operands.push_back(std::to_string(capture.offset));
        operands.push_back(std::to_string(capture.size));
        operands.push_back(std::to_string(capture.alignment));
        operands.push_back(capture.inline_storage ? "inline" : "value");
        operands.push_back(capture.value);
        operands.push_back(capture.cleanup_symbol.empty() ? "-" : capture.cleanup_symbol);
      }
      const auto result = temporary();
      emit({MirOpcode::callable_create, result, callable_type, std::move(operands), node.range});
      return result;
    }
    if (node.kind == SyntaxKind::literal_expression) {
      const auto result = temporary();
      std::string literal = node.label;
      if (!literal.empty() && literal.front() == '\'') {
        const auto character = decode_character_literal(literal);
        if (!character) {
          diagnostics_.error("D3037", node.range, "invalid character literal during MIR lowering");
          return {};
        }
        literal = std::to_string(*character);
      }
      emit({MirOpcode::constant, result, expression_type(node), {std::move(literal)}, node.range});
      return result;
    }
    if (node.kind == SyntaxKind::name_expression) {
      if (const auto intrinsic = primitive_integer_constant(node.label)) {
        const auto result = temporary();
        emit({MirOpcode::constant, result, intrinsic->first, {intrinsic->second}, node.range});
        return result;
      }
      if (const auto local_constant = local_constants_.find(node.label); local_constant != local_constants_.end()) return local_constant->second;
      if (const auto constant = constants_.find(node.label); constant != constants_.end()) {
        return materialize_constant_value(constant->second.first, constant->second.second, node.range);
      }
      if (node.label == "true" || node.label == "false") {
        const auto result = temporary();
        emit({MirOpcode::constant, result, "bool", {node.label}, node.range});
        return result;
      }
      const auto found = places_.find(node.label);
      if (found == places_.end()) {
        if (signatures_.contains(node.label)) {
          const auto result = temporary();
          emit({MirOpcode::function_address, result, expression_type(node), {node.label}, node.range});
          return result;
        }
        return node.label;
      }
      const auto type = expression_type(node);
      if (is_aggregate(type)) return found->second;
      const auto result = temporary();
      emit({MirOpcode::load, result, type, {found->second}, node.range});
      return result;
    }
    if (node.kind == SyntaxKind::tuple_expression) {
      const auto type = expression_type(node);
      const auto layout = layouts_.find(type);
      if (layout == layouts_.end()) {
        diagnostics_.error("D3012", node.range, "tuple layout is unavailable for '" + type + "'");
        return {};
      }
      const auto storage = temporary();
      emit({MirOpcode::stack_allocate, storage, type,
            {std::to_string(std::max<std::uint64_t>(1, layout->second.size)),
             std::to_string(layout->second.alignment)}, node.range});
      for (std::size_t index = 0; index < node.children.size() && index < layout->second.ordered_fields.size(); ++index) {
        const auto& field = layout->second.ordered_fields[index];
        const auto place = temporary();
        emit({MirOpcode::pointer_offset, place, field.type_name,
              {storage, std::to_string(field.offset)}, node.children[index].range});
        const auto value = lower_expression(node.children[index]);
        if (is_aggregate(field.type_name)) copy_aggregate(field.type_name, value, place, node.children[index].range);
        else emit({MirOpcode::store, {}, field.type_name, {value, place}, node.children[index].range});
      }
      return storage;
    }
    if (node.kind == SyntaxKind::array_expression) {
      const auto type = expression_type(node);
      const auto array = parse_fixed_array_type(type);
      if (!array) {
        diagnostics_.error("D3012", node.range, "array literal layout is unavailable for '" + type + "'");
        return {};
      }
      const auto storage = temporary();
      emit({MirOpcode::stack_allocate, storage, type,
            {std::to_string(std::max<std::uint64_t>(1, type_size(type))), std::to_string(type_alignment(type))}, node.range});
      const auto stride = type_size(array->element_type);
      for (std::size_t index = 0; index < node.children.size(); ++index) {
        const auto element_place = temporary();
        emit({MirOpcode::pointer_offset, element_place, array->element_type,
              {storage, std::to_string(index * stride)}, node.children[index].range});
        if (is_aggregate(array->element_type)) {
          const auto source = lower_expression(node.children[index]);
          copy_aggregate(array->element_type, source, element_place, node.children[index].range);
        } else {
          const auto value = lower_expression(node.children[index]);
          emit({MirOpcode::store, {}, array->element_type, {value, element_place}, node.children[index].range});
        }
      }
      return storage;
    }
    if (node.kind == SyntaxKind::parenthesized_expression && !node.children.empty()) {
      return lower_expression(node.children.front());
    }
    if (node.kind == SyntaxKind::try_expression && !node.children.empty()) {
      const auto operand_type = expression_type(node.children.front());
      const auto enumeration = enums_.find(operand_type);
      const auto return_enumeration = enums_.find(hir_.return_type);
      if (enumeration == enums_.end() || !enumeration->second.tagged ||
          return_enumeration == enums_.end() || !return_enumeration->second.tagged) {
        diagnostics_.error("D3010", node.range, "the '?' operand or enclosing return type is not a tagged enum");
        return {};
      }
      const bool is_result = enumeration->second.variants.contains("Ok");
      const auto success = is_result ? enumeration->second.variants.find("Ok")
                                     : enumeration->second.variants.find("Some");
      const auto failure = is_result ? enumeration->second.variants.find("Error")
                                     : enumeration->second.variants.find("None");
      const auto return_failure = is_result ? return_enumeration->second.variants.find("Error")
                                            : return_enumeration->second.variants.find("None");
      if (success == enumeration->second.variants.end() || success->second.payload_types.empty() ||
          failure == enumeration->second.variants.end() || return_failure == return_enumeration->second.variants.end()) {
        diagnostics_.error("D3011", node.range, "the '?' operand has an invalid Result/Option layout");
        return {};
      }
      const auto value = lower_expression(node.children.front());
      const auto tag_place = temporary();
      emit({MirOpcode::pointer_offset, tag_place, "i32", {value, "0"}, node.range});
      const auto tag = temporary();
      emit({MirOpcode::load, tag, "i32", {tag_place}, node.range});
      const auto expected = temporary();
      emit({MirOpcode::constant, expected, "i32", {std::to_string(success->second.discriminant)}, node.range});
      const auto is_success = temporary();
      emit({MirOpcode::equal, is_success, "i32", {tag, expected}, node.range});
      const auto success_block = create_block("try.success");
      const auto failure_block = create_block("try.failure");
      emit({MirOpcode::branch, {}, "bool", {is_success, block_name(success_block), block_name(failure_block)}, node.range});

      current_block_ = failure_block;
      std::string failure_value;
      if (operand_type == hir_.return_type) {
        failure_value = preserve_return_value(hir_.return_type, value, node.range);
      } else {
        failure_value = temporary();
        emit({MirOpcode::stack_allocate, failure_value, hir_.return_type,
              {std::to_string(return_enumeration->second.size),
               std::to_string(return_enumeration->second.alignment)}, node.range});
        const auto return_tag_place = temporary();
        emit({MirOpcode::pointer_offset, return_tag_place, "i32", {failure_value, "0"}, node.range});
        const auto return_tag = temporary();
        emit({MirOpcode::constant, return_tag, "i32",
              {std::to_string(return_failure->second.discriminant)}, node.range});
        emit({MirOpcode::store, {}, "i32", {return_tag, return_tag_place}, node.range});
        const auto payload_count = std::min(failure->second.payload_types.size(),
                                            return_failure->second.payload_types.size());
        for (std::size_t index = 0; index < payload_count; ++index) {
          const auto& source_type = failure->second.payload_types[index];
          const auto& target_type = return_failure->second.payload_types[index];
          const auto source_place = temporary();
          emit({MirOpcode::pointer_offset, source_place, source_type,
                {value, std::to_string(enumeration->second.payload_offset +
                                       failure->second.payload_offsets[index])}, node.range});
          const auto target_place = temporary();
          emit({MirOpcode::pointer_offset, target_place, target_type,
                {failure_value, std::to_string(return_enumeration->second.payload_offset +
                                               return_failure->second.payload_offsets[index])}, node.range});
          if (is_aggregate(source_type)) {
            copy_aggregate(source_type, source_place, target_place, node.range);
          } else {
            const auto payload = temporary();
            emit({MirOpcode::load, payload, source_type, {source_place}, node.range});
            emit({MirOpcode::store, {}, target_type, {payload, target_place}, node.range});
          }
        }
      }
      // A propagated failure is an early return and must obey exactly the same
      // lexical cleanup contract as an explicit `return`: defer runs in LIFO
      // order and owned locals are dropped after the return value is preserved.
      emit_deferred_from(0);
      emit_drops_from(0);
      emit({MirOpcode::return_value, {}, hir_.return_type, {failure_value}, node.range});

      current_block_ = success_block;
      const auto& payload_type = success->second.payload_types.front();
      const auto payload_place = temporary();
      emit({MirOpcode::pointer_offset, payload_place, payload_type,
            {value, std::to_string(enumeration->second.payload_offset + success->second.payload_offsets.front())}, node.range});
      if (is_aggregate(payload_type)) return payload_place;
      const auto payload = temporary();
      emit({MirOpcode::load, payload, payload_type, {payload_place}, node.range});
      return payload;
    }
    if (node.kind == SyntaxKind::unary_expression && !node.children.empty()) {
      if (node.label == "&" || node.label == "&mut") {
        const auto [place, referent_type] = lower_place(node.children.front());
        const auto path = logical_place_path(node.children.front());
        emit({node.label == "&mut" ? MirOpcode::borrow_exclusive : MirOpcode::borrow_shared,
              {}, referent_type + (node.label == "&mut" ? "&mut" : "&"),
              path.empty() ? std::vector<std::string>{place}
                           : std::vector<std::string>{place, path},
              node.range});
        return place;
      }
      if (node.label == "*") {
        const auto [place, type] = lower_place(node);
        if (is_aggregate(type)) return place;
        const auto result = temporary();
        emit({MirOpcode::load, result, type, {place}, node.range});
        return result;
      }
      const auto value = lower_expression(node.children.front());
      if (node.label == "move") {
        const auto path = logical_place_path(node.children.front());
        if (!path.empty()) mark_runtime_moved(path, node.range);
        return value;
      }
      if (node.label == "+") return value;
      if (node.label == "await") {
        const auto result = temporary();
        emit({MirOpcode::async_await, result, expression_type(node), {value}, node.range});
        return result;
      }
      if (node.label == "spawn") {
        const auto result = temporary();
        emit({MirOpcode::task_spawn, result, expression_type(node), {value}, node.range});
        return result;
      }
      const auto type = expression_type(node.children.front());
      const auto result = temporary();
      if (node.label == "-") {
        const auto zero = temporary();
        emit({MirOpcode::constant, zero, type, {"0"}, node.range});
        emit({MirOpcode::subtract, result, type, {zero, value}, node.range});
        return result;
      }
      if (node.label == "!") {
        const auto falsy = temporary();
        emit({MirOpcode::constant, falsy, "bool", {"false"}, node.range});
        emit({MirOpcode::equal, result, "bool", {value, falsy}, node.range});
        return result;
      }
      if (node.label == "~") {
        const auto mask = temporary();
        emit({MirOpcode::constant, mask, type, {"-1"}, node.range});
        emit({MirOpcode::bit_xor, result, type, {value, mask}, node.range});
        return result;
      }
      diagnostics_.error("D3002", node.range, "unary operator '" + node.label + "' is not yet supported by MIR lowering");
      return {};
    }
    if (node.kind == SyntaxKind::cast_expression && !node.children.empty()) {
      const auto source_type = expression_type(node.children.front());
      const auto target_type = node.label;
      const auto value = lower_expression(node.children.front());
      if (source_type == target_type) return value;
      const auto result = temporary();
      emit({MirOpcode::numeric_cast, result, target_type, {value, source_type}, node.range});
      return result;
    }
    if (node.kind == SyntaxKind::binary_expression && node.children.size() >= 2) {
      if (node.label == "&&" || node.label == "||") return lower_short_circuit(node);
      const auto left_type = expression_type(node.children[0]);
      const auto right_type = expression_type(node.children[1]);
      const auto left = lower_expression(node.children[0]);
      auto right = lower_expression(node.children[1]);
      right = coerce_implicit_numeric(right, right_type, left_type, node.children[1].range);
      const auto result = temporary();
      const bool comparison = node.label == "==" || node.label == "!=" || node.label == "<" ||
                              node.label == "<=" || node.label == ">" || node.label == ">=";
      const auto operation_type = comparison ? left_type : expression_type(node);
      emit({binary_opcode(node.label, node.range), result, operation_type, {left, right}, node.range});
      return result;
    }
    if (node.kind == SyntaxKind::member_expression && !node.children.empty() &&
        node.children.front().kind == SyntaxKind::name_expression) {
      if (node.modifier == "scoped") {
        if (const auto intrinsic = primitive_integer_constant(node.children.front().label + "::" + node.label)) {
          const auto result = temporary();
          emit({MirOpcode::constant, result, intrinsic->first, {intrinsic->second}, node.range});
          return result;
        }
      }
      const auto enumeration = enums_.find(node.children.front().label);
      if (enumeration != enums_.end()) {
        const auto variant = enumeration->second.variants.find(node.label);
        if (variant != enumeration->second.variants.end()) {
          if (!enumeration->second.tagged) {
            const auto result = temporary();
            emit({MirOpcode::constant, result, enumeration->first, {std::to_string(variant->second.discriminant)}, node.range});
            return result;
          }
          const auto aggregate = temporary();
          emit({MirOpcode::stack_allocate, aggregate, enumeration->first,
                {std::to_string(enumeration->second.size), std::to_string(enumeration->second.alignment)}, node.range});
          const auto tag_place = temporary();
          emit({MirOpcode::pointer_offset, tag_place, "i32", {aggregate, "0"}, node.range});
          const auto tag = temporary();
          emit({MirOpcode::constant, tag, "i32", {std::to_string(variant->second.discriminant)}, node.range});
          emit({MirOpcode::store, {}, "i32", {tag, tag_place}, node.range});
          return aggregate;
        }
      }
    }
    if (node.kind == SyntaxKind::member_expression || node.kind == SyntaxKind::index_expression) {
      const auto [place, type] = lower_place(node);
      if (place.empty()) { diagnostics_.error("D3009", node.range, "member or index expression has no addressable place"); return {}; }
      if (is_aggregate(type)) return place;
      const auto result = temporary();
      emit({MirOpcode::load, result, type, {place}, node.range});
      return result;
    }
    if (node.kind == SyntaxKind::call_expression && !node.children.empty()) {
      if (node.children.front().kind == SyntaxKind::closure_expression) {
        const auto& closure = node.children.front();
        const auto arrow = closure.label.find(" -> ");
        const auto function_name = arrow == std::string::npos ? closure.label : closure.label.substr(0, arrow);
        const auto signature = signatures_.find(function_name);
        const auto return_type = signature == signatures_.end() ? expression_type(closure) : signature->second.return_type;
        std::vector<std::string> operands{function_name};
        std::vector<std::pair<std::string, std::string>> owned_captures;
        for (const auto& capture : closure_capture_names(closure)) {
          SyntaxNode capture_node;
          capture_node.kind = SyntaxKind::name_expression;
          capture_node.label = capture;
          capture_node.range = closure.range;
          const auto capture_type = types_.contains(capture) ? types_.at(capture) : expression_type(capture_node);
          if (closure.modifier == "ref" || closure.modifier == "mut") {
            const auto [capture_place, _] = lower_place(capture_node);
            operands.push_back(capture_place);
            continue;
          }
          const auto source = lower_expression(capture_node);
          if (closure.modifier == "move" && is_aggregate(capture_type)) {
            const auto snapshot = temporary();
            emit({MirOpcode::stack_allocate, snapshot, capture_type,
                  {std::to_string(type_size(capture_type)), std::to_string(type_alignment(capture_type))}, closure.range});
            copy_aggregate(capture_type, source, snapshot, closure.range);
            operands.push_back(snapshot);
            owned_captures.emplace_back(capture_type, snapshot);
          } else {
            operands.push_back(source);
          }
          if (closure.modifier == "move") mark_runtime_moved(capture, closure.range);
        }
        for (std::size_t index = 1; index < node.children.size(); ++index)
          operands.push_back(lower_expression(node.children[index]));
        const auto result = return_type == "void" ? std::string{} : temporary();
        emit({MirOpcode::call, result, return_type, std::move(operands), node.range});
        for (auto capture = owned_captures.rbegin(); capture != owned_captures.rend(); ++capture) {
          if (needs_drop(capture->first)) emit_drop_value(capture->first, capture->second, node.range);
        }
        return result;
      }
      const auto& callee = node.children.front().label;
      if (node.children.front().kind == SyntaxKind::name_expression) {
        const auto [builtin_base, builtin_arguments] = split_generic_name(callee);
        if ((builtin_base == "size_of" || builtin_base == "align_of") && builtin_arguments.size() == 1 && node.children.size() == 1) {
          const auto& reflected_type = builtin_arguments.front();
          const auto result = temporary();
          const auto value = builtin_base == "size_of" ? type_size(reflected_type) : type_alignment(reflected_type);
          emit({MirOpcode::constant, result, "usize", {std::to_string(value)}, node.range});
          return result;
        }
        const auto closure = closure_bindings_.find(callee);
        if (closure != closure_bindings_.end()) {
          const auto signature = signatures_.find(closure->second.function_name);
          const auto return_type = signature == signatures_.end() ? expression_type(node) : signature->second.return_type;
          std::vector<std::string> operands{closure->second.function_name};
          operands.insert(operands.end(), closure->second.capture_values.begin(), closure->second.capture_values.end());
          for (std::size_t index = 1; index < node.children.size(); ++index)
            operands.push_back(lower_expression(node.children[index]));
          const auto result = return_type == "void" ? std::string{} : temporary();
          emit({MirOpcode::call, result, return_type, std::move(operands), node.range});
          if (closure->second.move_capture && !closure->second.consumed) {
            for (std::size_t capture_index = closure->second.capture_values.size(); capture_index > 0; --capture_index) {
              const auto index = capture_index - 1;
              if (needs_drop(closure->second.capture_types[index])) {
                emit_drop_value(closure->second.capture_types[index], closure->second.capture_values[index], node.range);
              }
              moved_locals_.insert(closure->second.capture_drop_names[index]);
            }
            closure->second.consumed = true;
          }
          return result;
        }
      }
      if (node.children.front().kind == SyntaxKind::name_expression &&
          places_.contains(node.children.front().label)) {
        const auto type = expression_type(node.children.front());
        if (const auto function = parse_function_type(type)) {
          const auto pointer = lower_expression(node.children.front());
          std::string signature_name;
          for (const auto& [candidate_name, candidate] : signatures_) {
            if (candidate.return_type == function->return_type && candidate.parameter_types == function->parameter_types) {
              signature_name = candidate_name;
              break;
            }
          }
          if (signature_name.empty()) {
            diagnostics_.error("D3027", node.range, "function pointer signature has no declared function prototype");
            return {};
          }
          std::vector<std::string> operands{pointer, signature_name};
          for (std::size_t index = 1; index < node.children.size(); ++index)
            operands.push_back(lower_expression(node.children[index]));
          const auto result = function->return_type == "void" ? std::string{} : temporary();
          emit({MirOpcode::call_indirect, result, function->return_type, std::move(operands), node.range});
          return result;
        }
        if (const auto callable = parse_callable_type(type)) {
          const auto handle = lower_expression(node.children.front());
          std::string canonical = callable->kind == CallableKind::shared ? "Fn" :
                                  (callable->kind == CallableKind::mutable_call ? "FnMut" : "FnOnce");
          canonical += '(';
          for (std::size_t index = 0; index < callable->parameter_types.size(); ++index) {
            if (index != 0) canonical += ',';
            canonical += callable->parameter_types[index];
          }
          canonical += ")->" + callable->return_type;
          std::vector<std::string> operands{handle, std::to_string(stable_dispatch_id(canonical))};
          for (std::size_t index = 1; index < node.children.size(); ++index) operands.push_back(lower_expression(node.children[index]));
          const auto result = callable->return_type == "void" ? std::string{} : temporary();
          emit({MirOpcode::callable_invoke, result, callable->return_type, std::move(operands), node.range});
          return result;
        }
      }
      if (node.children.front().kind == SyntaxKind::member_expression &&
          !node.children.front().children.empty()) {
        const auto& receiver_node = node.children.front().children.front();
        const bool type_qualified = receiver_node.kind == SyntaxKind::name_expression &&
                                    (layouts_.contains(receiver_node.label) || enums_.contains(receiver_node.label));
        auto receiver_type = type_qualified ? receiver_node.label : expression_type(receiver_node);
        if (const auto reference = parse_reference_type(receiver_type)) receiver_type = reference->referent_type;
        if (const auto dynamic_trait = parse_dynamic_trait_type(receiver_type)) {
          const auto method = dynamic_methods_.find(dynamic_trait->trait_name + "::" + callee);
          if (method == dynamic_methods_.end()) {
            diagnostics_.error("D3035", node.range, "dynamic trait method metadata is unavailable");
            return {};
          }
          std::vector<std::string> explicit_parameters = method->second.parameter_types;
          if (!explicit_parameters.empty() && (explicit_parameters.front() == "Self&" || explicit_parameters.front() == "Self&mut"))
            explicit_parameters.erase(explicit_parameters.begin());
          std::string canonical = dynamic_trait->trait_name + "::" + callee + "(";
          for (std::size_t index = 0; index < explicit_parameters.size(); ++index) {
            if (index != 0) canonical += ',';
            canonical += explicit_parameters[index];
          }
          canonical += ")->" + method->second.return_type;
          std::vector<std::string> operands{lower_expression(receiver_node), std::to_string(method->second.slot),
                                           std::to_string(stable_dispatch_id(canonical))};
          for (std::size_t index = 1; index < node.children.size(); ++index)
            operands.push_back(lower_expression(node.children[index]));
          const auto result = method->second.return_type == "void" ? std::string{} : temporary();
          emit({MirOpcode::trait_object_invoke, result, method->second.return_type, std::move(operands), node.range});
          return result;
        }
        const auto method = trait_methods_.find(receiver_type + "::" + callee);
        if (method != trait_methods_.end()) {
          std::vector<std::string> operands{method->second};
          const auto signature = signatures_.find(method->second);
          const bool associated = type_qualified && signature != signatures_.end() &&
                                  signature->second.parameter_types.size() == node.children.size() - 1;
          if (!associated) {
            const auto& receiver = node.children.front().children.front();
            const auto expected_receiver = signature != signatures_.end() && !signature->second.parameter_types.empty()
                ? signature->second.parameter_types.front() : std::string{};
            if (parse_reference_type(expected_receiver)) {
              const auto actual_receiver_type = expression_type(receiver);
              if (parse_reference_type(actual_receiver_type)) operands.push_back(lower_expression(receiver));
              else operands.push_back(lower_place(receiver).first);
            } else {
              operands.push_back(lower_expression(receiver));
            }
          }
          for (std::size_t index = 1; index < node.children.size(); ++index) operands.push_back(lower_expression(node.children[index]));
          const auto return_type = signature == signatures_.end() ? expression_type(node) : signature->second.return_type;
          const auto result = return_type == "void" ? std::string{} : temporary();
          emit({MirOpcode::call, result, return_type, std::move(operands), node.range});
          return result;
        }
      }
      if (callee == "clone" && node.children.size() == 2) {
        const auto type = expression_type(node.children[1]);
        const auto source = lower_expression(node.children[1]);
        if (parse_dynamic_trait_type(type)) {
          const auto result = temporary();
          emit({MirOpcode::trait_object_clone, result, type, {source}, node.range});
          return result;
        }
        if (const auto callable = parse_callable_type(type)) {
          if (callable->kind == CallableKind::once) {
            diagnostics_.error("D3032", node.range, "FnOnce callable environments cannot be cloned");
            return {};
          }
          const auto result = temporary();
          emit({MirOpcode::callable_clone, result, type, {source}, node.range});
          return result;
        }
        if (type.starts_with("Fn<") || type.starts_with("FnMut<")) {
          const auto result = temporary();
          emit({MirOpcode::callable_clone, result, type, {source}, node.range});
          return result;
        }
        if (type.starts_with("FnOnce<")) {
          diagnostics_.error("D3032", node.range, "FnOnce callable environments cannot be cloned");
          return {};
        }
        if (const auto implementation = clone_functions_.find(type); implementation != clone_functions_.end()) {
          const auto result = temporary();
          emit({MirOpcode::call, result, type, {implementation->second, source}, node.range});
          return result;
        }
        if (!is_aggregate(type)) return source;
        const auto destination = temporary();
        std::uint64_t size = type_size(type);
        std::uint32_t alignment = type_alignment(type);
        emit({MirOpcode::stack_allocate, destination, type,
              {std::to_string(size), std::to_string(alignment)}, node.range});
        clone_aggregate(type, source, destination, node.range);
        return destination;
      }
      if (node.children.front().kind == SyntaxKind::member_expression &&
          !node.children.front().children.empty() &&
          node.children.front().children.front().kind == SyntaxKind::name_expression) {
        const auto enum_name = node.children.front().children.front().label;
        const auto enumeration = enums_.find(enum_name);
        if (enumeration != enums_.end()) {
          const auto variant = enumeration->second.variants.find(node.children.front().label);
          if (variant != enumeration->second.variants.end()) {
            if (!enumeration->second.tagged) {
              const auto result = temporary();
              emit({MirOpcode::constant, result, enum_name, {std::to_string(variant->second.discriminant)}, node.range});
              return result;
            }
            const auto aggregate = temporary();
            emit({MirOpcode::stack_allocate, aggregate, enum_name,
                  {std::to_string(enumeration->second.size), std::to_string(enumeration->second.alignment)}, node.range});
            const auto tag_place = temporary();
            emit({MirOpcode::pointer_offset, tag_place, "i32", {aggregate, "0"}, node.range});
            const auto tag = temporary();
            emit({MirOpcode::constant, tag, "i32", {std::to_string(variant->second.discriminant)}, node.range});
            emit({MirOpcode::store, {}, "i32", {tag, tag_place}, node.range});
            for (std::size_t index = 1; index < node.children.size() && index - 1 < variant->second.payload_types.size(); ++index) {
              const auto& payload_type = variant->second.payload_types[index - 1];
              const auto payload_place = temporary();
              emit({MirOpcode::pointer_offset, payload_place, payload_type,
                    {aggregate, std::to_string(enumeration->second.payload_offset + variant->second.payload_offsets[index - 1])}, node.children[index].range});
              const auto value = lower_expression(node.children[index]);
              if (is_aggregate(payload_type)) copy_aggregate(payload_type, value, payload_place, node.children[index].range);
              else emit({MirOpcode::store, {}, payload_type, {value, payload_place}, node.children[index].range});
            }
            return aggregate;
          }
        }
      }
      const auto layout = layouts_.find(callee);
      if (layout != layouts_.end()) {
        const auto aggregate = temporary();
        emit({MirOpcode::stack_allocate, aggregate, callee,
              {std::to_string(std::max<std::uint64_t>(1, layout->second.size)), std::to_string(layout->second.alignment)}, node.range});
        for (std::size_t index = 1; index < node.children.size() && index - 1 < layout->second.ordered_fields.size(); ++index) {
          const auto& field = layout->second.ordered_fields[index - 1];
          const auto address = temporary();
          emit({MirOpcode::pointer_offset, address, field.type_name, {aggregate, std::to_string(field.offset)}, node.range});
          const auto source_type = expression_type(node.children[index]);
          auto value = lower_expression(node.children[index]);
          if (is_aggregate(field.type_name)) {
            copy_aggregate(field.type_name, value, address, node.children[index].range);
          } else {
            value = coerce_implicit_numeric(value, source_type, field.type_name, node.children[index].range);
            emit({MirOpcode::store, {}, field.type_name, {value, address}, node.range});
          }
        }
        return aggregate;
      }
      std::vector<std::string> operands{callee};
      const auto signature = signatures_.find(callee);
      for (std::size_t index = 1; index < node.children.size(); ++index) {
        std::string argument;
        if (signature != signatures_.end() && index - 1 < signature->second.parameter_types.size()) {
          const auto& expected_type = signature->second.parameter_types[index - 1];
          const auto expected_slice = parse_slice_type(expected_type);
          const auto& argument_node = node.children[index];
          // A fixed-array borrow coerced to T[] is a two-word slice value, not
          // the raw array pointer. The compilered MIR has always materialized
          // this descriptor (HIR node kind 37); keep the bootstrap path ABI-
          // equivalent so compiler source can freely use the current language.
          if (expected_slice && argument_node.kind == SyntaxKind::unary_expression &&
              (argument_node.label == "&" || argument_node.label == "&mut") &&
              !argument_node.children.empty()) {
            const auto [source_place, source_type] = lower_place(argument_node.children.front());
            const auto source_array = parse_fixed_array_type(source_type);
            if (!source_place.empty() && source_array &&
                source_array->element_type == expected_slice->element_type) {
              argument = temporary();
              emit({MirOpcode::stack_allocate, argument, expected_type, {"16", "8"}, argument_node.range});
              const auto data_place = temporary();
              const auto data_type = expected_slice->element_type + (expected_slice->mutable_slice ? "&mut" : "&");
              emit({MirOpcode::pointer_offset, data_place, data_type, {argument, "0"}, argument_node.range});
              emit({MirOpcode::store, {}, data_type, {source_place, data_place}, argument_node.range});
              const auto length_place = temporary();
              emit({MirOpcode::pointer_offset, length_place, "usize", {argument, "8"}, argument_node.range});
              const auto length_value = temporary();
              emit({MirOpcode::constant, length_value, "usize", {std::to_string(source_array->length)}, argument_node.range});
              emit({MirOpcode::store, {}, "usize", {length_value, length_place}, argument_node.range});
            }
          }
          if (argument.empty()) {
            argument = lower_expression(argument_node);
            argument = coerce_implicit_numeric(argument, expression_type(argument_node), expected_type, argument_node.range);
          }
        } else {
          argument = lower_expression(node.children[index]);
        }
        operands.push_back(argument);
      }
      const auto return_type = expression_type(node);
      const auto result = return_type == "void" ? std::string{} : temporary();
      emit({MirOpcode::call, result, return_type, std::move(operands), node.range});
      return result;
    }
    if (node.kind == SyntaxKind::assignment_expression) {
      if (node.children.size() < 2) return {};
      const auto [target_place, target_type] = lower_place(node.children[0]);
      if (target_place.empty()) {
        diagnostics_.error("D3005", node.range, "assignment target has no mutable storage place");
        return {};
      }
      const auto source_type = expression_type(node.children[1]);
      auto value = lower_expression(node.children[1]);
      value = coerce_implicit_numeric(value, source_type, target_type, node.children[1].range);
      if (node.label != "=") {
        const auto current = temporary();
        emit({MirOpcode::load, current, target_type, {target_place}, node.range});
        const auto combined = temporary();
        emit({binary_opcode(node.label, node.range), combined, target_type, {current, value}, node.range});
        value = combined;
      }
      if (is_aggregate(target_type)) {
        copy_aggregate(target_type, value, target_place, node.range);
      } else {
        emit({MirOpcode::store, {}, target_type, {value, target_place}, node.range});
      }
      if (node.label == "=") {
        mark_reinitialized_path(logical_place_path(node.children.front()), node.range);
      }
      return value;
    }
    diagnostics_.error("D3002", node.range, "expression is not yet supported by MIR lowering");
    return {};
  }

  void lower_if(const SyntaxNode& node) {
    if (node.children.size() < 2) return;
    const auto condition = lower_expression(node.children[0]);
    const auto then_block = create_block("if.then");
    const auto else_block = create_block("if.else");
    const auto merge_block = create_block("if.end");
    const auto then_name = block_name(then_block);
    const auto else_name = block_name(else_block);
    const auto merge_name = block_name(merge_block);
    emit({MirOpcode::branch, {}, "bool", {condition, then_name, else_name}, node.range});

    current_block_ = then_block;
    lower_statement(node.children[1]);
    const bool then_terminated = terminated();
    if (!then_terminated) emit({MirOpcode::jump, {}, {}, {merge_name}, node.range});

    current_block_ = else_block;
    if (node.children.size() >= 3) lower_statement(node.children[2]);
    const bool else_terminated = terminated();
    if (!else_terminated) emit({MirOpcode::jump, {}, {}, {merge_name}, node.range});

    current_block_ = merge_block;
    if (then_terminated && else_terminated) emit({MirOpcode::unreachable, {}, {}, {}, node.range});
  }

  void lower_while(const SyntaxNode& node) {
    if (node.children.size() < 2) return;
    const auto header_block = create_block("while.header");
    const auto body_block = create_block("while.body");
    const auto exit_block = create_block("while.end");
    const auto header_name = block_name(header_block);
    const auto body_name = block_name(body_block);
    const auto exit_name = block_name(exit_block);
    emit({MirOpcode::jump, {}, {}, {header_name}, node.range});

    current_block_ = header_block;
    const auto condition = lower_expression(node.children[0]);
    emit({MirOpcode::branch, {}, "bool", {condition, body_name, exit_name}, node.range});

    current_block_ = body_block;
    loop_stack_.push_back({header_name, exit_name, defer_scopes_.size()});
    lower_statement(node.children[1]);
    loop_stack_.pop_back();
    if (!terminated()) emit({MirOpcode::jump, {}, {}, {header_name}, node.range});

    current_block_ = exit_block;
  }

  void lower_for(const SyntaxNode& node) {
    if (node.label.empty() || node.children.size() < 2) return;
    const auto iterable_type = expression_type(node.children.front());
    const auto& iterable_node = node.children.front();
    if (iterable_node.kind == SyntaxKind::binary_expression &&
        (iterable_node.label == ".." || iterable_node.label == "..=") &&
        iterable_node.children.size() >= 2) {
      const auto element_type = expression_type(iterable_node.children.front());
      const auto start = lower_expression(iterable_node.children.front());
      const auto finish = lower_expression(iterable_node.children[1]);
      const auto value_place = temporary();
      emit({MirOpcode::stack_allocate, value_place, element_type,
            {std::to_string(type_size(element_type)), std::to_string(type_alignment(element_type))}, node.range});
      emit({MirOpcode::store, {}, element_type, {start, value_place}, node.range});

      const auto header_block = create_block("range.header");
      const auto body_block = create_block("range.body");
      const auto exit_block = create_block("range.end");
      const auto header_name = block_name(header_block);
      const auto body_name = block_name(body_block);
      const auto exit_name = block_name(exit_block);
      emit({MirOpcode::jump, {}, {}, {header_name}, node.range});

      current_block_ = header_block;
      const auto current = temporary();
      emit({MirOpcode::load, current, element_type, {value_place}, node.range});
      const auto condition = temporary();
      emit({iterable_node.label == "..=" ? MirOpcode::less_equal : MirOpcode::less,
            condition, element_type, {current, finish}, node.range});
      emit({MirOpcode::branch, {}, "bool", {condition, body_name, exit_name}, node.range});

      current_block_ = body_block;
      const auto old_place = places_.find(node.label);
      const auto old_type = types_.find(node.label);
      const bool had_place = old_place != places_.end();
      const bool had_type = old_type != types_.end();
      const auto saved_place = had_place ? old_place->second : std::string{};
      const auto saved_type = had_type ? old_type->second : std::string{};
      places_[node.label] = value_place;
      types_[node.label] = element_type;

      loop_stack_.push_back({header_name, exit_name, defer_scopes_.size()});
      lower_statement(node.children[1]);
      loop_stack_.pop_back();

      if (had_place) places_[node.label] = saved_place; else places_.erase(node.label);
      if (had_type) types_[node.label] = saved_type; else types_.erase(node.label);

      if (!terminated()) {
        const auto current_value = temporary();
        emit({MirOpcode::load, current_value, element_type, {value_place}, node.range});
        const auto one = temporary();
        emit({MirOpcode::constant, one, element_type, {"1"}, node.range});
        const auto next = temporary();
        emit({MirOpcode::add, next, element_type, {current_value, one}, node.range});
        emit({MirOpcode::store, {}, element_type, {next, value_place}, node.range});
        emit({MirOpcode::jump, {}, {}, {header_name}, node.range});
      }
      current_block_ = exit_block;
      return;
    }

    const auto iterable_reference = parse_reference_type(iterable_type);
    const auto array = parse_fixed_array_type(
        iterable_reference ? iterable_reference->referent_type : iterable_type);
    if (!array) {
      std::string iterator_type = iterable_type;
      std::string iterator_value;
      bool owns_hidden_iterator = false;
      bool owns_temporary_source = false;
      std::string temporary_source_value;
      const bool iterable_is_named_local = node.children.front().kind == SyntaxKind::name_expression;
      if (const auto into_iter = trait_methods_.find(iterable_type + "::into_iter"); into_iter != trait_methods_.end()) {
        const auto into_iter_type = associated_types_.find(iterable_type + "::IntoIterator::IntoIter");
        if (into_iter_type == associated_types_.end()) {
          diagnostics_.error("D3012", node.children.front().range,
                             "IntoIterator lowering requires an IntoIter associated type");
          return;
        }
        iterator_type = into_iter_type->second;
        const auto source_value = lower_expression(node.children.front());
        iterator_value = temporary();
        emit({MirOpcode::call, iterator_value, iterator_type, {into_iter->second, source_value}, node.range});
        owns_hidden_iterator = true;
        owns_temporary_source = !iterable_is_named_local && needs_drop(iterable_type);
        if (owns_temporary_source) temporary_source_value = source_value;
      } else {
        iterator_value = lower_expression(node.children.front());
        owns_hidden_iterator = !iterable_is_named_local && needs_drop(iterator_type);
      }

      const auto next_method = trait_methods_.find(iterator_type + "::next");
      const auto current_method = trait_methods_.find(iterator_type + "::current");
      const auto item_binding = associated_types_.find(iterator_type + "::Iterator::Item");
      if (next_method == trait_methods_.end() || current_method == trait_methods_.end() ||
          item_binding == associated_types_.end()) {
        diagnostics_.error("D3011", node.children.front().range,
                           "for-loop lowering requires a fixed array, integer range, Iterator, or IntoIterator");
        return;
      }

      const auto header_block = create_block("iter.header");
      const auto body_block = create_block("iter.body");
      const auto exit_block = create_block("iter.end");
      const auto header_name = block_name(header_block);
      const auto body_name = block_name(body_block);
      const auto exit_name = block_name(exit_block);
      emit({MirOpcode::jump, {}, {}, {header_name}, node.range});

      current_block_ = header_block;
      const auto has_next = temporary();
      emit({MirOpcode::call, has_next, "bool", {next_method->second, iterator_value}, node.range});
      emit({MirOpcode::branch, {}, "bool", {has_next, body_name, exit_name}, node.range});

      current_block_ = body_block;
      const auto& element_type = item_binding->second;
      const auto current_value = temporary();
      emit({MirOpcode::call, current_value, element_type, {current_method->second, iterator_value}, node.range});
      const auto binding_place = temporary();
      emit({MirOpcode::stack_allocate, binding_place, element_type,
            {std::to_string(type_size(element_type)), std::to_string(type_alignment(element_type))}, node.range});
      if (is_aggregate(element_type)) copy_aggregate(element_type, current_value, binding_place, node.range);
      else emit({MirOpcode::store, {}, element_type, {current_value, binding_place}, node.range});

      const auto old_place = places_.find(node.label);
      const auto old_type = types_.find(node.label);
      const bool had_place = old_place != places_.end();
      const bool had_type = old_type != types_.end();
      const auto saved_place = had_place ? old_place->second : std::string{};
      const auto saved_type = had_type ? old_type->second : std::string{};
      places_[node.label] = binding_place;
      types_[node.label] = element_type;

      // The loop binding is a real lexical value for each iteration. Give it a
      // synthetic cleanup scope so continue/break/return paths destroy it exactly
      // once before transferring control.
      defer_scopes_.emplace_back();
      drop_scopes_.emplace_back();
      if (needs_drop(element_type)) {
        drop_scopes_.back().push_back({node.label, element_type, binding_place, node.range});
      }
      const auto iteration_cleanup_depth = drop_scopes_.size() - 1;
      loop_stack_.push_back({header_name, exit_name, iteration_cleanup_depth});
      lower_statement(node.children[1]);
      loop_stack_.pop_back();

      if (!terminated()) {
        emit_deferred_from(iteration_cleanup_depth);
        emit_drops_from(iteration_cleanup_depth);
      }
      drop_scopes_.pop_back();
      defer_scopes_.pop_back();

      if (had_place) places_[node.label] = saved_place; else places_.erase(node.label);
      if (had_type) types_[node.label] = saved_type; else types_.erase(node.label);
      if (!terminated()) emit({MirOpcode::jump, {}, {}, {header_name}, node.range});
      current_block_ = exit_block;
      // Hidden iterator state has the same ownership obligations as an ordinary
      // local. Destroy it after the loop, including state produced by IntoIterator.
      if (owns_hidden_iterator && needs_drop(iterator_type)) {
        emit_drop_value(iterator_type, iterator_value, node.range);
      }
      if (owns_temporary_source) {
        emit_drop_value(iterable_type, temporary_source_value, node.range);
      }
      return;
    }

    const auto iterable = lower_expression(node.children.front());
    const auto index_place = temporary();
    emit({MirOpcode::stack_allocate, index_place, "usize", {"8", "8"}, node.range});
    const auto zero = temporary();
    emit({MirOpcode::constant, zero, "usize", {"0"}, node.range});
    emit({MirOpcode::store, {}, "usize", {zero, index_place}, node.range});

    const auto header_block = create_block("for.header");
    const auto body_block = create_block("for.body");
    const auto exit_block = create_block("for.end");
    const auto header_name = block_name(header_block);
    const auto body_name = block_name(body_block);
    const auto exit_name = block_name(exit_block);
    emit({MirOpcode::jump, {}, {}, {header_name}, node.range});

    current_block_ = header_block;
    const auto index = temporary();
    emit({MirOpcode::load, index, "usize", {index_place}, node.range});
    const auto length = temporary();
    emit({MirOpcode::constant, length, "usize", {std::to_string(array->length)}, node.range});
    const auto condition = temporary();
    emit({MirOpcode::less, condition, "usize", {index, length}, node.range});
    emit({MirOpcode::branch, {}, "bool", {condition, body_name, exit_name}, node.range});

    current_block_ = body_block;
    const auto element_size = temporary();
    emit({MirOpcode::constant, element_size, "usize", {std::to_string(type_size(array->element_type))}, node.range});
    const auto byte_offset = temporary();
    emit({MirOpcode::multiply, byte_offset, "usize", {index, element_size}, node.range});
    const auto element_place = temporary();
    emit({MirOpcode::pointer_offset, element_place, array->element_type, {iterable, byte_offset}, node.range});

    const auto binding_type = iterable_reference
        ? array->element_type + (iterable_reference->mutable_reference ? "&mut" : "&")
        : array->element_type;
    const auto binding_place = temporary();
    emit({MirOpcode::stack_allocate, binding_place, binding_type,
          {std::to_string(type_size(binding_type)), std::to_string(type_alignment(binding_type))}, node.range});
    if (iterable_reference) {
      emit({MirOpcode::store, {}, binding_type, {element_place, binding_place}, node.range});
    } else if (is_aggregate(array->element_type)) {
      copy_aggregate(array->element_type, element_place, binding_place, node.range);
    } else {
      const auto element = temporary();
      emit({MirOpcode::load, element, array->element_type, {element_place}, node.range});
      emit({MirOpcode::store, {}, array->element_type, {element, binding_place}, node.range});
    }

    const auto old_place = places_.find(node.label);
    const auto old_type = types_.find(node.label);
    const bool had_place = old_place != places_.end();
    const bool had_type = old_type != types_.end();
    const auto saved_place = had_place ? old_place->second : std::string{};
    const auto saved_type = had_type ? old_type->second : std::string{};
    places_[node.label] = binding_place;
    types_[node.label] = binding_type;

    loop_stack_.push_back({header_name, exit_name, defer_scopes_.size()});
    lower_statement(node.children[1]);
    loop_stack_.pop_back();

    if (had_place) places_[node.label] = saved_place; else places_.erase(node.label);
    if (had_type) types_[node.label] = saved_type; else types_.erase(node.label);

    if (!terminated()) {
      const auto current_index = temporary();
      emit({MirOpcode::load, current_index, "usize", {index_place}, node.range});
      const auto one = temporary();
      emit({MirOpcode::constant, one, "usize", {"1"}, node.range});
      const auto next_index = temporary();
      emit({MirOpcode::add, next_index, "usize", {current_index, one}, node.range});
      emit({MirOpcode::store, {}, "usize", {next_index, index_place}, node.range});
      emit({MirOpcode::jump, {}, {}, {header_name}, node.range});
    }

    current_block_ = exit_block;
  }

  void lower_match(const SyntaxNode& node) {
    if (node.children.empty()) return;
    const auto subject = lower_expression(node.children.front());
    const auto subject_type = expression_type(node.children.front());
    const auto subject_path = logical_place_path(node.children.front());
    const auto enumeration = enums_.find(subject_type);
    const bool tagged = enumeration != enums_.end() && enumeration->second.tagged;
    std::string discriminant = subject;
    if (tagged) {
      const auto tag_place = temporary();
      emit({MirOpcode::pointer_offset, tag_place, "i32", {subject, "0"}, node.range});
      if (!subject_path.empty()) emit_place_path(tag_place, subject_path + ".$tag", "i32", node.range);
      discriminant = temporary();
      emit({MirOpcode::load, discriminant, "i32", {tag_place}, node.range});
    }
    const auto comparison_type = tagged ? std::string("i32") : subject_type;
    const auto merge_block = create_block("match.end");
    const auto merge_name = block_name(merge_block);
    std::size_t test_block = current_block_;
    std::optional<std::size_t> wildcard_block;
    bool has_fallthrough = false;

    for (std::size_t index = 1; index < node.children.size(); ++index) {
      const auto& arm = node.children[index];
      if (arm.kind != SyntaxKind::match_arm) continue;
      const auto body_block = create_block("match.arm");
      if (arm.label == "_") { wildcard_block = body_block; continue; }
      if (arm.children.size() < 2) continue;
      const auto& pattern_node = arm.children.front();
      std::string variant_name;
      const SyntaxNode* variant_member = nullptr;
      if (pattern_node.kind == SyntaxKind::member_expression) {
        variant_name = pattern_node.label;
        variant_member = &pattern_node;
      } else if (pattern_node.kind == SyntaxKind::call_expression && !pattern_node.children.empty() &&
                 pattern_node.children.front().kind == SyntaxKind::member_expression) {
        variant_name = pattern_node.children.front().label;
        variant_member = &pattern_node.children.front();
      }
      current_block_ = test_block;
      std::string pattern_value;
      if (enumeration != enums_.end() && variant_member != nullptr) {
        const auto variant = enumeration->second.variants.find(variant_name);
        if (variant != enumeration->second.variants.end()) {
          pattern_value = temporary();
          emit({MirOpcode::constant, pattern_value, comparison_type,
                {std::to_string(variant->second.discriminant)}, arm.range});
        }
      }
      if (pattern_value.empty()) pattern_value = lower_expression(pattern_node);
      const auto equal = temporary();
      emit({MirOpcode::equal, equal, comparison_type, {discriminant, pattern_value}, arm.range});
      const auto next_test = create_block("match.next");
      emit({MirOpcode::branch, {}, "bool", {equal, block_name(body_block), block_name(next_test)}, arm.range});
      current_block_ = body_block;
      drop_scopes_.emplace_back();

      std::vector<std::string> bound_names;
      if (tagged && pattern_node.kind == SyntaxKind::call_expression && enumeration != enums_.end()) {
        const auto variant = enumeration->second.variants.find(variant_name);
        if (variant != enumeration->second.variants.end()) {
          for (std::size_t binding_index = 1; binding_index < pattern_node.children.size() &&
               binding_index - 1 < variant->second.payload_types.size(); ++binding_index) {
            const auto& binding = pattern_node.children[binding_index];
            if (binding.kind != SyntaxKind::name_expression || binding.label == "_") continue;
            const auto& payload_type = variant->second.payload_types[binding_index - 1];
            const auto payload_place = temporary();
            emit({MirOpcode::pointer_offset, payload_place, payload_type,
                  {subject, std::to_string(enumeration->second.payload_offset + variant->second.payload_offsets[binding_index - 1])}, binding.range});
            // Pattern binding transfers ownership out of the enum payload into a
            // fresh logical local even though it aliases the same physical bytes.
            // Keep both paths explicit: the enum projection becomes moved while
            // the binding remains a live owner that can itself be moved/dropped.
            emit_place_path(payload_place, binding.label, payload_type, binding.range);
            emit({MirOpcode::storage_live, {}, payload_type, {payload_place}, binding.range});
            places_[binding.label] = payload_place;
            types_[binding.label] = payload_type;
            bound_names.push_back(binding.label);
            if (needs_drop(payload_type)) {
              const auto payload_path = enum_payload_path(
                  subject_path, variant_name, binding_index - 1);
              mark_runtime_moved(payload_path, binding.range);
              register_drop_flags(payload_type, binding.label, binding.range, true);
              drop_scopes_.back().push_back(
                  {binding.label, payload_type, payload_place, binding.range});
            }
          }
        }
      }
      lower_statement(arm.children.back());
      if (!terminated()) emit_drops_from(drop_scopes_.size() - 1);
      for (const auto& local : drop_scopes_.back()) {
        moved_locals_.erase(local.name);
        erase_drop_flags(local.name);
      }
      drop_scopes_.pop_back();
      for (const auto& name : bound_names) { places_.erase(name); types_.erase(name); }
      if (!terminated()) { emit({MirOpcode::jump, {}, {}, {merge_name}, arm.range}); has_fallthrough = true; }
      test_block = next_test;
    }

    current_block_ = test_block;
    if (wildcard_block.has_value()) {
      emit({MirOpcode::jump, {}, {}, {block_name(*wildcard_block)}, node.range});
      const auto& wildcard = *std::find_if(node.children.begin() + 1, node.children.end(),
          [](const SyntaxNode& arm) { return arm.kind == SyntaxKind::match_arm && arm.label == "_"; });
      current_block_ = *wildcard_block;
      if (!wildcard.children.empty()) lower_statement(wildcard.children.back());
      if (!terminated()) { emit({MirOpcode::jump, {}, {}, {merge_name}, wildcard.range}); has_fallthrough = true; }
    } else {
      emit({MirOpcode::unreachable, {}, {}, {}, node.range});
    }
    current_block_ = merge_block;
    if (!has_fallthrough) emit({MirOpcode::unreachable, {}, {}, {}, node.range});
  }

  void lower_statement(const SyntaxNode& node) {
    if (terminated()) return;
    if (node.kind == SyntaxKind::block_statement) {
      defer_scopes_.emplace_back();
      drop_scopes_.emplace_back();
      for (const auto& child : node.children) lower_statement(child);
      if (!terminated()) {
        emit_deferred_from(defer_scopes_.size() - 1);
        emit_drops_from(drop_scopes_.size() - 1);
      }
      for (const auto& local : drop_scopes_.back()) {
        moved_locals_.erase(local.name);
        erase_drop_flags(local.name);
      }
      drop_scopes_.pop_back();
      defer_scopes_.pop_back();
      return;
    }
    if (node.kind == SyntaxKind::comptime_statement) return;
    if (node.kind == SyntaxKind::const_declaration) {
      if (node.children.empty()) return;
      const auto separator = node.label.rfind(' ');
      const auto name = separator == std::string::npos ? node.label : node.label.substr(separator + 1);
      local_constants_[name] = lower_expression(node.children.front());
      return;
    }
    if (node.kind == SyntaxKind::variable_declaration) {
      const auto separator = node.label.rfind(' ');
      auto type = separator == std::string::npos ? node.label : node.label.substr(0, separator);
      const auto name = separator == std::string::npos ? std::string{} : node.label.substr(separator + 1);
      if (type == "auto") {
        const auto local = std::find_if(hir_.locals.begin(), hir_.locals.end(), [&](const HirVariable& value) {
          return value.name == name;
        });
        if (local != hir_.locals.end()) type = local->type_name;
      }
      types_[name] = type;
      if (!node.children.empty() && node.children.front().kind == SyntaxKind::closure_expression &&
          !parse_function_type(type) && !parse_callable_type(type)) {
        const auto& closure = node.children.front();
        const auto arrow = closure.label.find(" -> ");
        ClosureBinding binding;
        binding.function_name = arrow == std::string::npos ? closure.label : closure.label.substr(0, arrow);
        binding.move_capture = closure.modifier == "move";
        binding.callable_kind = binding.move_capture ? "FnOnce" : (closure.modifier == "mut" ? "FnMut" : "Fn");
        binding.borrowed_capture = closure.modifier == "ref" || closure.modifier == "mut";
        binding.mutable_capture = closure.modifier == "mut";
        for (const auto& capture : closure_capture_names(closure)) {
          SyntaxNode capture_node;
          capture_node.kind = SyntaxKind::name_expression;
          capture_node.label = capture;
          capture_node.range = node.range;
          const auto capture_type = types_.contains(capture) ? types_.at(capture) : expression_type(capture_node);
          if (binding.borrowed_capture) {
            const auto [capture_place, capture_place_type] = lower_place(capture_node);
            binding.capture_values.push_back(capture_place);
            binding.capture_types.push_back(capture_place_type + (binding.mutable_capture ? "&mut" : "&"));
          } else {
            const auto source = lower_expression(capture_node);
            if (is_aggregate(capture_type)) {
              const auto snapshot = temporary();
              emit({MirOpcode::stack_allocate, snapshot, capture_type,
                    {std::to_string(type_size(capture_type)), std::to_string(type_alignment(capture_type))}, node.range});
              copy_aggregate(capture_type, source, snapshot, node.range);
              binding.capture_values.push_back(snapshot);
            } else {
              binding.capture_values.push_back(source);
            }
            binding.capture_types.push_back(capture_type);
          }
          const auto drop_name = name + ".capture." + capture;
          binding.capture_drop_names.push_back(drop_name);
          if (binding.move_capture) {
            mark_runtime_moved(capture, closure.range);
            if (needs_drop(capture_type) && !drop_scopes_.empty()) {
              drop_scopes_.back().push_back({drop_name, capture_type, binding.capture_values.back(), node.range});
            }
          }
        }
        closure_bindings_[name] = std::move(binding);
        return;
      }
      if (const auto dynamic_trait = parse_dynamic_trait_type(type)) {
        const auto slot = temporary();
        emit({MirOpcode::stack_allocate, slot, type, {"8", "8"}, node.range});
        places_[name] = slot;
        emit_place_path(slot, name, type, node.range);
        if (!node.children.empty()) {
          const auto initializer_type = expression_type(node.children.front());
          if (initializer_type == type) {
            const auto object = lower_expression(node.children.front());
            emit({MirOpcode::store, {}, type, {object, slot}, node.range});
          } else {
            const auto reference = parse_reference_type(initializer_type);
            if (!reference) {
              diagnostics_.error("D3033", node.children.front().range,
                                 "dynamic trait object lowering requires a reference or compatible object initializer");
            } else {
              const auto vtable = dynamic_vtables_.find(reference->referent_type + "::" + dynamic_trait->trait_name);
              if (vtable == dynamic_vtables_.end()) {
                diagnostics_.error("D3034", node.children.front().range, "dynamic trait vtable is unavailable");
              } else {
                std::vector<std::string> operands{lower_expression(node.children.front()), reference->referent_type, dynamic_trait->trait_name};
                operands.insert(operands.end(), vtable->second.begin(), vtable->second.end());
                const auto object = temporary();
                emit({MirOpcode::trait_object_create, object, type, std::move(operands), node.range});
                emit({MirOpcode::store, {}, type, {object, slot}, node.range});
              }
            }
          }
        }
      } else if (is_aggregate(type)) {
        const auto aggregate = temporary();
        emit({MirOpcode::stack_allocate, aggregate, type,
              {std::to_string(type_size(type)), std::to_string(type_alignment(type))}, node.range});
        places_[name] = aggregate;
        emit_place_path(aggregate, name, type, node.range);
        if (!node.children.empty()) initialize_place(type, aggregate, node.children.front());
      } else {
        const auto slot = temporary();
        emit({MirOpcode::stack_allocate, slot, type,
              {std::to_string(type_size(type)), std::to_string(type_alignment(type))}, node.range});
        places_[name] = slot;
        emit_place_path(slot, name, type, node.range);
        if (!node.children.empty()) {
          auto value = lower_expression(node.children.front());
          value = coerce_implicit_numeric(value, expression_type(node.children.front()), type, node.children.front().range);
          emit({MirOpcode::store, {}, type, {value, slot}, node.range});
          if (const auto reference = parse_reference_type(type)) {
            const auto source_path = logical_place_path(node.children.front());
            emit({MirOpcode::borrow_bind, {}, type, {slot, value, source_path, name}, node.range});
          }
        }
      }
      if (needs_drop(type) && !drop_scopes_.empty()) {
        register_drop_flags(type, name, node.range, !node.children.empty());
        drop_scopes_.back().push_back({name, type, places_.at(name), node.range});
      }
      return;
    }
    if (node.kind == SyntaxKind::return_statement) {
      std::string value;
      std::string return_move_path;
      if (!node.children.empty()) {
        auto lowered = lower_expression(node.children.front());
        lowered = coerce_implicit_numeric(lowered, expression_type(node.children.front()),
                                          hir_.return_type, node.children.front().range);
        value = preserve_return_value(hir_.return_type, lowered, node.range);
        // A by-value return of an owning place transfers ownership to the caller.
        // `preserve_return_value` snapshots aggregate bytes into the ABI return
        // slot; suppress Drop of the source place only after defers have run so
        // the existing defer-observation semantics remain unchanged.
        if (needs_drop(hir_.return_type)) {
          return_move_path = logical_place_path(node.children.front());
        }
      }
      emit_deferred_from(0);
      if (!return_move_path.empty()) mark_runtime_moved(return_move_path, node.range);
      emit_drops_from(0);
      if (node.children.empty()) emit({MirOpcode::return_void, {}, {}, {}, node.range});
      else emit({MirOpcode::return_value, {}, hir_.return_type, {value}, node.range});
      return;
    }
    if (node.kind == SyntaxKind::defer_statement) {
      if (node.children.empty()) {
        diagnostics_.error("D3009", node.range, "defer requires a statement");
      } else if (defer_scopes_.empty()) {
        diagnostics_.error("D3010", node.range, "defer requires a lexical scope");
      } else {
        defer_scopes_.back().push_back(&node.children.front());
      }
      return;
    }
    if (node.kind == SyntaxKind::expression_statement && !node.children.empty()) {
      (void)lower_expression(node.children.front());
      return;
    }
    if (node.kind == SyntaxKind::if_statement) {
      lower_if(node);
      return;
    }
    if (node.kind == SyntaxKind::while_statement) {
      lower_while(node);
      return;
    }
    if (node.kind == SyntaxKind::match_statement) {
      lower_match(node);
      return;
    }
    if (node.kind == SyntaxKind::break_statement) {
      if (loop_stack_.empty()) diagnostics_.error("D3006", node.range, "break used outside a loop");
      else {
        emit_deferred_from(loop_stack_.back().cleanup_depth);
        emit_drops_from(loop_stack_.back().cleanup_depth);
        emit({MirOpcode::jump, {}, {}, {loop_stack_.back().exit}, node.range});
      }
      return;
    }
    if (node.kind == SyntaxKind::continue_statement) {
      if (loop_stack_.empty()) diagnostics_.error("D3007", node.range, "continue used outside a loop");
      else {
        emit_deferred_from(loop_stack_.back().cleanup_depth);
        emit_drops_from(loop_stack_.back().cleanup_depth);
        emit({MirOpcode::jump, {}, {}, {loop_stack_.back().header}, node.range});
      }
      return;
    }
    if (node.kind == SyntaxKind::for_statement) {
      lower_for(node);
      return;
    }
    for (const auto& child : node.children) lower_statement(child);
  }

  DiagnosticEngine& diagnostics_;
  const HirFunction& hir_;
  MirFunction& mir_;
  const std::unordered_map<std::string, FunctionSignature>& signatures_;
  const std::unordered_map<std::string, AggregateLayout>& layouts_;
  const std::unordered_map<std::string, EnumLayout>& enums_;
  const std::unordered_map<std::string, std::string>& drop_functions_;
  const std::unordered_map<std::string, std::string>& clone_functions_;
  const std::unordered_map<std::string, std::string>& trait_methods_;
  const std::unordered_map<std::string, DynamicMethodLayout>& dynamic_methods_;
  const std::unordered_map<std::string, std::vector<std::string>>& dynamic_vtables_;
  const std::unordered_map<std::string, std::string>& associated_types_;
  const std::unordered_set<std::string>& copy_types_;
  const std::unordered_map<std::string, std::pair<std::string, std::string>>& constants_;
  std::unordered_map<std::string, std::string> local_constants_;
  struct ClosureBinding final {
    std::string function_name;
    std::vector<std::string> capture_values;
    std::vector<std::string> capture_types;
    std::vector<std::string> capture_drop_names;
    std::string callable_kind = "Fn";
    bool move_capture = false;
    bool borrowed_capture = false;
    bool mutable_capture = false;
    bool consumed = false;
  };
  std::unordered_map<std::string, std::string> places_;
  std::unordered_map<std::string, std::string> types_;
  std::unordered_map<std::string, ClosureBinding> closure_bindings_;
  struct LoopContext final {
    std::string header;
    std::string exit;
    std::size_t cleanup_depth = 0;
  };
  std::vector<LoopContext> loop_stack_;
  std::vector<std::vector<const SyntaxNode*>> defer_scopes_;
  std::vector<std::vector<DropLocal>> drop_scopes_;
  std::unordered_set<std::string> moved_locals_;
  std::unordered_map<std::string, std::string> drop_flags_;
  bool emitting_defer_ = false;
  std::size_t current_block_ = 0;
  std::size_t next_value_ = 0;
  std::size_t next_block_ = 0;
};

}  // namespace
