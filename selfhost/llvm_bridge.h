#ifndef LLVM_BRIDGE_H
#define LLVM_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//===----------------------------------------------------------------------===//
// LLVM Bridge - C interface to LLVM C++ API
//===----------------------------------------------------------------------===//
// All LLVM objects are represented as opaque pointers (void*)
// This allows pyxc to work with LLVM without understanding C++ classes

//===----------------------------------------------------------------------===//
// Context and Module Management
//===----------------------------------------------------------------------===//

// Create a new LLVM context
void* llvm_create_context(void);

// Create a new LLVM module with given name
void* llvm_create_module(void* ctx, const char* name);

// Create a new IR builder
void* llvm_create_builder(void* ctx);

// Clean up (optional - for manual cleanup)
void llvm_dispose_context(void* ctx);
void llvm_dispose_module(void* mod);

//===----------------------------------------------------------------------===//
// Type Creation
//===----------------------------------------------------------------------===//

// Integer types
void* llvm_i1_type(void* ctx);
void* llvm_i8_type(void* ctx);
void* llvm_i16_type(void* ctx);
void* llvm_i32_type(void* ctx);
void* llvm_i64_type(void* ctx);

// Floating point types
void* llvm_f32_type(void* ctx);
void* llvm_f64_type(void* ctx);

// Pointer type (opaque pointer in LLVM 15+)
void* llvm_ptr_type(void* ctx);

// Void type
void* llvm_void_type(void* ctx);

// Array type
void* llvm_array_type(void* elem_type, uint64_t count);

// Struct type (packed if packed != 0)
void* llvm_struct_type(void* ctx, void** field_types, int num_fields,
                       int packed, const char* name);

// Function type (vararg if vararg != 0)
void* llvm_function_type(void* return_type, void** param_types,
                         int num_params, int vararg);

//===----------------------------------------------------------------------===//
// Constants
//===----------------------------------------------------------------------===//

void* llvm_const_i32(void* ctx, int32_t val);
void* llvm_const_i64(void* ctx, int64_t val);
void* llvm_const_f64(void* ctx, double val);
void* llvm_const_null(void* type);

//===----------------------------------------------------------------------===//
// Function Creation
//===----------------------------------------------------------------------===//

// Create a function in the module
void* llvm_create_function(void* module, const char* name, void* fn_type);

// Get function parameter by index
void* llvm_get_param(void* func, int idx);

// Set parameter name
void llvm_set_param_name(void* param, const char* name);

// Look up function by name
void* llvm_get_named_function(void* module, const char* name);

//===----------------------------------------------------------------------===//
// Basic Block Creation
//===----------------------------------------------------------------------===//

// Create a basic block (not yet attached to a function)
void* llvm_create_block(void* ctx, const char* name);

// Append a basic block to a function
void llvm_append_block(void* func, void* block);

// Position builder at end of block
void llvm_position_at_end(void* builder, void* block);

// Get current insert block
void* llvm_get_insert_block(void* builder);

//===----------------------------------------------------------------------===//
// IR Building - Arithmetic
//===----------------------------------------------------------------------===//

void* llvm_build_add(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_sub(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_mul(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_sdiv(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_udiv(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_srem(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_urem(void* builder, void* lhs, void* rhs, const char* name);

void* llvm_build_fadd(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_fsub(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_fmul(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_fdiv(void* builder, void* lhs, void* rhs, const char* name);

//===----------------------------------------------------------------------===//
// IR Building - Comparisons
//===----------------------------------------------------------------------===//

void* llvm_build_icmp_eq(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_icmp_ne(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_icmp_slt(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_icmp_sle(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_icmp_sgt(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_icmp_sge(void* builder, void* lhs, void* rhs, const char* name);

void* llvm_build_fcmp_olt(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_fcmp_ole(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_fcmp_ogt(void* builder, void* lhs, void* rhs, const char* name);
void* llvm_build_fcmp_oge(void* builder, void* lhs, void* rhs, const char* name);

//===----------------------------------------------------------------------===//
// IR Building - Memory Operations
//===----------------------------------------------------------------------===//

void* llvm_build_alloca(void* builder, void* type, const char* name);
void* llvm_build_load(void* builder, void* ptr, const char* name);
void* llvm_build_store(void* builder, void* val, void* ptr);
void* llvm_build_gep(void* builder, void* ptr, void** indices,
                     int num_indices, const char* name);

//===----------------------------------------------------------------------===//
// IR Building - Control Flow
//===----------------------------------------------------------------------===//

void* llvm_build_br(void* builder, void* dest);
void* llvm_build_cond_br(void* builder, void* cond, void* then_bb, void* else_bb);
void* llvm_build_ret(void* builder, void* val);
void* llvm_build_ret_void(void* builder);

void* llvm_build_phi(void* builder, void* type, const char* name);
void llvm_add_phi_incoming(void* phi, void* val, void* block);

//===----------------------------------------------------------------------===//
// IR Building - Function Calls
//===----------------------------------------------------------------------===//

void* llvm_build_call(void* builder, void* func, void** args,
                      int num_args, const char* name);

//===----------------------------------------------------------------------===//
// IR Building - Type Casts
//===----------------------------------------------------------------------===//

void* llvm_build_bitcast(void* builder, void* val, void* type, const char* name);
void* llvm_build_zext(void* builder, void* val, void* type, const char* name);
void* llvm_build_sext(void* builder, void* val, void* type, const char* name);
void* llvm_build_trunc(void* builder, void* val, void* type, const char* name);
void* llvm_build_fpext(void* builder, void* val, void* type, const char* name);
void* llvm_build_fptrunc(void* builder, void* val, void* type, const char* name);
void* llvm_build_sitofp(void* builder, void* val, void* type, const char* name);
void* llvm_build_fptosi(void* builder, void* val, void* type, const char* name);

//===----------------------------------------------------------------------===//
// Module Operations
//===----------------------------------------------------------------------===//

// Verify module (returns 0 on success, non-zero on failure)
int llvm_verify_module(void* module);

// Dump module to stderr (for debugging)
void llvm_dump_module(void* module);

// Print value to string (caller must free)
char* llvm_print_to_string(void* val);

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

// Compile module to object file
// Returns 0 on success, non-zero on failure
int llvm_compile_to_object(void* module, const char* filename);

#ifdef __cplusplus
}
#endif

#endif // LLVM_BRIDGE_H
