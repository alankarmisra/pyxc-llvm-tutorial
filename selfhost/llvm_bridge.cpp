#include "llvm_bridge.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include <cstring>

using namespace llvm;

static Type* infer_pointee_type(Value* ptr) {
    if (auto* alloca_inst = dyn_cast<AllocaInst>(ptr)) {
        return alloca_inst->getAllocatedType();
    }
    if (auto* gep_inst = dyn_cast<GetElementPtrInst>(ptr)) {
        return gep_inst->getResultElementType();
    }
    if (auto* global_var = dyn_cast<GlobalVariable>(ptr)) {
        return global_var->getValueType();
    }
    if (auto* bitcast_inst = dyn_cast<BitCastInst>(ptr)) {
        return infer_pointee_type(bitcast_inst->getOperand(0));
    }
    if (auto* addrspace_cast_inst = dyn_cast<AddrSpaceCastInst>(ptr)) {
        return infer_pointee_type(addrspace_cast_inst->getOperand(0));
    }
    return nullptr;
}

//===----------------------------------------------------------------------===//
// Context and Module Management
//===----------------------------------------------------------------------===//

extern "C" void* llvm_create_context() {
    return new LLVMContext();
}

extern "C" void* llvm_create_module(void* ctx, const char* name) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return new Module(name, *context);
}

extern "C" void* llvm_create_builder(void* ctx) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return new IRBuilder<>(*context);
}

extern "C" void llvm_dispose_context(void* ctx) {
    delete static_cast<LLVMContext*>(ctx);
}

extern "C" void llvm_dispose_module(void* mod) {
    delete static_cast<Module*>(mod);
}

//===----------------------------------------------------------------------===//
// Type Creation
//===----------------------------------------------------------------------===//

extern "C" void* llvm_i1_type(void* ctx) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return Type::getInt1Ty(*context);
}

extern "C" void* llvm_i8_type(void* ctx) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return Type::getInt8Ty(*context);
}

extern "C" void* llvm_i16_type(void* ctx) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return Type::getInt16Ty(*context);
}

extern "C" void* llvm_i32_type(void* ctx) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return Type::getInt32Ty(*context);
}

extern "C" void* llvm_i64_type(void* ctx) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return Type::getInt64Ty(*context);
}

extern "C" void* llvm_f32_type(void* ctx) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return Type::getFloatTy(*context);
}

extern "C" void* llvm_f64_type(void* ctx) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return Type::getDoubleTy(*context);
}

extern "C" void* llvm_ptr_type(void* ctx) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return PointerType::getUnqual(*context);
}

extern "C" void* llvm_void_type(void* ctx) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return Type::getVoidTy(*context);
}

extern "C" void* llvm_array_type(void* elem_type, uint64_t count) {
    auto* type = static_cast<Type*>(elem_type);
    return ArrayType::get(type, count);
}

extern "C" void* llvm_struct_type(void* ctx, void** field_types, int num_fields,
                                  int packed, const char* name) {
    auto* context = static_cast<LLVMContext*>(ctx);
    std::vector<Type*> fields;
    for (int i = 0; i < num_fields; i++) {
        fields.push_back(static_cast<Type*>(field_types[i]));
    }

    if (name && name[0]) {
        return StructType::create(*context, fields, name, packed != 0);
    } else {
        return StructType::get(*context, fields, packed != 0);
    }
}

extern "C" void* llvm_function_type(void* return_type, void** param_types,
                                    int num_params, int vararg) {
    auto* ret_ty = static_cast<Type*>(return_type);
    std::vector<Type*> params;
    for (int i = 0; i < num_params; i++) {
        params.push_back(static_cast<Type*>(param_types[i]));
    }
    return FunctionType::get(ret_ty, params, vararg != 0);
}

//===----------------------------------------------------------------------===//
// Constants
//===----------------------------------------------------------------------===//

extern "C" void* llvm_const_i32(void* ctx, int32_t val) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return ConstantInt::get(*context, APInt(32, val, true));
}

extern "C" void* llvm_const_i64(void* ctx, int64_t val) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return ConstantInt::get(*context, APInt(64, val, true));
}

extern "C" void* llvm_const_f64(void* ctx, double val) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return ConstantFP::get(*context, APFloat(val));
}

extern "C" void* llvm_const_null(void* type) {
    auto* ty = static_cast<Type*>(type);
    return Constant::getNullValue(ty);
}

//===----------------------------------------------------------------------===//
// Function Creation
//===----------------------------------------------------------------------===//

extern "C" void* llvm_create_function(void* module, const char* name, void* fn_type) {
    auto* mod = static_cast<Module*>(module);
    auto* fty = static_cast<FunctionType*>(fn_type);
    return Function::Create(fty, Function::ExternalLinkage, name, mod);
}

extern "C" void* llvm_get_param(void* func, int idx) {
    auto* fn = static_cast<Function*>(func);
    if (idx < 0 || idx >= (int)fn->arg_size())
        return nullptr;
    return fn->getArg(idx);
}

extern "C" void llvm_set_param_name(void* param, const char* name) {
    auto* arg = static_cast<Argument*>(param);
    arg->setName(name);
}

extern "C" void* llvm_get_named_function(void* module, const char* name) {
    auto* mod = static_cast<Module*>(module);
    return mod->getFunction(name);
}

//===----------------------------------------------------------------------===//
// Basic Block Creation
//===----------------------------------------------------------------------===//

extern "C" void* llvm_create_block(void* ctx, const char* name) {
    auto* context = static_cast<LLVMContext*>(ctx);
    return BasicBlock::Create(*context, name);
}

extern "C" void llvm_append_block(void* func, void* block) {
    auto* fn = static_cast<Function*>(func);
    auto* bb = static_cast<BasicBlock*>(block);
    fn->insert(fn->end(), bb);
}

extern "C" void llvm_position_at_end(void* builder, void* block) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    auto* bb = static_cast<BasicBlock*>(block);
    b->SetInsertPoint(bb);
}

extern "C" void* llvm_get_insert_block(void* builder) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->GetInsertBlock();
}

//===----------------------------------------------------------------------===//
// IR Building - Arithmetic
//===----------------------------------------------------------------------===//

extern "C" void* llvm_build_add(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateAdd(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_sub(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateSub(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_mul(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateMul(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_sdiv(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateSDiv(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_udiv(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateUDiv(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_srem(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateSRem(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_urem(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateURem(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_fadd(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateFAdd(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_fsub(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateFSub(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_fmul(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateFMul(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_fdiv(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateFDiv(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

//===----------------------------------------------------------------------===//
// IR Building - Comparisons
//===----------------------------------------------------------------------===//

extern "C" void* llvm_build_icmp_eq(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateICmpEQ(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_icmp_ne(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateICmpNE(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_icmp_slt(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateICmpSLT(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_icmp_sle(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateICmpSLE(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_icmp_sgt(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateICmpSGT(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_icmp_sge(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateICmpSGE(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_fcmp_olt(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateFCmpOLT(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_fcmp_ole(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateFCmpOLE(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_fcmp_ogt(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateFCmpOGT(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

extern "C" void* llvm_build_fcmp_oge(void* builder, void* lhs, void* rhs, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateFCmpOGE(static_cast<Value*>(lhs), static_cast<Value*>(rhs), name);
}

//===----------------------------------------------------------------------===//
// IR Building - Memory Operations
//===----------------------------------------------------------------------===//

extern "C" void* llvm_build_alloca(void* builder, void* type, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    auto* ty = static_cast<Type*>(type);
    return b->CreateAlloca(ty, nullptr, name);
}

extern "C" void* llvm_build_load(void* builder, void* ptr, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    auto* p = static_cast<Value*>(ptr);
    auto* pointee_ty = infer_pointee_type(p);
    if (!pointee_ty) {
        fprintf(stderr, "llvm_build_load: could not infer pointee type\n");
        return nullptr;
    }
    return b->CreateLoad(pointee_ty, p, name);
}

extern "C" void* llvm_build_store(void* builder, void* val, void* ptr) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateStore(static_cast<Value*>(val), static_cast<Value*>(ptr));
}

extern "C" void* llvm_build_gep(void* builder, void* ptr, void** indices,
                                int num_indices, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    auto* p = static_cast<Value*>(ptr);
    std::vector<Value*> idxs;
    for (int i = 0; i < num_indices; i++) {
        idxs.push_back(static_cast<Value*>(indices[i]));
    }
    auto* pointee_ty = infer_pointee_type(p);
    if (!pointee_ty) {
        fprintf(stderr, "llvm_build_gep: could not infer pointee type\n");
        return nullptr;
    }
    return b->CreateGEP(pointee_ty, p, idxs, name);
}

//===----------------------------------------------------------------------===//
// IR Building - Control Flow
//===----------------------------------------------------------------------===//

extern "C" void* llvm_build_br(void* builder, void* dest) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateBr(static_cast<BasicBlock*>(dest));
}

extern "C" void* llvm_build_cond_br(void* builder, void* cond, void* then_bb, void* else_bb) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateCondBr(static_cast<Value*>(cond),
                           static_cast<BasicBlock*>(then_bb),
                           static_cast<BasicBlock*>(else_bb));
}

extern "C" void* llvm_build_ret(void* builder, void* val) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateRet(static_cast<Value*>(val));
}

extern "C" void* llvm_build_ret_void(void* builder) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateRetVoid();
}

extern "C" void* llvm_build_phi(void* builder, void* type, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    auto* ty = static_cast<Type*>(type);
    return b->CreatePHI(ty, 2, name);
}

extern "C" void llvm_add_phi_incoming(void* phi, void* val, void* block) {
    auto* p = static_cast<PHINode*>(phi);
    p->addIncoming(static_cast<Value*>(val), static_cast<BasicBlock*>(block));
}

//===----------------------------------------------------------------------===//
// IR Building - Function Calls
//===----------------------------------------------------------------------===//

extern "C" void* llvm_build_call(void* builder, void* func, void** args,
                                 int num_args, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    auto* fn = static_cast<Function*>(func);
    std::vector<Value*> arg_vals;
    for (int i = 0; i < num_args; i++) {
        arg_vals.push_back(static_cast<Value*>(args[i]));
    }
    return b->CreateCall(fn, arg_vals, name);
}

//===----------------------------------------------------------------------===//
// IR Building - Type Casts
//===----------------------------------------------------------------------===//

extern "C" void* llvm_build_bitcast(void* builder, void* val, void* type, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateBitCast(static_cast<Value*>(val), static_cast<Type*>(type), name);
}

extern "C" void* llvm_build_zext(void* builder, void* val, void* type, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateZExt(static_cast<Value*>(val), static_cast<Type*>(type), name);
}

extern "C" void* llvm_build_sext(void* builder, void* val, void* type, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateSExt(static_cast<Value*>(val), static_cast<Type*>(type), name);
}

extern "C" void* llvm_build_trunc(void* builder, void* val, void* type, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateTrunc(static_cast<Value*>(val), static_cast<Type*>(type), name);
}

extern "C" void* llvm_build_fpext(void* builder, void* val, void* type, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateFPExt(static_cast<Value*>(val), static_cast<Type*>(type), name);
}

extern "C" void* llvm_build_fptrunc(void* builder, void* val, void* type, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateFPTrunc(static_cast<Value*>(val), static_cast<Type*>(type), name);
}

extern "C" void* llvm_build_sitofp(void* builder, void* val, void* type, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateSIToFP(static_cast<Value*>(val), static_cast<Type*>(type), name);
}

extern "C" void* llvm_build_fptosi(void* builder, void* val, void* type, const char* name) {
    auto* b = static_cast<IRBuilder<>*>(builder);
    return b->CreateFPToSI(static_cast<Value*>(val), static_cast<Type*>(type), name);
}

//===----------------------------------------------------------------------===//
// Module Operations
//===----------------------------------------------------------------------===//

extern "C" int llvm_verify_module(void* module) {
    auto* mod = static_cast<Module*>(module);
    std::string err_str;
    raw_string_ostream err_stream(err_str);
    bool failed = verifyModule(*mod, &err_stream);
    if (failed) {
        fprintf(stderr, "Module verification failed:\n%s\n", err_str.c_str());
    }
    return failed ? 1 : 0;
}

extern "C" void llvm_dump_module(void* module) {
    auto* mod = static_cast<Module*>(module);
    mod->print(errs(), nullptr);
}

extern "C" char* llvm_print_to_string(void* val) {
    auto* v = static_cast<Value*>(val);
    std::string str;
    raw_string_ostream stream(str);
    v->print(stream);
    return strdup(str.c_str());
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

extern "C" int llvm_compile_to_object(void* module, const char* filename) {
    auto* mod = static_cast<Module*>(module);

    // Initialize native target
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    // Get target triple
    std::string target_triple = sys::getDefaultTargetTriple();
    Triple triple(target_triple);
    mod->setTargetTriple(triple);

    // Look up target
    std::string error;
    auto target = TargetRegistry::lookupTarget(target_triple, error);
    if (!target) {
        fprintf(stderr, "Error looking up target: %s\n", error.c_str());
        return 1;
    }

    // Create target machine
    auto cpu = "generic";
    auto features = "";
    TargetOptions opt;
    auto RM = std::optional<Reloc::Model>();
    auto target_machine = target->createTargetMachine(
        triple, cpu, features, opt, RM);

    mod->setDataLayout(target_machine->createDataLayout());

    // Open output file
    std::error_code EC;
    raw_fd_ostream dest(filename, EC, sys::fs::OF_None);
    if (EC) {
        fprintf(stderr, "Could not open file: %s\n", EC.message().c_str());
        return 1;
    }

    // Emit object file
    legacy::PassManager pass;
    auto file_type = CodeGenFileType::ObjectFile;

    if (target_machine->addPassesToEmitFile(pass, dest, nullptr, file_type)) {
        fprintf(stderr, "TargetMachine can't emit a file of this type\n");
        return 1;
    }

    pass.run(*mod);
    dest.flush();

    return 0;
}
