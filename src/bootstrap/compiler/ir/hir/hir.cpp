// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/ir/hir/hir.hpp"

#include <ostream>

namespace raz::compiler {

void HirModule::dump(std::ostream& stream) const {
  stream << "hir.module\n";
  for (const auto& constant : constants) {
    stream << "  const " << constant.type_name << " @" << constant.name << " = " << constant.value << '\n';
  }

  for (const auto& type : types) {
    stream << "  type @" << type.name;
    if (!type.generic_parameters.empty()) { stream << "<"; for (std::size_t i=0;i<type.generic_parameters.size();++i) { if(i) stream << ","; if (i < type.generic_const_types.size() && !type.generic_const_types[i].empty()) stream << "const " << type.generic_const_types[i] << " "; stream << type.generic_parameters[i]; } stream << ">"; }
    if (type.concrete_instantiation) stream << " [instantiated]";
    if (type.representation != "Raz" || type.packed || type.requested_alignment != 0 || !type.attributes.empty()) {
      stream << " repr " << type.representation;
      if (type.packed) stream << " packed";
      if (type.requested_alignment != 0) stream << " requested_align " << type.requested_alignment;
      if (!type.attributes.empty()) {
        stream << " attrs [";
        for (std::size_t index = 0; index < type.attributes.size(); ++index) {
          if (index != 0) stream << ",";
          stream << type.attributes[index];
        }
        stream << "]";
      }
    }
    stream << " size " << type.size << " align " << type.alignment << '\n';
    for (const auto& field : type.fields) {
      stream << "    field " << field.type_name << " %" << field.name << " offset " << field.offset << '\n';
    }
  }

  for (const auto& enumeration : enums) {
    stream << "  enum @" << enumeration.name;
    if (!enumeration.generic_parameters.empty()) { stream << "<"; for (std::size_t i=0;i<enumeration.generic_parameters.size();++i) { if(i) stream << ","; if (i < enumeration.generic_const_types.size() && !enumeration.generic_const_types[i].empty()) stream << "const " << enumeration.generic_const_types[i] << " "; stream << enumeration.generic_parameters[i]; } stream << ">"; }
    if (enumeration.concrete_instantiation) stream << " [instantiated]";
    stream << " repr tagged size " << enumeration.size << " align " << enumeration.alignment
           << " payload_offset " << enumeration.payload_offset << '\n';
    for (const auto& variant : enumeration.variants) {
      stream << "    variant %" << variant.name;
      if (!variant.payload_types.empty()) { stream << "("; for (std::size_t i=0;i<variant.payload_types.size();++i) { if(i) stream << ","; stream << variant.payload_types[i]; } stream << ")"; }
      stream << " = " << variant.discriminant;
      if (!variant.payload_types.empty()) {
        stream << " payload_size " << variant.payload_size << " payload_align " << variant.payload_alignment;
      }
      stream << '\n';
    }
  }

  for (const auto& trait : traits) {
    stream << "  trait @" << trait.name;
    if (!trait.generic_parameters.empty()) {
      stream << "<";
      for (std::size_t index = 0; index < trait.generic_parameters.size(); ++index) {
        if (index != 0) stream << ",";
        stream << trait.generic_parameters[index];
      }
      stream << ">";
    }
    if (!trait.alias_targets.empty()) {
      stream << " = ";
      for (std::size_t index = 0; index < trait.alias_targets.size(); ++index) {
        if (index != 0) stream << "+";
        stream << trait.alias_targets[index];
      }
    } else if (!trait.supertraits.empty()) {
      stream << " : ";
      for (std::size_t index = 0; index < trait.supertraits.size(); ++index) {
        if (index != 0) stream << "+";
        stream << trait.supertraits[index];
      }
    }
    if (trait.object_safe) stream << " [object-safe]";
    stream << '\n';
    for (const auto& associated : trait.associated_types) stream << "    type %" << associated.name << '\n';
    for (const auto& associated : trait.associated_constants) stream << "    const " << associated.type_name << " %" << associated.name << '\n';
    for (const auto& method : trait.methods) {
      stream << "    fn %" << method.name << '(';
      for (std::size_t index = 0; index < method.parameters.size(); ++index) {
        if (index != 0) stream << ", ";
        stream << method.parameters[index].type_name << " %" << method.parameters[index].name;
      }
      stream << ") -> " << method.return_type;
      if (trait.object_safe) stream << " [vtable=" << method.vtable_slot << "]";
      if (method.has_default) stream << " [default]";
      stream << '\n';
    }
  }

  for (const auto& binding : associated_type_bindings) {
    stream << "  impl " << binding.trait_name << " for " << binding.target_type
           << " type " << binding.name << " = " << binding.type_name << '\n';
  }

  for (const auto& binding : associated_const_bindings) {
    stream << "  impl " << binding.trait_name << " for " << binding.target_type
           << " const " << binding.type_name << " " << binding.name << " = " << binding.value << '\n';
  }

  for (const auto& implementation : trait_implementations) {
    if (implementation.trait_name.empty()) stream << "  impl " << implementation.target_type;
    else stream << "  impl " << implementation.trait_name << " for " << implementation.target_type;
    if (!implementation.method_name.empty() && implementation.trait_name != "Clone" && implementation.trait_name != "Drop") stream << " method " << implementation.method_name;
    if (!implementation.function_name.empty()) stream << " using @" << implementation.function_name;
    stream << '\n';
  }

  for (const auto& function : functions) {
    stream << "  fn @" << function.name;
    if (!function.generic_parameters.empty()) {
      stream << "<";
      for (std::size_t index = 0; index < function.generic_parameters.size(); ++index) {
        if (index != 0) stream << ",";
        if (index < function.generic_const_types.size() && !function.generic_const_types[index].empty()) {
          stream << "const " << function.generic_const_types[index] << " ";
        }
        stream << function.generic_parameters[index];
        if (index < function.generic_bounds.size() && !function.generic_bounds[index].empty()) {
          stream << ":";
          for (std::size_t bound_index = 0; bound_index < function.generic_bounds[index].size(); ++bound_index) {
            if (bound_index != 0) stream << "+";
            stream << function.generic_bounds[index][bound_index];
          }
        }
      }
      stream << ">";
    }
    if (function.generic_template) stream << " [template]";
    for (const auto& attribute : function.attributes) stream << " [@" << attribute << "]";
    if (function.concrete_instantiation) stream << " [instantiated]";
    stream << '(';
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      if (index != 0) stream << ", ";
      stream << function.parameters[index].type_name << " %" << function.parameters[index].name;
    }
    stream << ") -> " << function.return_type << '\n';
    for (const auto& local : function.locals) {
      stream << "    local " << local.type_name << " %" << local.name << '\n';
    }
  }
}

}  // namespace raz::compiler
