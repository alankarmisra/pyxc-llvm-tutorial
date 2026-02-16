#include "llvm_bridge.h"
#include <stdio.h>
#include <stdlib.h>

// Test program to verify LLVM bridge works correctly
// Creates a simple "add" function: i32 add(i32 %a, i32 %b) { return a + b; }

int main() {
    printf("=== LLVM Bridge Test ===\n\n");

    // Create LLVM context, module, and builder
    printf("Creating LLVM context...\n");
    void* ctx = llvm_create_context();
    if (!ctx) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    printf("Creating LLVM module...\n");
    void* mod = llvm_create_module(ctx, "test_module");
    if (!mod) {
        fprintf(stderr, "Failed to create module\n");
        return 1;
    }

    printf("Creating IR builder...\n");
    void* builder = llvm_create_builder(ctx);
    if (!builder) {
        fprintf(stderr, "Failed to create builder\n");
        return 1;
    }

    // Create function type: i32(i32, i32)
    printf("\nCreating function type i32(i32, i32)...\n");
    void* i32_ty = llvm_i32_type(ctx);
    void* param_types[2] = {i32_ty, i32_ty};
    void* fn_ty = llvm_function_type(i32_ty, param_types, 2, 0);

    // Create function
    printf("Creating function 'add'...\n");
    void* fn = llvm_create_function(mod, "add", fn_ty);
    if (!fn) {
        fprintf(stderr, "Failed to create function\n");
        return 1;
    }

    // Set parameter names
    void* param_a = llvm_get_param(fn, 0);
    void* param_b = llvm_get_param(fn, 1);
    llvm_set_param_name(param_a, "a");
    llvm_set_param_name(param_b, "b");

    // Create entry basic block
    printf("Creating entry basic block...\n");
    void* entry = llvm_create_block(ctx, "entry");
    llvm_append_block(fn, entry);
    llvm_position_at_end(builder, entry);

    // Build: sum = a + b
    printf("Building add instruction...\n");
    void* sum = llvm_build_add(builder, param_a, param_b, "sum");

    // Build: return sum
    printf("Building return instruction...\n");
    llvm_build_ret(builder, sum);

    // Verify module
    printf("\nVerifying module...\n");
    if (llvm_verify_module(mod)) {
        fprintf(stderr, "Module verification failed!\n");
        return 1;
    }
    printf("Module verification successful!\n");

    // Dump module to see the IR
    printf("\n=== Generated LLVM IR ===\n");
    llvm_dump_module(mod);
    printf("=== End IR ===\n\n");

    // Compile to object file
    printf("Compiling to object file 'test_add.o'...\n");
    if (llvm_compile_to_object(mod, "test_add.o")) {
        fprintf(stderr, "Failed to compile to object file!\n");
        return 1;
    }
    printf("Successfully compiled to test_add.o\n");

    printf("\n=== All tests passed! ===\n");
    printf("\nThe bridge is working correctly. You can now:\n");
    printf("1. Inspect test_add.o with: objdump -d test_add.o\n");
    printf("2. Create a C program that calls add() and link with test_add.o\n");
    printf("3. Start implementing the lexer in pyxc!\n");

    return 0;
}
