// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <forge-c/forge.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    forge_context_t* context = forge_context_create();
    forge_module_t* module = forge_module_create(context, "installed_c_api");
    forge_function_t* function = forge_function_create(module, "main", FORGE_TYPE_I64, NULL, 0);
    forge_block_t* block = forge_block_create(function, "entry");
    forge_builder_t* builder = forge_builder_create(context, module);
    forge_builder_position_at_end(builder, block);
    const char* value = forge_builder_constant(builder, FORGE_TYPE_I64, "42");
    forge_builder_return(builder, value);
    if (!forge_module_verify(module, NULL, 0)) return 2;
    const size_t required = forge_module_print(module, NULL, 0);
    char* text = (char*)malloc(required);
    if (text == NULL || forge_module_print(module, text, required) != required) return 3;
    puts(text);
    free(text);
    forge_builder_destroy(builder);
    forge_block_destroy(block);
    forge_function_destroy(function);
    forge_module_destroy(module);
    forge_context_destroy(context);
    return 0;
}
