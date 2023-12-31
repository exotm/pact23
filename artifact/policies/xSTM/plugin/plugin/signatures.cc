#include "../../common/tm_defines.h"

#include "signatures.h"

using namespace llvm;

// to reduce boilerplate code, we use CREATE_FUNC_1 and CREATE_FUNC_2 to produce
// Module::getOrInsertFunction() calls.  These two macros differ based on the
// number of arguments to getOrInsertFunction()
//
// NB: the 'false' indicates that it is not a varargs function
#define CREATE_FUNC_1(NAME, RETTY, ARGSTY1)                                    \
  cast<Function>(                                                              \
      M.getOrInsertFunction(NAME, FunctionType::get(RETTY, {ARGSTY1}, false))  \
          .getCallee())
#define CREATE_FUNC_2(NAME, RETTY, ARGSTY1, ARGSTY2)                           \
  cast<Function>(                                                              \
      M.getOrInsertFunction(                                                   \
           NAME, FunctionType::get(RETTY, {ARGSTY1, ARGSTY2}, false))          \
          .getCallee())

/// Initialize the signatures object by creating Type* objects that can be
/// reused throughout the plugin, and by inserting extern Function declarations
/// into the Module for any TM function we might ever need to call.
void signatures::init(Module &M) {
  // Create references to the common types in C/C++ programs
  types[I8] = Type::getInt8Ty(M.getContext());
  types[I16] = Type::getInt16Ty(M.getContext());
  types[I32] = Type::getInt32Ty(M.getContext());
  types[I64] = Type::getInt64Ty(M.getContext());
  types[F32] = Type::getFloatTy(M.getContext());
  types[F64] = Type::getDoubleTy(M.getContext());
  types[F128] = Type::getX86_FP80Ty(M.getContext());
  types[VOID] = Type::getVoidTy(M.getContext());
  // TODO: will the next change after LLVM's opaque pointer transition?
  types[PTR] = PointerType::getUnqual(types[I8]);

  // create the load functions: each has a single parameter T*, and returns T
  loads[U1] = CREATE_FUNC_1(TM_LOAD_U1_STR, types[I8], types[PTR]);
  loads[U2] = CREATE_FUNC_1(TM_LOAD_U2_STR, types[I16], types[PTR]);
  loads[U4] = CREATE_FUNC_1(TM_LOAD_U4_STR, types[I32], types[PTR]);
  loads[U8] = CREATE_FUNC_1(TM_LOAD_U8_STR, types[I64], types[PTR]);
  loads[F] = CREATE_FUNC_1(TM_LOAD_F_STR, types[F32], types[PTR]);
  loads[D] = CREATE_FUNC_1(TM_LOAD_D_STR, types[F64], types[PTR]);
  loads[LD] = CREATE_FUNC_1(TM_LOAD_LD_STR, types[F128], types[PTR]);
  loads[P] = CREATE_FUNC_1(TM_LOAD_P_STR, types[PTR], types[PTR]);

  // create the store functions.  each takes T and T*, returns void
  stores[U1] =
      CREATE_FUNC_2(TM_STORE_U1_STR, types[VOID], {types[I8], types[PTR]});
  stores[U2] =
      CREATE_FUNC_2(TM_STORE_U2_STR, types[VOID], {types[I16], types[PTR]});
  stores[U4] =
      CREATE_FUNC_2(TM_STORE_U4_STR, types[VOID], {types[I32], types[PTR]});
  stores[U8] =
      CREATE_FUNC_2(TM_STORE_U8_STR, types[VOID], {types[I64], types[PTR]});
  stores[F] =
      CREATE_FUNC_2(TM_STORE_F_STR, types[VOID], {types[F32], types[PTR]});
  stores[D] =
      CREATE_FUNC_2(TM_STORE_D_STR, types[VOID], {types[F64], types[PTR]});
  stores[LD] =
      CREATE_FUNC_2(TM_STORE_LD_STR, types[VOID], {types[F128], types[PTR]});
  stores[P] =
      CREATE_FUNC_2(TM_STORE_P_STR, types[VOID], {types[PTR], types[PTR]});

  // create malloc, aligned_alloc, free, memcpy, memset and memmove functions
  this->funcs[MALLOC] = CREATE_FUNC_1(TM_MALLOC_STR, types[PTR], types[I64]);
  this->funcs[ALIGNED_ALLOC] =
      CREATE_FUNC_2(TM_ALIGNED_ALLOC_STR, types[PTR], {types[I64], types[I64]});
  this->funcs[FREE] = CREATE_FUNC_1(TM_FREE_STR, types[VOID], types[PTR]);
  this->funcs[MEMCPY] = cast<Function>(
      M.getOrInsertFunction(
           TM_MEMCPY_STR,
           FunctionType::get(types[PTR],
                             {types[PTR], types[PTR], types[I64], types[I32]},
                             false))
          .getCallee());
  this->funcs[MEMSET] = cast<Function>(
      M.getOrInsertFunction(
           TM_MEMSET_STR,
           FunctionType::get(types[PTR],
                             {types[PTR], types[I8], types[I64], types[I32]},
                             false))
          .getCallee());
  this->funcs[MEMMOVE] = cast<Function>(
      M.getOrInsertFunction(
           TM_MEMMOVE_STR,
           FunctionType::get(types[PTR], {types[PTR], types[PTR], types[I64]},
                             false))
          .getCallee());

  // create the call for translating function pointers
  funcs[TRANSLATE] =
      CREATE_FUNC_1(TM_TRANSLATE_CALL_STR, types[PTR], types[PTR]);

  // create the call for forcing a transaction to become irrevocable.
  funcs[UNSAFE] = cast<Function>(
      M.getOrInsertFunction(
           TM_UNSAFE_STR,
           FunctionType::get(Type::getVoidTy(M.getContext()), false))
          .getCallee());

  // The signature for the *internal* c-api execution function is complex, and
  // not necessarily needed. To simplify, we make the signature only if we can
  // find the *external* c-api execution function
  //
  // TODO: it's probably simpler given the opaque pointer transition
  if (Function *OriginalFunc = M.getFunction(TM_EXECUTE_C_STR)) {
    // Create arg types of OriginalFunc
    std::vector<Type *> arg_types;
    for (const Argument &arg : OriginalFunc->args())
      arg_types.push_back(arg.getType());
    // Duplicate the (first) function parameter and add it as an extra parameter
    Type *ArgTy = arg_types[0];
    arg_types.push_back(ArgTy);
    // Create the new function type and function
    FunctionType *FuncTy = FunctionType::get(
        OriginalFunc->getFunctionType()->getReturnType(), arg_types,
        OriginalFunc->getFunctionType()->isVarArg());
    funcs[CAPI] =
        cast<Function>(M.getOrInsertFunction(TM_EXECUTE_C_INTERNAL_STR, FuncTy,
                                             OriginalFunc->getAttributes())
                           .getCallee());
  }
}

/// Helper function to map a type to a vartype, to simplify array indexing
///
/// TODO: The use of x86_fp80ty means that this is actually hard-coded for x86
int signatures::type_to_vartype(Type *t) {
  if (t->isPointerTy())
    return P;
  if (t->getPrimitiveSizeInBits() == 8 && t->isIntegerTy())
    return U1;
  else if (t->getPrimitiveSizeInBits() == 16 && t->isIntegerTy())
    return U2;
  else if (t->getPrimitiveSizeInBits() == 32 && t->isIntegerTy())
    return U4;
  else if (t->getPrimitiveSizeInBits() == 64 && t->isIntegerTy())
    return U8;
  else if (t->getPrimitiveSizeInBits() == 32 && t->isFloatTy())
    return F;
  else if (t->getPrimitiveSizeInBits() == 64 && t->isDoubleTy())
    return D;
  else if (t->isX86_FP80Ty())
    return LD;
  return -1;
}