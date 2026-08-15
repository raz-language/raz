// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0


HirToMirLowering::HirToMirLowering(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

MirModule HirToMirLowering::lower(const HirModule& hir) {
  MirModule module;
  for (const auto& enumeration : hir.enums) {
    const bool tagged = std::any_of(enumeration.variants.begin(), enumeration.variants.end(), [](const HirEnumVariant& variant) { return !variant.payload_types.empty(); });
    if (!tagged) module.enum_types.push_back(enumeration.name);
  }
  std::unordered_map<std::string, FunctionSignature> signatures;
  std::unordered_map<std::string, AggregateLayout> layouts;
  std::unordered_map<std::string, EnumLayout> enums;
  std::unordered_map<std::string, std::string> drop_functions;
  std::unordered_map<std::string, std::string> clone_functions;
  std::unordered_map<std::string, std::string> trait_methods;
  std::unordered_map<std::string, DynamicMethodLayout> dynamic_methods;
  std::unordered_map<std::string, std::vector<std::string>> dynamic_vtables;
  std::unordered_map<std::string, std::string> associated_types;
  std::unordered_set<std::string> copy_types;
  std::unordered_map<std::string, std::pair<std::string, std::string>> constants;
  for (const auto& constant : hir.constants) constants.emplace(constant.name, std::pair{constant.type_name, constant.value});
  std::unordered_map<std::string, std::unordered_map<std::string, std::uint32_t>> trait_slots;
  for (const auto& trait : hir.traits) {
    if (!trait.object_safe) continue;
    for (const auto& method : trait.methods) {
      std::vector<std::string> parameters;
      for (const auto& parameter : method.parameters) parameters.push_back(parameter.type_name);
      dynamic_methods.emplace(trait.name + "::" + method.name,
                              DynamicMethodLayout{method.vtable_slot, method.return_type, std::move(parameters)});
      trait_slots[trait.name].emplace(method.name, method.vtable_slot);
    }
  }

  for (const auto& implementation : hir.trait_implementations) {
    if (implementation.trait_name == "Drop" && !implementation.function_name.empty())
      drop_functions[implementation.target_type] = implementation.function_name;
    if (implementation.trait_name == "Clone" && !implementation.function_name.empty())
      clone_functions[implementation.target_type] = implementation.function_name;
    if (implementation.trait_name == "Copy") copy_types.insert(implementation.target_type);
    if (!implementation.method_name.empty() && implementation.trait_name != "Clone" && implementation.trait_name != "Drop")
      trait_methods.emplace(implementation.target_type + "::" + implementation.method_name, implementation.function_name);
    if (const auto slots = trait_slots.find(implementation.trait_name);
        slots != trait_slots.end() && !implementation.method_name.empty()) {
      const auto slot = slots->second.find(implementation.method_name);
      if (slot != slots->second.end()) {
        auto& vtable = dynamic_vtables[implementation.target_type + "::" + implementation.trait_name];
        if (vtable.size() <= slot->second) vtable.resize(slot->second + 1);
        vtable[slot->second] = implementation.function_name;
      }
    }
  }

  for (const auto& binding : hir.associated_type_bindings) {
    associated_types.emplace(binding.target_type + "::" + binding.trait_name + "::" + binding.name, binding.type_name);
    const auto protocol_alias = [&](std::string_view short_name) {
      return binding.trait_name == short_name || binding.trait_name.ends_with(std::string("__") + std::string(short_name)) ||
             binding.trait_name.ends_with(std::string("::") + std::string(short_name));
    };
    if (protocol_alias("Iterator")) {
      associated_types.emplace(binding.target_type + "::Iterator::" + binding.name, binding.type_name);
    } else if (protocol_alias("IntoIterator")) {
      associated_types.emplace(binding.target_type + "::IntoIterator::" + binding.name, binding.type_name);
    }
  }

  for (const auto& type : hir.types) {
    AggregateLayout layout;
    layout.size = type.size;
    layout.alignment = type.alignment;
    layout.ordered_fields = type.fields;
    for (const auto& field : type.fields) layout.fields.emplace(field.name, field);
    layouts.emplace(type.name, std::move(layout));
    module.aggregate_layouts.push_back(MirAggregateLayout{type.name, type.size, type.alignment});
  }

  for (const auto& enumeration : hir.enums) {
    EnumLayout layout;
    layout.size = enumeration.size;
    layout.alignment = enumeration.alignment;
    layout.payload_offset = enumeration.payload_offset;
    layout.tagged = std::any_of(enumeration.variants.begin(), enumeration.variants.end(),
                                [](const HirEnumVariant& variant) { return !variant.payload_types.empty(); });
    for (const auto& variant : enumeration.variants) {
      layout.variants.emplace(variant.name, EnumVariantLayout{variant.discriminant, variant.payload_types, variant.payload_offsets});
    }
    const bool aggregate_enum = layout.tagged;
    const auto aggregate_size = layout.size;
    const auto aggregate_alignment = layout.alignment;
    enums.emplace(enumeration.name, std::move(layout));
    if (aggregate_enum) module.aggregate_layouts.push_back(MirAggregateLayout{enumeration.name, aggregate_size, aggregate_alignment});
  }
  signatures.emplace("print", FunctionSignature{"void", {"string"}});
  for (const auto& function : hir.functions) {
    if (function.generic_template) continue;
    FunctionSignature signature;
    signature.return_type = function.return_type;
    for (const auto& parameter : function.parameters) signature.parameter_types.push_back(parameter.type_name);
    signatures.emplace(function.name, std::move(signature));
  }
  const std::function<bool(const std::string&)> async_needs_drop = [&](const std::string& type) -> bool {
    if (parse_callable_type(type) || parse_dynamic_trait_type(type)) return true;
    if (copy_types.contains(type) || parse_slice_type(type)) return false;
    if (drop_functions.contains(type) || type == "string") return true;
    if (const auto array = parse_fixed_array_type(type)) return async_needs_drop(array->element_type);
    if (const auto layout = layouts.find(type); layout != layouts.end()) {
      return std::any_of(layout->second.ordered_fields.begin(), layout->second.ordered_fields.end(),
                         [&](const auto& field) { return async_needs_drop(field.type_name); });
    }

    if (const auto enumeration = enums.find(type); enumeration != enums.end() && enumeration->second.tagged) {
      for (const auto& [_, variant] : enumeration->second.variants) {
        if (std::any_of(variant.payload_types.begin(), variant.payload_types.end(), async_needs_drop)) return true;
      }
    }
    return false;
  };
  const std::function<std::uint64_t(const std::string&)> async_type_size = [&](const std::string& type) -> std::uint64_t {
    if (const auto array = parse_fixed_array_type(type)) return async_type_size(array->element_type) * array->length;
    if (const auto enumeration = enums.find(type); enumeration != enums.end()) return std::max<std::uint64_t>(1, enumeration->second.size);
    if (const auto layout = layouts.find(type); layout != layouts.end()) return std::max<std::uint64_t>(1, layout->second.size);
    if (type == "bool" || type == "i8" || type == "u8" || type == "byte") return 1;
    if (type == "i16" || type == "u16") return 2;
    if (type == "i32" || type == "u32" || type == "f32" || type == "char") return 4;
    return 8;
  };
  const auto async_type_alignment = [&](const std::string& type) -> std::uint32_t {
    if (const auto enumeration = enums.find(type); enumeration != enums.end()) return enumeration->second.alignment;
    if (const auto layout = layouts.find(type); layout != layouts.end()) return layout->second.alignment;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(8, async_type_size(type)));
  };
  const auto async_cleanup_symbol = [](const std::string& type) {
    std::string symbol = "__raz_async_cleanup_";
    for (const unsigned char character : type) {
      symbol += std::isalnum(character) != 0 ? static_cast<char>(character) : '_';
    }
    return symbol;
  };
  std::function<std::uint32_t(const std::string&)> async_projection_count;
  async_projection_count = [&](const std::string& type) -> std::uint32_t {
    if (!async_needs_drop(type)) return 0;
    if (parse_callable_type(type) || parse_dynamic_trait_type(type)) return 1;
    std::uint64_t count = 0;
    if (const auto layout = layouts.find(type); layout != layouts.end()) {
      for (const auto& field : layout->second.ordered_fields) count += async_projection_count(field.type_name);
    } else if (const auto array = parse_fixed_array_type(type)) {
      count = static_cast<std::uint64_t>(async_projection_count(array->element_type)) * array->length;
    } else if (const auto enumeration = enums.find(type); enumeration != enums.end() && enumeration->second.tagged) {
      for (const auto& [_, variant] : enumeration->second.variants) {
        for (const auto& payload : variant.payload_types) count += async_projection_count(payload);
      }
    } else {
      count = 1;
    }
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(std::numeric_limits<std::uint32_t>::max(), std::max<std::uint64_t>(1, count)));
  };
  std::unordered_set<std::string> async_cleanup_types;

  for (const auto& function : hir.functions) {
    if (function.generic_template) continue;
    MirFunction mir_function;
    mir_function.name = function.name;
    mir_function.return_type = function.return_type;
    mir_function.is_external = function.is_external;
    mir_function.is_async = function.is_async;
    mir_function.external_name = function.external_name;
    mir_function.abi = function.abi;
    mir_function.range = function.range;
    for (const auto& parameter : function.parameters) mir_function.parameters.emplace_back(parameter.type_name, parameter.name);
    module.functions.push_back(std::move(mir_function));
    if (!function.is_external) {
      FunctionLowerer lowerer(diagnostics_, function, module.functions.back(), signatures, layouts, enums,
                              drop_functions, clone_functions, trait_methods, dynamic_methods, dynamic_vtables,
                              associated_types, copy_types, constants);
      lowerer.lower();
      // Escaping move closures own their captured values inside the erased
      // environment. Reuse the compiler-generated whole-value cleanup thunks
      // so environment destruction follows the same recursive Drop semantics
      // as lexical locals and async frame slots.
      for (const auto& block : module.functions.back().blocks) {
        for (const auto& instruction : block.instructions) {
          if (instruction.opcode != MirOpcode::callable_create || instruction.operands.size() < 4) continue;
          constexpr std::size_t metadata_start = 4;
          constexpr std::size_t metadata_width = 7;
          for (std::size_t index = metadata_start; index + metadata_width <= instruction.operands.size(); index += metadata_width) {
            const auto& capture_type = instruction.operands[index];
            const auto& cleanup_symbol = instruction.operands[index + 6];
            if (cleanup_symbol != "-") async_cleanup_types.insert(capture_type);
          }
        }
      }
      mir_analyze_async_frame(module.functions.back());
      mir_infer_borrow_regions(module.functions.back());
      // Every recursively non-Copy value crossing a suspension point transfers
      // ownership to the frame. Erased handles use their permanent runtime
      // destructor; aggregates use a compiler-generated recursive cleanup thunk.
      for (auto& slot : module.functions.back().async_frame) {
        if (!async_needs_drop(slot.type_name)) continue;
        slot.owned = true;
        if (parse_callable_type(slot.type_name)) {
          slot.cleanup_symbol = "raz_rt_callable_destroy_erased";
        } else if (parse_dynamic_trait_type(slot.type_name)) {
          slot.cleanup_symbol = "raz_rt_trait_object_destroy_erased";
        } else {
          slot.cleanup_symbol = async_cleanup_symbol(slot.type_name);
          slot.owned_bytes = layouts.contains(slot.type_name) || enums.contains(slot.type_name) || parse_fixed_array_type(slot.type_name).has_value();
          if (slot.owned_bytes) {
            slot.storage_size = async_type_size(slot.type_name);
            slot.storage_alignment = async_type_alignment(slot.type_name);
            slot.projection_count = async_projection_count(slot.type_name);
          }
          async_cleanup_types.insert(slot.type_name);
        }
      }
      mir_synthesize_async_state_machine(module.functions.back());
    }
  }
  // Generate one whole-value cleanup thunk per owned async aggregate type.
  // The callback receives the stored pointer-sized representation and mirrors
  // normal lexical destruction: user Drop first, then children in reverse order.
  for (const auto& cleanup_type : async_cleanup_types) {
    MirFunction cleanup;
    cleanup.name = async_cleanup_symbol(cleanup_type);
    cleanup.return_type = "void";
    cleanup.parameters.push_back({cleanup_type + "*mut", "value"});
    cleanup.parameters.push_back({"u64*const", "projection_words"});
    cleanup.parameters.push_back({"i64", "projection_word_count"});
    cleanup.blocks.push_back({"entry", {}});
    std::size_t block_index = 0;
    std::size_t next_value = 0;
    std::size_t next_block = 0;
    const auto temporary = [&] { return "cleanup." + std::to_string(next_value++); };
    const auto create_block = [&](const std::string& stem) {
      cleanup.blocks.push_back({stem + "." + std::to_string(next_block++), {}});
      return cleanup.blocks.size() - 1;
    };
    std::function<std::uint32_t(const std::string&)> projection_width;
    projection_width = [&](const std::string& type) -> std::uint32_t {
      if (!async_needs_drop(type)) return 0;
      if (parse_callable_type(type) || parse_dynamic_trait_type(type)) return 1;
      std::uint64_t width = 0;
      if (const auto layout = layouts.find(type); layout != layouts.end()) {
        for (const auto& field : layout->second.ordered_fields) width += projection_width(field.type_name);
      } else if (const auto array = parse_fixed_array_type(type)) {
        width = static_cast<std::uint64_t>(projection_width(array->element_type)) * array->length;
      } else if (const auto enumeration = enums.find(type);
                 enumeration != enums.end() && enumeration->second.tagged) {
        std::vector<const EnumVariantLayout*> variants;
        for (const auto& [_, variant] : enumeration->second.variants) variants.push_back(&variant);
        std::sort(variants.begin(), variants.end(), [](const auto* lhs, const auto* rhs) {
          return lhs->discriminant < rhs->discriminant;
        });
        for (const auto* variant : variants)
          for (const auto& payload : variant->payload_types) width += projection_width(payload);
      }
      return static_cast<std::uint32_t>(std::max<std::uint64_t>(1, width));
    };
    const auto emit_guard = [&](std::uint32_t first, std::uint32_t width, const auto& body) {
      const auto active_block = create_block("cleanup.projection.active");
      const auto mask_block = create_block("cleanup.projection.mask");
      const auto merge_block = create_block("cleanup.projection.end");
      // A zero projection-word count means whole-value destruction. This is
      // used by owning closure environments, while async frames pass an
      // explicit initialized-projection bitmap for partial-move cleanup.
      const auto zero_count = temporary();
      cleanup.blocks[block_index].instructions.push_back(
          {MirOpcode::constant, zero_count, "i64", {"0"}, {}});
      const auto whole_value = temporary();
      cleanup.blocks[block_index].instructions.push_back(
          {MirOpcode::equal, whole_value, "i64", {"projection_word_count", zero_count}, {}});
      cleanup.blocks[block_index].instructions.push_back(
          {MirOpcode::branch, {}, "bool", {whole_value, cleanup.blocks[active_block].name,
                                           cleanup.blocks[mask_block].name}, {}});
      block_index = mask_block;
      const auto first_word = first / 64;
      const auto last_word = (first + width - 1) / 64;
      std::string any_active;
      for (std::uint32_t word = first_word; word <= last_word; ++word) {
        const auto word_place = temporary();
        cleanup.blocks[block_index].instructions.push_back(
            {MirOpcode::pointer_offset, word_place, "u64", {"projection_words", std::to_string(word * 8)}, {}});
        const auto word_value = temporary();
        cleanup.blocks[block_index].instructions.push_back(
            {MirOpcode::load, word_value, "u64", {word_place}, {}});
        const auto local_first = word == first_word ? first % 64 : 0;
        const auto local_last = word == last_word ? (first + width - 1) % 64 : 63;
        const auto local_width = local_last - local_first + 1;
        const auto mask_value = local_width == 64 ? ~std::uint64_t{0}
            : (((std::uint64_t{1} << local_width) - 1) << local_first);
        const auto mask = temporary();
        cleanup.blocks[block_index].instructions.push_back(
            {MirOpcode::constant, mask, "u64", {std::to_string(mask_value)}, {}});
        const auto selected = temporary();
        cleanup.blocks[block_index].instructions.push_back(
            {MirOpcode::bit_and, selected, "u64", {word_value, mask}, {}});
        if (any_active.empty()) any_active = selected;
        else {
          const auto combined = temporary();
          cleanup.blocks[block_index].instructions.push_back(
              {MirOpcode::bit_or, combined, "u64", {any_active, selected}, {}});
          any_active = combined;
        }
      }
      const auto zero = temporary();
      cleanup.blocks[block_index].instructions.push_back({MirOpcode::constant, zero, "u64", {"0"}, {}});
      const auto active = temporary();
      cleanup.blocks[block_index].instructions.push_back(
          {MirOpcode::not_equal, active, "u64", {any_active, zero}, {}});
      cleanup.blocks[block_index].instructions.push_back(
          {MirOpcode::branch, {}, "bool", {active, cleanup.blocks[active_block].name,
                                           cleanup.blocks[merge_block].name}, {}});
      block_index = active_block;
      body();
      if (cleanup.blocks[block_index].instructions.empty() ||
          !mir_is_terminator(cleanup.blocks[block_index].instructions.back().opcode))
        cleanup.blocks[block_index].instructions.push_back(
            {MirOpcode::jump, {}, {}, {cleanup.blocks[merge_block].name}, {}});
      block_index = merge_block;
    };
    std::function<void(const std::string&, const std::string&, std::uint32_t)> emit_cleanup;
    emit_cleanup = [&](const std::string& type, const std::string& place, std::uint32_t first_bit) {
      const auto width = projection_width(type);
      if (const auto destructor = drop_functions.find(type); destructor != drop_functions.end()) {
        emit_guard(first_bit, width, [&] {
          cleanup.blocks[block_index].instructions.push_back(
              {MirOpcode::call, {}, "void", {destructor->second, place}, {}});
        });
      }
      if (parse_callable_type(type)) {
        emit_guard(first_bit, 1, [&] {
          const auto handle = temporary();
          cleanup.blocks[block_index].instructions.push_back({MirOpcode::load, handle, type, {place}, {}});
          cleanup.blocks[block_index].instructions.push_back({MirOpcode::callable_destroy, {}, type, {handle}, {}});
        });
        return;
      }
      if (parse_dynamic_trait_type(type)) {
        emit_guard(first_bit, 1, [&] {
          const auto handle = temporary();
          cleanup.blocks[block_index].instructions.push_back({MirOpcode::load, handle, type, {place}, {}});
          cleanup.blocks[block_index].instructions.push_back({MirOpcode::trait_object_destroy, {}, type, {handle}, {}});
        });
        return;
      }
      if (const auto layout = layouts.find(type); layout != layouts.end()) {
        std::vector<std::uint32_t> starts;
        auto cursor = first_bit;
        for (const auto& field : layout->second.ordered_fields) {
          starts.push_back(cursor);
          cursor += projection_width(field.type_name);
        }
        for (std::size_t index = layout->second.ordered_fields.size(); index > 0; --index) {
          const auto& field = layout->second.ordered_fields[index - 1];
          if (!async_needs_drop(field.type_name)) continue;
          const auto field_place = temporary();
          cleanup.blocks[block_index].instructions.push_back(
              {MirOpcode::pointer_offset, field_place, field.type_name,
               {place, std::to_string(field.offset)}, {}});
          emit_cleanup(field.type_name, field_place, starts[index - 1]);
        }
      } else if (const auto array = parse_fixed_array_type(type)) {
        const auto element_width = projection_width(array->element_type);
        if (element_width != 0) {
          std::uint64_t element_size = async_type_size(array->element_type);
          for (std::uint64_t index = array->length; index > 0; --index) {
            const auto element_place = temporary();
            cleanup.blocks[block_index].instructions.push_back(
                {MirOpcode::pointer_offset, element_place, array->element_type,
                 {place, std::to_string((index - 1) * element_size)}, {}});
            emit_cleanup(array->element_type, element_place,
                         first_bit + static_cast<std::uint32_t>((index - 1) * element_width));
          }
        }
      } else if (const auto enumeration = enums.find(type);
                 enumeration != enums.end() && enumeration->second.tagged) {
        struct VariantBits { const EnumVariantLayout* layout; std::uint32_t first; };
        std::vector<std::pair<std::string, const EnumVariantLayout*>> ordered;
        for (const auto& [name, variant] : enumeration->second.variants) ordered.push_back({name, &variant});
        std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
          return lhs.second->discriminant < rhs.second->discriminant;
        });
        std::vector<VariantBits> variants;
        auto cursor = first_bit;
        for (const auto& [_, variant] : ordered) {
          variants.push_back({variant, cursor});
          for (const auto& payload : variant->payload_types) cursor += projection_width(payload);
        }
        if (!variants.empty()) {
          const auto tag_place = temporary();
          cleanup.blocks[block_index].instructions.push_back(
              {MirOpcode::pointer_offset, tag_place, "i32", {place, "0"}, {}});
          const auto tag = temporary();
          cleanup.blocks[block_index].instructions.push_back({MirOpcode::load, tag, "i32", {tag_place}, {}});
          const auto merge = create_block("cleanup.enum.end");
          for (std::size_t index = 0; index < variants.size(); ++index) {
            const auto payload_block = create_block("cleanup.enum.payload");
            const auto next = index + 1 == variants.size() ? merge : create_block("cleanup.enum.next");
            const auto expected = temporary();
            cleanup.blocks[block_index].instructions.push_back(
                {MirOpcode::constant, expected, "i32", {std::to_string(variants[index].layout->discriminant)}, {}});
            const auto matches = temporary();
            cleanup.blocks[block_index].instructions.push_back(
                {MirOpcode::equal, matches, "i32", {tag, expected}, {}});
            cleanup.blocks[block_index].instructions.push_back(
                {MirOpcode::branch, {}, "bool", {matches, cleanup.blocks[payload_block].name,
                                                 cleanup.blocks[next].name}, {}});
            block_index = payload_block;
            const auto* variant = variants[index].layout;
            std::vector<std::uint32_t> payload_starts;
            auto payload_cursor = variants[index].first;
            for (const auto& payload_type : variant->payload_types) {
              payload_starts.push_back(payload_cursor);
              payload_cursor += projection_width(payload_type);
            }
            for (std::size_t payload_index = variant->payload_types.size(); payload_index > 0; --payload_index) {
              const auto element = payload_index - 1;
              if (!async_needs_drop(variant->payload_types[element])) continue;
              const auto payload_place = temporary();
              cleanup.blocks[block_index].instructions.push_back(
                  {MirOpcode::pointer_offset, payload_place, variant->payload_types[element],
                   {place, std::to_string(enumeration->second.payload_offset + variant->payload_offsets[element])}, {}});
              emit_cleanup(variant->payload_types[element], payload_place, payload_starts[element]);
            }
            cleanup.blocks[block_index].instructions.push_back(
                {MirOpcode::jump, {}, {}, {cleanup.blocks[merge].name}, {}});
            block_index = next;
          }
          block_index = merge;
        }
      }
    };
    emit_cleanup(cleanup_type, "value", 0);
    cleanup.blocks[block_index].instructions.push_back({MirOpcode::return_void, {}, "void", {}, {}});
    module.functions.push_back(std::move(cleanup));
  }

  const auto abi_size = [&](const std::string& type) -> std::uint64_t {
    if (type == "void") return 0;
    if (const auto array = parse_fixed_array_type(type)) {
      const auto element = layouts.find(array->element_type);
      const std::uint64_t element_size = element == layouts.end() ? 8 : std::max<std::uint64_t>(1, element->second.size);
      return element_size * array->length;
    }

    if (const auto enumeration = enums.find(type); enumeration != enums.end()) return enumeration->second.size;
    if (const auto layout = layouts.find(type); layout != layouts.end()) return std::max<std::uint64_t>(1, layout->second.size);
    if (type == "bool" || type == "i8" || type == "u8" || type == "byte") return 1;
    if (type == "i16" || type == "u16") return 2;
    if (type == "i32" || type == "u32" || type == "f32" || type == "char") return 4;
    return 8;
  };
  const auto abi_alignment = [&](const std::string& type) -> std::uint32_t {
    if (type == "void") return 1;
    if (const auto enumeration = enums.find(type); enumeration != enums.end()) return enumeration->second.alignment;
    if (const auto layout = layouts.find(type); layout != layouts.end()) return layout->second.alignment;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(8, abi_size(type)));
  };
  const auto append_descriptor = [&](MirDispatchAbiKind kind, std::string owner, std::string method,
                                     const std::vector<std::string>& parameters, const std::string& result_type,
                                     std::uint32_t vtable_slot = 0) {
    std::string canonical = owner;
    if (!method.empty()) canonical += "::" + method;
    canonical += '(';
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (index != 0) canonical += ',';
      canonical += parameters[index];
    }
    canonical += ")->" + result_type;
    if (std::any_of(module.dispatch_abis.begin(), module.dispatch_abis.end(), [&](const MirDispatchAbi& abi) {
          return abi.canonical_signature == canonical;
        })) return;
    MirDispatchAbi descriptor;
    descriptor.kind = kind;
    descriptor.owner = std::move(owner);
    descriptor.method = std::move(method);
    descriptor.vtable_slot = vtable_slot;
    descriptor.canonical_signature = std::move(canonical);
    descriptor.signature_id = stable_dispatch_id(descriptor.canonical_signature);
    descriptor.result_type = result_type;
    descriptor.result_size = abi_size(result_type);
    descriptor.result_alignment = abi_alignment(result_type);
    std::uint64_t offset = 0;
    for (const auto& parameter : parameters) {
      const auto alignment = abi_alignment(parameter);
      offset = align_up(offset, alignment);
      descriptor.arguments.push_back(MirAbiField{parameter, offset, abi_size(parameter), alignment});
      offset += abi_size(parameter);
      descriptor.argument_alignment = std::max(descriptor.argument_alignment, alignment);
    }
    descriptor.argument_size = align_up(offset, descriptor.argument_alignment);
    module.dispatch_abis.push_back(std::move(descriptor));
  };
  for (const auto& function : module.functions) {
    for (const auto& [parameter_type, _] : function.parameters) {
      if (const auto callable = parse_callable_type(parameter_type)) {
        append_descriptor(MirDispatchAbiKind::callable,
                          callable->kind == CallableKind::shared ? "Fn" : (callable->kind == CallableKind::mutable_call ? "FnMut" : "FnOnce"),
                          {}, callable->parameter_types, callable->return_type);
      }
    }

    for (const auto& block : function.blocks) {
      for (const auto& instruction : block.instructions) {
        if (instruction.opcode != MirOpcode::callable_create && instruction.opcode != MirOpcode::callable_invoke) continue;
        const auto callable = parse_callable_type(instruction.opcode == MirOpcode::callable_create ? instruction.type_name : "");
        if (callable) append_descriptor(MirDispatchAbiKind::callable, callable->kind == CallableKind::shared ? "Fn" : (callable->kind == CallableKind::mutable_call ? "FnMut" : "FnOnce"), {}, callable->parameter_types, callable->return_type);
      }
    }
  }

  for (const auto& [qualified, method] : dynamic_methods) {
    const auto separator = qualified.find("::");
    auto parameters = method.parameter_types;
    if (!parameters.empty() && (parameters.front() == "Self&" || parameters.front() == "Self&mut")) parameters.erase(parameters.begin());
    append_descriptor(MirDispatchAbiKind::trait_method, qualified.substr(0, separator), qualified.substr(separator + 2), parameters, method.return_type, method.slot);
  }

  std::sort(module.dispatch_abis.begin(), module.dispatch_abis.end(), [](const MirDispatchAbi& left, const MirDispatchAbi& right) {
    return left.canonical_signature < right.canonical_signature;
  });

  for (const auto& error : module.verify()) diagnostics_.error("D3000", {}, error);
  return module;
}
