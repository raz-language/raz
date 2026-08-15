// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#ifndef FORGE_C_FORGE_H
#define FORGE_C_FORGE_H

#define FORGE_C_API_VERSION 14

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct forge_context forge_context_t;
typedef struct forge_module forge_module_t;
typedef struct forge_function forge_function_t;
typedef struct forge_block forge_block_t;
typedef struct forge_builder forge_builder_t;

typedef enum forge_type_kind {
    FORGE_TYPE_VOID, FORGE_TYPE_I1, FORGE_TYPE_I8, FORGE_TYPE_I16, FORGE_TYPE_I32,
    FORGE_TYPE_I64, FORGE_TYPE_F32, FORGE_TYPE_F64, FORGE_TYPE_PTR
} forge_type_kind_t;



typedef enum forge_aggregate_ref_kind {
    FORGE_AGGREGATE_SCALAR,
    FORGE_AGGREGATE_STRUCT,
    FORGE_AGGREGATE_ARRAY
} forge_aggregate_ref_kind_t;

typedef enum forge_borrow_mode {
    FORGE_BORROW_NONE,
    FORGE_BORROW_IMMUTABLE,
    FORGE_BORROW_MUTABLE
} forge_borrow_mode_t;

typedef enum forge_calling_convention {
    FORGE_CALL_PLATFORM,
    FORGE_CALL_C,
    FORGE_CALL_SYSTEM_V,
    FORGE_CALL_WINDOWS_X64,
    FORGE_CALL_FAST
} forge_calling_convention_t;

typedef enum forge_symbol_linkage {
    FORGE_LINKAGE_EXTERNAL,
    FORGE_LINKAGE_INTERNAL,
    FORGE_LINKAGE_WEAK
} forge_symbol_linkage_t;

typedef enum forge_symbol_visibility {
    FORGE_VISIBILITY_DEFAULT,
    FORGE_VISIBILITY_HIDDEN
} forge_symbol_visibility_t;

typedef enum forge_native_abi {
    FORGE_ABI_SYSTEM_V_X86_64,
    FORGE_ABI_WINDOWS_X64
} forge_native_abi_t;

typedef enum forge_optimization_level {
    FORGE_OPT_O0,
    FORGE_OPT_O1,
    FORGE_OPT_O2,
    FORGE_OPT_O3,
    FORGE_OPT_OS,
    FORGE_OPT_OZ
} forge_optimization_level_t;

typedef enum forge_diagnostic_severity {
    FORGE_DIAGNOSTIC_NOTE,
    FORGE_DIAGNOSTIC_WARNING,
    FORGE_DIAGNOSTIC_ERROR
} forge_diagnostic_severity_t;

typedef enum forge_opcode {
    FORGE_OPCODE_ADD, FORGE_OPCODE_SUBTRACT, FORGE_OPCODE_MULTIPLY,
    FORGE_OPCODE_DIVIDE_SIGNED, FORGE_OPCODE_DIVIDE_UNSIGNED,
    FORGE_OPCODE_REMAINDER_SIGNED, FORGE_OPCODE_REMAINDER_UNSIGNED,
    FORGE_OPCODE_AND, FORGE_OPCODE_OR, FORGE_OPCODE_XOR,
    FORGE_OPCODE_SHIFT_LEFT, FORGE_OPCODE_SHIFT_RIGHT_SIGNED, FORGE_OPCODE_SHIFT_RIGHT_UNSIGNED,
    FORGE_OPCODE_COMPARE_EQUAL, FORGE_OPCODE_COMPARE_NOT_EQUAL,
    FORGE_OPCODE_COMPARE_LESS_SIGNED, FORGE_OPCODE_COMPARE_LESS_UNSIGNED,
    FORGE_OPCODE_COMPARE_LESS_EQUAL_SIGNED, FORGE_OPCODE_COMPARE_LESS_EQUAL_UNSIGNED
} forge_opcode_t;

forge_context_t* forge_context_create(void);
void forge_context_destroy(forge_context_t* context);
forge_module_t* forge_module_create(forge_context_t* context, const char* name);
forge_module_t* forge_module_parse(forge_context_t* context, const char* source, size_t source_length);
void forge_module_destroy(forge_module_t* module);

// Structured module construction. These functions bypass Forge IR text parsing and
// preserve caller-provided names so frontends can keep deterministic IR identities.
size_t forge_module_add_struct(forge_module_t* module, const char* name, int move_only);
int forge_struct_add_field(forge_module_t* module, size_t struct_index,
                           const char* name, forge_type_kind_t type,
                           forge_aggregate_ref_kind_t aggregate_kind,
                           const char* aggregate_name);
size_t forge_module_add_array(forge_module_t* module, const char* name,
                              forge_type_kind_t element_type, uint32_t element_count,
                              int move_only);
size_t forge_module_add_array_ex(forge_module_t* module, const char* name,
                                 forge_type_kind_t element_type,
                                 forge_aggregate_ref_kind_t element_aggregate_kind,
                                 const char* element_aggregate_name,
                                 uint32_t element_count, int move_only);
size_t forge_module_add_global(forge_module_t* module, const char* name,
                               forge_type_kind_t type,
                               forge_aggregate_ref_kind_t aggregate_kind,
                               const char* aggregate_name,
                               const char* function_signature_name,
                               int is_constant, int is_external,
                               forge_symbol_linkage_t linkage,
                               forge_symbol_visibility_t visibility,
                               const char* initializer, uint32_t element_count,
                               uint32_t alignment, int zero_initialized,
                               const uint8_t* bytes, size_t byte_count);
size_t forge_module_add_global_ex(forge_module_t* module, const char* name,
                                  forge_type_kind_t type,
                                  forge_aggregate_ref_kind_t aggregate_kind,
                                  const char* aggregate_name,
                                  const char* function_signature_name,
                                  int is_constant, int is_external, int is_thread_local,
                                  forge_symbol_linkage_t linkage,
                                  forge_symbol_visibility_t visibility,
                                  const char* initializer, uint32_t element_count,
                                  uint32_t alignment, int zero_initialized,
                                  const uint8_t* bytes, size_t byte_count);

forge_function_t* forge_function_create_ex(
    forge_module_t* module, const char* name, forge_type_kind_t return_type,
    forge_aggregate_ref_kind_t return_aggregate_kind, const char* return_aggregate_name,
    int return_owned, forge_borrow_mode_t return_borrow_mode, int32_t return_borrow_parameter,
    int is_external, int is_signature);
int forge_function_add_parameter(forge_function_t* function, const char* name,
                                 forge_type_kind_t type,
                                 forge_aggregate_ref_kind_t aggregate_kind,
                                 const char* aggregate_name, int owned,
                                 forge_borrow_mode_t borrow_mode,
                                 const char* function_signature_name);
int forge_function_set_target_feature(forge_function_t* function, const char* target_feature);

forge_function_t* forge_function_create(forge_module_t* module, const char* name,
                                        forge_type_kind_t return_type,
                                        const forge_type_kind_t* parameter_types,
                                        size_t parameter_count);
void forge_function_destroy(forge_function_t* function);
int forge_function_set_abi(forge_function_t* function,
                           forge_calling_convention_t calling_convention,
                           int variadic,
                           forge_symbol_linkage_t linkage,
                           forge_symbol_visibility_t visibility);
forge_block_t* forge_block_create(forge_function_t* function, const char* name);
forge_block_t* forge_block_create_with_parameters(forge_function_t* function, const char* name,
                                                   const forge_type_kind_t* parameter_types,
                                                   size_t parameter_count);
int forge_block_add_parameter(forge_block_t* block, const char* name, forge_type_kind_t type,
                              forge_aggregate_ref_kind_t aggregate_kind,
                              const char* aggregate_name, int owned,
                              forge_borrow_mode_t borrow_mode,
                              const char* function_signature_name);
// Generic operation construction mirrors forge::ir::Operation without exposing C++ ABI.
// Returns SIZE_MAX on failure.
size_t forge_block_append_operation(forge_block_t* block, const char* result,
                                    const char* opcode, forge_type_kind_t type);
int forge_operation_add_operand(forge_block_t* block, size_t operation_index, const char* operand);
int forge_operation_add_successor(forge_block_t* block, size_t operation_index, const char* successor);
int forge_operation_add_successor_argument(forge_block_t* block, size_t operation_index,
                                           size_t successor_index, const char* argument);
int forge_operation_set_alignment(forge_block_t* block, size_t operation_index, uint32_t alignment);
int forge_operation_set_source_range(forge_block_t* block, size_t operation_index,
                                     const char* file, uint32_t line, uint32_t column,
                                     uint32_t end_line, uint32_t end_column);
int forge_operation_add_attribute(forge_block_t* block, size_t operation_index,
                                  const char* name, const char* value);
void forge_block_destroy(forge_block_t* block);
forge_builder_t* forge_builder_create(forge_context_t* context, forge_module_t* module);
void forge_builder_destroy(forge_builder_t* builder);
void forge_builder_position_at_end(forge_builder_t* builder, forge_block_t* block);
void forge_builder_clear_insertion_point(forge_builder_t* builder);
void forge_builder_set_source_location(forge_builder_t* builder, const char* file,
                                       uint32_t line, uint32_t column);
void forge_builder_set_source_range(forge_builder_t* builder, const char* file,
                                    uint32_t line, uint32_t column,
                                    uint32_t end_line, uint32_t end_column);
void forge_builder_clear_source_location(forge_builder_t* builder);
void forge_module_set_metadata(forge_module_t* module, const char* name, const char* value);
const char* forge_module_get_metadata(const forge_module_t* module, const char* name);
void forge_builder_set_next_attribute(forge_builder_t* builder, const char* name, const char* value);
void forge_builder_clear_next_attributes(forge_builder_t* builder);
const char* forge_function_parameter(const forge_function_t* function, size_t index);
const char* forge_block_parameter(const forge_block_t* block, size_t index);
const char* forge_builder_constant(forge_builder_t* builder, forge_type_kind_t type,
                                   const char* literal);
const char* forge_builder_binary(forge_builder_t* builder, forge_opcode_t opcode,
                                 forge_type_kind_t type, const char* lhs, const char* rhs);
const char* forge_builder_compare(forge_builder_t* builder, forge_opcode_t opcode,
                                  forge_type_kind_t operand_type, const char* lhs, const char* rhs);
const char* forge_builder_stack_alloc(forge_builder_t* builder, uint64_t size, uint32_t alignment);
const char* forge_builder_load(forge_builder_t* builder, forge_type_kind_t type,
                               const char* address, uint32_t alignment);
void forge_builder_store(forge_builder_t* builder, forge_type_kind_t type,
                         const char* value, const char* address, uint32_t alignment);
const char* forge_builder_call(forge_builder_t* builder, forge_type_kind_t return_type,
                               const char* callee, const char* const* arguments,
                               size_t argument_count);
void forge_builder_jump(forge_builder_t* builder, const forge_block_t* destination,
                        const char* const* arguments, size_t argument_count);
void forge_builder_branch(forge_builder_t* builder, const char* condition,
                          const forge_block_t* true_destination,
                          const forge_block_t* false_destination);
void forge_builder_return(forge_builder_t* builder, const char* value);
void forge_builder_unreachable(forge_builder_t* builder);
int forge_module_verify(const forge_module_t* module, char* message, size_t message_capacity);
int forge_module_optimize(forge_module_t* module, forge_optimization_level_t level);
size_t forge_module_emit_object(forge_module_t* module, forge_native_abi_t abi,
                                uint8_t* output, size_t output_capacity);
size_t forge_module_write_object_file(forge_module_t* module, forge_native_abi_t abi,
                                     const char* output_path);
size_t forge_module_diagnostic_count(const forge_module_t* module);
forge_diagnostic_severity_t forge_module_diagnostic_severity(const forge_module_t* module, size_t index);
const char* forge_module_diagnostic_message(const forge_module_t* module, size_t index);
const char* forge_module_diagnostic_file(const forge_module_t* module, size_t index);
uint32_t forge_module_diagnostic_line(const forge_module_t* module, size_t index);
uint32_t forge_module_diagnostic_column(const forge_module_t* module, size_t index);
uint32_t forge_module_diagnostic_end_line(const forge_module_t* module, size_t index);
uint32_t forge_module_diagnostic_end_column(const forge_module_t* module, size_t index);
int forge_builder_has_insertion_point(const forge_builder_t* builder);
int forge_builder_insertion_block_terminated(const forge_builder_t* builder);
size_t forge_module_print(const forge_module_t* module, char* output, size_t output_capacity);
size_t forge_module_source_map_json(const forge_module_t* module, char* output, size_t output_capacity);
size_t forge_module_semantic_fingerprint(const forge_module_t* module, char* output, size_t output_capacity);
size_t forge_module_frontend_fingerprint(const forge_module_t* module, char* output, size_t output_capacity);
size_t forge_module_incremental_manifest_json(const forge_module_t* module, char* output, size_t output_capacity);
size_t forge_module_cache_key(const forge_module_t* module, const char* frontend_id,
                              const char* configuration, char* output, size_t output_capacity);
size_t forge_module_incremental_build_plan_json(const forge_module_t* previous_module,
                                                const forge_module_t* current_module,
                                                const char* frontend_id,
                                                const char* configuration,
                                                char* output, size_t output_capacity);
size_t forge_module_parallel_build_schedule_json(const forge_module_t* previous_module,
                                                const forge_module_t* current_module,
                                                const char* frontend_id,
                                                const char* configuration,
                                                size_t requested_workers,
                                                char* output, size_t output_capacity);
size_t forge_module_function_abi_json(const forge_module_t* module,
                                      const char* function_name,
                                      forge_native_abi_t abi,
                                      char* output, size_t output_capacity);
size_t forge_module_dependency_build_schedule_json(const forge_module_t* previous_module,
                                                  const forge_module_t* current_module,
                                                  const char* frontend_id,
                                                  const char* configuration,
                                                  size_t requested_workers,
                                                  char* output, size_t output_capacity);
const char* forge_last_error(void);
void forge_clear_error(void);

#ifdef __cplusplus
}
#endif
#endif
