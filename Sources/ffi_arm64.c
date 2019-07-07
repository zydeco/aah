#include "aah.h"

/* Representation of the procedure call argument marshalling
   state.

   The terse state variable names match the names used in the AARCH64
   PCS. */

struct arg_state
{
  unsigned ngrn;                /* Next general-purpose register number. */
  unsigned nsrn;                /* Next vector register number. */
  size_t nsaa;                  /* Next stack offset. */

#if defined (__APPLE__)
  unsigned allocating_variadic;
#endif
};

/* Initialize a procedure call argument marshalling state.  */
static void
arg_init (struct arg_state *state)
{
  state->ngrn = 0;
  state->nsrn = 0;
  state->nsaa = 0;
#if defined (__APPLE__)
  state->allocating_variadic = 0;
#endif
}

/* A subroutine of is_vfp_type.  Given a structure type, return the type code
   of the first non-structure element.  Recurse for structure elements.
   Return -1 if the structure is in fact empty, i.e. no nested elements.  */

static int is_hfa0 (const ffi_type *ty)
{
  ffi_type **elements = ty->elements;
  int i, ret = -1;

  if (elements != NULL)
    for (i = 0; elements[i]; ++i)
      {
        ret = elements[i]->type;
        if (ret == FFI_TYPE_STRUCT || ret == FFI_TYPE_COMPLEX)
          {
            ret = is_hfa0 (elements[i]);
            if (ret < 0)
              continue;
          }
        break;
      }

  return ret;
}

/* A subroutine of is_vfp_type.  Given a structure type, return true if all
   of the non-structure elements are the same as CANDIDATE.  */

static int is_hfa1 (const ffi_type *ty, int candidate)
{
  ffi_type **elements = ty->elements;
  int i;

  if (elements != NULL)
    for (i = 0; elements[i]; ++i)
      {
        int t = elements[i]->type;
        if (t == FFI_TYPE_STRUCT || t == FFI_TYPE_COMPLEX)
          {
            if (!is_hfa1 (elements[i], candidate))
              return 0;
          }
        else if (t != candidate)
          return 0;
      }

  return 1;
}

/* Determine if TY may be allocated to the FP registers.  This is both an
   fp scalar type as well as an homogenous floating point aggregate (HFA).
   That is, a structure consisting of 1 to 4 members of all the same type,
   where that type is an fp scalar.

   Returns non-zero iff TY is an HFA.  The result is the AARCH64_RET_*
   constant for the type.  */

int is_vfp_type (const ffi_type *ty)
{
  ffi_type **elements;
  int candidate, i;
  size_t size, ele_count;

  /* Quickest tests first.  */
  candidate = ty->type;
  switch (candidate)
    {
    default:
      return 0;
    case FFI_TYPE_FLOAT:
    case FFI_TYPE_DOUBLE:
    case FFI_TYPE_LONGDOUBLE:
      ele_count = 1;
      goto done;
    case FFI_TYPE_COMPLEX:
      candidate = ty->elements[0]->type;
      switch (candidate)
	{
	case FFI_TYPE_FLOAT:
	case FFI_TYPE_DOUBLE:
	case FFI_TYPE_LONGDOUBLE:
	  ele_count = 2;
	  goto done;
	}
      return 0;
    case FFI_TYPE_STRUCT:
      break;
    }

  /* No HFA types are smaller than 4 bytes, or larger than 64 bytes.  */
  size = ty->size;
  if (size < 4 || size > 64)
    return 0;

  /* Find the type of the first non-structure member.  */
  elements = ty->elements;
  candidate = elements[0]->type;
  if (candidate == FFI_TYPE_STRUCT || candidate == FFI_TYPE_COMPLEX)
    {
      for (i = 0; ; ++i)
        {
          candidate = is_hfa0 (elements[i]);
          if (candidate >= 0)
            break;
        }
    }

  /* If the first member is not a floating point type, it's not an HFA.
     Also quickly re-check the size of the structure.  */
  switch (candidate)
    {
    case FFI_TYPE_FLOAT:
      ele_count = size / sizeof(float);
      if (size != ele_count * sizeof(float))
        return 0;
      break;
    case FFI_TYPE_DOUBLE:
    case FFI_TYPE_LONGDOUBLE:
      ele_count = size / sizeof(double);
      if (size != ele_count * sizeof(double))
        return 0;
      break;
    default:
      return 0;
    }
  if (ele_count > 4)
    return 0;

  /* Finally, make sure that all scalar elements are the same type.  */
  for (i = 0; elements[i]; ++i)
    {
      int t = elements[i]->type;
      if (t == FFI_TYPE_STRUCT || t == FFI_TYPE_COMPLEX)
        {
          if (!is_hfa1 (elements[i], candidate))
            return 0;
        }
      else if (t != candidate)
        return 0;
    }

  /* All tests succeeded.  Encode the result.  */
 done:
  return candidate * 4 + (4 - (int)ele_count);
}

static ffi_status initialize_aggregate(ffi_type *arg, size_t *offsets)
{
  ffi_type **ptr;

  if (UNLIKELY(arg == NULL || arg->elements == NULL))
    return FFI_BAD_TYPEDEF;

  arg->size = 0;
  arg->alignment = 0;

  ptr = &(arg->elements[0]);

  if (UNLIKELY(ptr == 0))
    return FFI_BAD_TYPEDEF;

  while ((*ptr) != NULL)
    {
      if (UNLIKELY(((*ptr)->size == 0)
		    && (initialize_aggregate((*ptr), NULL) != FFI_OK)))
	return FFI_BAD_TYPEDEF;

      /* Perform a sanity check on the argument type */
      FFI_ASSERT_VALID_TYPE(*ptr);

      arg->size = FFI_ALIGN(arg->size, (*ptr)->alignment);
      if (offsets)
	*offsets++ = arg->size;
      arg->size += (*ptr)->size;

      arg->alignment = (arg->alignment > (*ptr)->alignment) ?
	arg->alignment : (*ptr)->alignment;

      ptr++;
    }

  /* Structure size includes tail padding.  This is important for
     structures that fit in one register on ABIs like the PowerPC64
     Linux ABI that right justify small structs in a register.
     It's also needed for nested structure layout, for example
     struct A { long a; char b; }; struct B { struct A x; char y; };
     should find y at an offset of 2*sizeof(long) and result in a
     total size of 3*sizeof(long).  */
  arg->size = FFI_ALIGN (arg->size, arg->alignment);

  /* On some targets, the ABI defines that structures have an additional
     alignment beyond the "natural" one based on their elements.  */
#ifdef FFI_AGGREGATE_ALIGNMENT
  if (FFI_AGGREGATE_ALIGNMENT > arg->alignment)
    arg->alignment = FFI_AGGREGATE_ALIGNMENT;
#endif

  if (arg->size == 0)
    return FFI_BAD_TYPEDEF;
  else
    return FFI_OK;
}

ffi_status
ffi_prep_cif_machdep_arm64 (ffi_cif_arm64 *cif)
{
  ffi_type *rtype = cif->rtype;
  size_t bytes = cif->bytes;
  int flags, i, n;

  switch (rtype->type)
    {
    case FFI_TYPE_VOID:
      flags = AARCH64_RET_VOID;
      break;
    case FFI_TYPE_UINT8:
      flags = AARCH64_RET_UINT8;
      break;
    case FFI_TYPE_UINT16:
      flags = AARCH64_RET_UINT16;
      break;
    case FFI_TYPE_UINT32:
      flags = AARCH64_RET_UINT32;
      break;
    case FFI_TYPE_SINT8:
      flags = AARCH64_RET_SINT8;
      break;
    case FFI_TYPE_SINT16:
      flags = AARCH64_RET_SINT16;
      break;
    case FFI_TYPE_INT:
    case FFI_TYPE_SINT32:
      flags = AARCH64_RET_SINT32;
      break;
    case FFI_TYPE_SINT64:
    case FFI_TYPE_UINT64:
      flags = AARCH64_RET_INT64;
      break;
    case FFI_TYPE_POINTER:
      flags = (sizeof(void *) == 4 ? AARCH64_RET_UINT32 : AARCH64_RET_INT64);
      break;

    case FFI_TYPE_FLOAT:
    case FFI_TYPE_DOUBLE:
    case FFI_TYPE_LONGDOUBLE:
    case FFI_TYPE_STRUCT:
    case FFI_TYPE_COMPLEX:
      flags = is_vfp_type (rtype);
      if (flags == 0)
	{
	  size_t s = rtype->size;
	  if (s > 16)
	    {
	      flags = AARCH64_RET_VOID | AARCH64_RET_IN_MEM;
	      bytes += 8;
	    }
	  else if (s == 16)
	    flags = AARCH64_RET_INT128;
	  else if (s == 8)
	    flags = AARCH64_RET_INT64;
	  else
	    flags = AARCH64_RET_INT128 | AARCH64_RET_NEED_COPY;
	}
      break;

    default:
      abort();
    }

  for (i = 0, n = cif->nargs; i < n; i++)
    if (is_vfp_type (cif->arg_types[i]))
      {
	flags |= AARCH64_FLAG_ARG_V;
	break;
      }

  /* Round the stack up to a multiple of the stack alignment requirement. */
  cif->bytes = (unsigned) FFI_ALIGN(bytes, 16);
  cif->flags = flags;
  cif->aarch64_nfixedargs = 0;

  return FFI_OK;
}

ffi_status
ffi_prep_cif_machdep_var_arm64(ffi_cif_arm64 *cif, unsigned int nfixedargs,
			 unsigned int ntotalargs)
{
  ffi_status status = ffi_prep_cif_machdep_arm64 (cif);
  cif->aarch64_nfixedargs = nfixedargs;
  return status;
}

#define STACK_ARG_SIZE(x) FFI_ALIGN(x, 16)

// adapted from ffi_prep_cif_core
ffi_status ffi_prep_cif_arm64(ffi_cif_arm64 *cif,
                             unsigned int isvariadic,
                             unsigned int nfixedargs,
                             unsigned int ntotalargs,
                             ffi_type *rtype, ffi_type **atypes) {
  unsigned bytes = 0;
  unsigned int i;
  ffi_type **ptr;

  FFI_ASSERT(cif != NULL);
  FFI_ASSERT((!isvariadic) || (nfixedargs >= 1));
  FFI_ASSERT(nfixedargs <= ntotalargs);

  cif->abi = FFI_DEFAULT_ABI;
  cif->arg_types = atypes;
  cif->nargs = ntotalargs;
  cif->rtype = rtype;

  cif->flags = 0;

  /* Initialize the return type if necessary */
  if ((cif->rtype->size == 0)
      && (initialize_aggregate(cif->rtype, NULL) != FFI_OK))
    return FFI_BAD_TYPEDEF;

  /* Perform a sanity check on the return type */
  FFI_ASSERT_VALID_TYPE(cif->rtype);

  /* Make space for the return structure pointer */
  if (cif->rtype->type == FFI_TYPE_STRUCT) {
    bytes = STACK_ARG_SIZE(sizeof(void*));
  }

  for (ptr = cif->arg_types, i = cif->nargs; i > 0; i--, ptr++)
    {

      /* Initialize any uninitialized aggregate type definitions */
      if (((*ptr)->size == 0)
	  && (initialize_aggregate((*ptr), NULL) != FFI_OK))
	return FFI_BAD_TYPEDEF;

      /* Perform a sanity check on the argument type, do this
	 check after the initialization.  */
      FFI_ASSERT_VALID_TYPE(*ptr);

	{
	  /* Add any padding if necessary */
	  if (((*ptr)->alignment - 1) & bytes)
	    bytes = (unsigned)FFI_ALIGN(bytes, (*ptr)->alignment);
	  bytes += STACK_ARG_SIZE((*ptr)->size);
	}
    }

  cif->bytes = bytes;

  /* Perform machine dependent cif processing */
  if (isvariadic)
	return ffi_prep_cif_machdep_var_arm64(cif, nfixedargs, ntotalargs);

  return ffi_prep_cif_machdep_arm64(cif);
}

static void * compress_hfa_type (void *dest, void *reg, int h) {
    switch(h) {
    case AARCH64_RET_S4:
        *(float *)(dest + 12) = *(float *)(reg + 48);
    case AARCH64_RET_S3:
        *(float *)(dest + 8) = *(float *)(reg + 32);
    case AARCH64_RET_S2:
        *(float *)(dest + 4) = *(float *)(reg + 16);
    case AARCH64_RET_S1:
        *(float *)(dest) = *(float *)reg;
        break;
    case AARCH64_RET_D4:
        *(double *)(dest + 24) = *(double *)(reg + 48);
    case AARCH64_RET_D3:
        *(double *)(dest + 16) = *(double *)(reg + 32);
    case AARCH64_RET_D2:
        *(double *)(dest + 8) = *(double *)(reg + 16);
    case AARCH64_RET_D1:
        *(double *)(dest) = *(double *)reg;
    default:
        if (dest != reg)
            return memcpy (dest, reg, 16 * (4 - (h & 3)));
    }
    return dest;
}

/* Allocate an aligned slot on the stack and return a pointer to it.  */
static void *
allocate_to_stack (struct arg_state *state, void *stack,
		   size_t alignment, size_t size)
{
  size_t nsaa = state->nsaa;

  /* Round up the NSAA to the larger of 8 or the natural
     alignment of the argument's type.  */
#if defined (__APPLE__)
  if (state->allocating_variadic && alignment < 8)
    alignment = 8;
#else
  if (alignment < 8)
    alignment = 8;
#endif
    
  nsaa = FFI_ALIGN (nsaa, alignment);
  state->nsaa = nsaa + size;

  return (char *)stack + nsaa;
}

/* Either allocate an appropriate register for the argument type, or if
   none are available, allocate a stack slot and return a pointer
   to the allocated space.  */

static void *
allocate_int_to_reg_or_stack (struct arm64_call_context *context,
			      struct arg_state *state,
			      void *stack, size_t size)
{
  if (state->ngrn < N_X_ARG_REG)
    return &context->x[state->ngrn++];

  state->ngrn = N_X_ARG_REG;
  return allocate_to_stack (state, stack, size, size);
}

/* Primary handler to setup and invoke a function within a closure.

   A closure when invoked enters via the assembler wrapper
   ffi_closure_SYSV(). The wrapper allocates a call context on the
   stack, saves the interesting registers (from the perspective of
   the calling convention) into the context then passes control to
   ffi_closure_SYSV_inner() passing the saved context and a pointer to
   the stack at the point ffi_closure_SYSV() was invoked.

   On the return path the assembler wrapper will reload call context
   registers.

   ffi_closure_SYSV_inner() marshalls the call context into ffi value
   descriptors, invokes the wrapped function, then marshalls the return
   value back into the call context.  */

int ffi_closure_SYSV_inner_arm64 (ffi_cif_arm64 *cif,
			void (*fun)(ffi_cif_arm64*,void*,void**,void*),
			void *user_data,
			struct arm64_call_context *context,
			void *stack, void *rvalue)
{
  void **avalue = (void**) alloca (cif->nargs * sizeof (void*));
  int i, h, nargs, flags;
  struct arg_state state;

  arg_init (&state);

  for (i = 0, nargs = cif->nargs; i < nargs; i++)
    {
      ffi_type *ty = cif->arg_types[i];
      int t = ty->type;
      size_t n, s = ty->size;

      switch (t)
	{
	case FFI_TYPE_VOID:
	  FFI_ASSERT (0);
	  break;

	case FFI_TYPE_INT:
	case FFI_TYPE_UINT8:
	case FFI_TYPE_SINT8:
	case FFI_TYPE_UINT16:
	case FFI_TYPE_SINT16:
	case FFI_TYPE_UINT32:
	case FFI_TYPE_SINT32:
	case FFI_TYPE_UINT64:
	case FFI_TYPE_SINT64:
	case FFI_TYPE_POINTER:
	  avalue[i] = allocate_int_to_reg_or_stack (context, &state, stack, s);
	  break;

	case FFI_TYPE_FLOAT:
	case FFI_TYPE_DOUBLE:
	case FFI_TYPE_LONGDOUBLE:
	case FFI_TYPE_STRUCT:
	case FFI_TYPE_COMPLEX:
	  h = is_vfp_type (ty);
	  if (h)
	    {
	      n = 4 - (h & 3);
	      if (state.nsrn + n <= N_V_ARG_REG)
		{
		  void *reg = &context->v[state.nsrn];
		  state.nsrn += n;

		  /* Eeek! We need a pointer to the structure, however the
		     homogeneous float elements are being passed in individual
		     registers, therefore for float and double the structure
		     is not represented as a contiguous sequence of bytes in
		     our saved register context.  We don't need the original
		     contents of the register storage, so we reformat the
		     structure into the same memory.  */
		  avalue[i] = compress_hfa_type (reg, reg, h);
		}
	      else
		{
		  state.nsrn = N_V_ARG_REG;
		  avalue[i] = allocate_to_stack (&state, stack,
						 ty->alignment, s);
		}
	    }
	  else if (s > 16)
	    {
	      /* Replace Composite type of size greater than 16 with a
		 pointer.  */
	      avalue[i] = *(void **)
		allocate_int_to_reg_or_stack (context, &state, stack,
					      sizeof (void *));
	    }
	  else
	    {
	      n = (s + 7) / 8;
	      if (state.ngrn + n <= N_X_ARG_REG)
		{
		  avalue[i] = &context->x[state.ngrn];
		  state.ngrn += n;
		}
	      else
		{
		  state.ngrn = N_X_ARG_REG;
		  avalue[i] = allocate_to_stack (&state, stack,
						 ty->alignment, s);
		}
	    }
	  break;

	default:
	  abort();
	}

#if defined (__APPLE__)
      if (i + 1 == cif->aarch64_nfixedargs)
	{
	  state.ngrn = N_X_ARG_REG;
	  state.nsrn = N_V_ARG_REG;
	  state.allocating_variadic = 1;
	}
#endif
    }

  flags = cif->flags;
  fun (cif, rvalue, avalue, user_data);

  return flags;
}

hidden int arm64_rflags_for_type(ffi_type *rtype) {
    int rflags = 0;
    switch(rtype->type) {
        case FFI_TYPE_VOID:
            rflags = AARCH64_RET_VOID;
            break;
        case FFI_TYPE_UINT8:
            rflags = AARCH64_RET_UINT8;
            break;
        case FFI_TYPE_UINT16:
            rflags = AARCH64_RET_UINT16;
            break;
        case FFI_TYPE_UINT32:
            rflags = AARCH64_RET_UINT32;
            break;
        case FFI_TYPE_SINT8:
            rflags = AARCH64_RET_SINT8;
            break;
        case FFI_TYPE_SINT16:
            rflags = AARCH64_RET_SINT16;
            break;
        case FFI_TYPE_INT:
        case FFI_TYPE_SINT32:
            rflags = AARCH64_RET_SINT32;
            break;
        case FFI_TYPE_SINT64:
        case FFI_TYPE_UINT64:
            rflags = AARCH64_RET_INT64;
            break;
        case FFI_TYPE_POINTER:
            rflags = AARCH64_RET_INT64;
            break;
        case FFI_TYPE_FLOAT:
        case FFI_TYPE_DOUBLE:
        case FFI_TYPE_LONGDOUBLE:
        case FFI_TYPE_STRUCT:
        case FFI_TYPE_COMPLEX:
            rflags = is_vfp_type (rtype);
            if (rflags == 0) {
                size_t s = rtype->size;
                if (s > 16 || rtype->type == FFI_TYPE_STRUCT) {
                    // FIXME: only if FFI_TYPE_STRUCT is a non-trivial object
                    rflags = AARCH64_RET_VOID | AARCH64_RET_IN_MEM;
                    //bytes += 8;
                } else if (s == 16) {
                    rflags = AARCH64_RET_INT128;
                } else if (s == 8) {
                    rflags = AARCH64_RET_INT64;
                } else {
                    rflags = AARCH64_RET_INT128 | AARCH64_RET_NEED_COPY;
                }
            }
            break;
        default:
            abort();
    }
    return rflags;
}

static void extend_hfa_type (uc_engine *uc, unsigned nsrn, void *src, int h) {
    switch(h) {
        case AARCH64_RET_S4:
            uc_reg_write(uc, UC_ARM_REG_S0 + nsrn + 3, src+12);
        case AARCH64_RET_S3:
            uc_reg_write(uc, UC_ARM_REG_S0 + nsrn + 2, src+8);
        case AARCH64_RET_S2:
            uc_reg_write(uc, UC_ARM_REG_S0 + nsrn + 1, src+4);
        case AARCH64_RET_S1:
            uc_reg_write(uc, UC_ARM_REG_S0 + nsrn, src);
            break;

        case AARCH64_RET_D4:
            uc_reg_write(uc, UC_ARM_REG_D0 + nsrn + 3, src+24);
        case AARCH64_RET_D3:
            uc_reg_write(uc, UC_ARM_REG_D0 + nsrn + 2, src+16);
        case AARCH64_RET_D2:
            uc_reg_write(uc, UC_ARM_REG_D0 + nsrn + 1, src+8);
        case AARCH64_RET_D1:
            uc_reg_write(uc, UC_ARM_REG_D0 + nsrn, src);
            break;

        case AARCH64_RET_Q4:
            uc_reg_write(uc, UC_ARM_REG_Q0 + nsrn + 3, src+48);
        case AARCH64_RET_Q3:
            uc_reg_write(uc, UC_ARM_REG_Q0 + nsrn + 2, src+32);
        case AARCH64_RET_Q2:
            uc_reg_write(uc, UC_ARM_REG_Q0 + nsrn + 1, src+16);
        case AARCH64_RET_Q1:
            uc_reg_write(uc, UC_ARM_REG_Q0 + nsrn, src);
            break;
        default:
            abort();
    }
}

static uint64_t extend_integer_type (void *source, int type) {
    switch (type) {
        case FFI_TYPE_UINT8:
            return *(uint8_t *) source;
        case FFI_TYPE_SINT8:
            return *(int8_t *) source;
        case FFI_TYPE_UINT16:
            return *(uint16_t *) source;
        case FFI_TYPE_SINT16:
            return *(int16_t *) source;
        case FFI_TYPE_UINT32:
            return *(uint32_t *) source;
        case FFI_TYPE_INT:
        case FFI_TYPE_SINT32:
            return *(int32_t *) source;
        case FFI_TYPE_UINT64:
        case FFI_TYPE_SINT64:
            return *(uint64_t *) source;
        case FFI_TYPE_POINTER:
            return *(uintptr_t *) source;
        default:
          abort();
    }
}

hidden void call_emulated_function (ffi_cif *cif, void *ret, void **args, void *address) {
    printf("calling emulated function at %p\n", address);
    struct emulator_ctx *ctx = get_emulator_ctx();
    ffi_cif *cif_native = cif_cache_get_native(address);
    ffi_cif_arm64 *cif_arm64;
    struct call_wrapper *wrapper = NULL;
    
    if (cif_native == CIF_MARKER_WRAPPER) {
        wrapper = (struct call_wrapper*)cif_cache_get_arm64(address);
        cif_arm64 = wrapper->cif_arm64;
    } else if (cif_native == CIF_MARKER_SHIM) {
        abort();
    } else if (CIF_IS_CIF(cif_native)) {
        cif_arm64 = cif_cache_get_arm64(address);
    } else {
        fprintf(stderr, "call_emulated_function(%p) with no valid cif (%p)\n", address, cif_native);
        abort();
    }
    
    // allocate stack
    size_t stack_bytes = cif_arm64->bytes;
    uint64_t stack_ptr;
    uc_reg_read(ctx->uc, UC_ARM64_REG_SP, &stack_ptr);
    stack_ptr -= stack_bytes;
    uc_reg_write(ctx->uc, UC_ARM64_REG_SP, &stack_ptr);
    void *stack = (void*)stack_ptr;
    
    // pass arguments
    struct arg_state state;
    arg_init (&state);
    int i, nargs;
    for (i = 0, nargs = cif_arm64->nargs; i < nargs; i++) {
        ffi_type *ty = cif_arm64->arg_types[i];
        size_t s = ty->size;
        void *a = args[i];
        int h, t;

        t = ty->type;
        switch (t) {
            case FFI_TYPE_VOID:
                abort();
        /* If the argument is a basic type the argument is allocated to an
           appropriate register, or if none are available, to the stack.  */
            case FFI_TYPE_INT:
            case FFI_TYPE_UINT8:
            case FFI_TYPE_SINT8:
            case FFI_TYPE_UINT16:
            case FFI_TYPE_SINT16:
            case FFI_TYPE_UINT32:
            case FFI_TYPE_SINT32:
            case FFI_TYPE_UINT64:
            case FFI_TYPE_SINT64:
            case FFI_TYPE_POINTER:
            do_pointer:
            {
                uint64_t ext = extend_integer_type (a, t);
                if (state.ngrn < N_X_ARG_REG) {
                    uc_reg_write(ctx->uc, UC_ARM64_REG_X0 + (state.ngrn++), &ext);
                } else {
                    void *d = allocate_to_stack(&state, stack, ty->alignment, s);
                    state.ngrn = N_X_ARG_REG;
                    memcpy(d, a, s);
                }
            }
            break;
            case FFI_TYPE_FLOAT:
            case FFI_TYPE_DOUBLE:
            case FFI_TYPE_LONGDOUBLE:
            case FFI_TYPE_STRUCT:
            case FFI_TYPE_COMPLEX:
            {
                void *dest;
                h = is_vfp_type (ty);
                if (h) {
                    int elems = 4 - (h & 3);
                    if (state.nsrn + elems <= N_V_ARG_REG) {
                        // put hfa type into registers starting at nsrn
                        extend_hfa_type (ctx->uc, state.nsrn, a, h);
                        state.nsrn += elems;
                        break;
                    }
                    state.nsrn = N_V_ARG_REG;
                    dest = allocate_to_stack (&state, stack, ty->alignment, s);
                    memcpy(dest, a, s);
                } else if (s > 16) {
                    /* If the argument is a composite type that is larger than 16
                       bytes, then the argument has been copied to memory, and
                       the argument is replaced by a pointer to the copy.  */
                    a = &args[i];
                    t = FFI_TYPE_POINTER;
                    s = sizeof (void *);
                    goto do_pointer;
                } else {
                    size_t n = (s + 7) / 8;
                    if (state.ngrn + n <= N_X_ARG_REG) {
                        /* If the argument is a composite type and the size in
                           double-words is not more than the number of available
                           X registers, then the argument is copied into
                           consecutive X registers.  */
                        for(size_t j = 0; j < n; j++) {
                            uc_reg_write(ctx->uc, UC_ARM64_REG_X0 + state.ngrn + j, a + (8*j));
                        }
                        state.ngrn += n;
                    } else {
                        /* Otherwise, there are insufficient X registers. Further
                           X register allocations are prevented, the NSAA is
                           adjusted and the argument is copied to memory at the
                           adjusted NSAA.  */
                        state.ngrn = N_X_ARG_REG;
                        void *dest = allocate_to_stack (&state, stack, ty->alignment, s);
                        memcpy(dest, a, s);
                    }
                }
            }
            break;
            default:
                abort();
        }
        
        if (i + 1 == cif_arm64->aarch64_nfixedargs) {
            state.ngrn = N_X_ARG_REG;
            state.nsrn = N_V_ARG_REG;
            state.allocating_variadic = 1;
        }
    }
    
    if (wrapper && wrapper->native_to_emulated) {
        printf("calling reverse wrapper for %p\n", address);
        wrapper->native_to_emulated(ret, args);
    }
    run_emulator(ctx, (uint64_t)address);
    if (wrapper && wrapper->emulated_to_native) {
        printf("calling reverse wrapper for %p\n", address);
        wrapper->emulated_to_native(ret, args);
    }
    
    stack_ptr += stack_bytes;
    uc_reg_write(ctx->uc, UC_ARM64_REG_SP, &stack_ptr);
    
    // result
    int rflags = arm64_rflags_for_type(cif->rtype);
    uint64_t rr0;
    if (rflags & AARCH64_RET_IN_MEM) {
        // already done
    } else switch(rflags & AARCH64_RET_MASK) {
        case AARCH64_RET_VOID:
            break;
        case AARCH64_RET_INT64:
            uc_reg_read(ctx->uc, UC_ARM64_REG_X0, ret);
        case AARCH64_RET_INT128:
            uc_reg_read(ctx->uc, UC_ARM64_REG_X0, ret);
            uc_reg_read(ctx->uc, UC_ARM64_REG_X1, ret+8);
            break;
        case AARCH64_RET_UINT8:
        case AARCH64_RET_SINT8:
            uc_reg_read(ctx->uc, UC_ARM64_REG_X0, &rr0);
            memcpy(ret, &rr0, 1);
            break;
        case AARCH64_RET_UINT16:
        case AARCH64_RET_SINT16:
            uc_reg_read(ctx->uc, UC_ARM64_REG_X0, &rr0);
            memcpy(ret, &rr0, 2);
            break;
        case AARCH64_RET_UINT32:
        case AARCH64_RET_SINT32:
            uc_reg_read(ctx->uc, UC_ARM64_REG_X0, &rr0);
            memcpy(ret, &rr0, 4);
            break;
        
        case AARCH64_RET_S4:
            uc_reg_read(ctx->uc, UC_ARM64_REG_S3, ret+12);
        case AARCH64_RET_S3:
            uc_reg_read(ctx->uc, UC_ARM64_REG_S2, ret+8);
        case AARCH64_RET_S2:
            uc_reg_read(ctx->uc, UC_ARM64_REG_S1, ret+4);
        case AARCH64_RET_S1:
            uc_reg_read(ctx->uc, UC_ARM64_REG_S0, ret);
            break;
        
        case AARCH64_RET_D4:
            uc_reg_read(ctx->uc, UC_ARM64_REG_D3, ret+24);
        case AARCH64_RET_D3:
            uc_reg_read(ctx->uc, UC_ARM64_REG_D2, ret+16);
        case AARCH64_RET_D2:
            uc_reg_read(ctx->uc, UC_ARM64_REG_D1, ret+8);
        case AARCH64_RET_D1:
            uc_reg_read(ctx->uc, UC_ARM64_REG_D0, ret);
            break;
        
        case AARCH64_RET_Q4:
            uc_reg_read(ctx->uc, UC_ARM64_REG_Q3, ret+48);
        case AARCH64_RET_Q3:
            uc_reg_read(ctx->uc, UC_ARM64_REG_Q2, ret+32);
        case AARCH64_RET_Q2:
            uc_reg_read(ctx->uc, UC_ARM64_REG_Q1, ret+16);
        case AARCH64_RET_Q1:
            uc_reg_read(ctx->uc, UC_ARM64_REG_Q0, ret);
            break;
        
        default:
            abort();
    }
}
