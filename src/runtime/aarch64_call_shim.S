.text
.align 2
.global pico_ot_call_noarg_with_sret_scratch
.type pico_ot_call_noarg_with_sret_scratch, %function

// uintptr_t pico_ot_call_noarg_with_sret_scratch(void* function, void* scratch)
// x0=function, x1=scratch. Populate x8 with valid storage so the recovered vendor
// singleton accessor is safe whether it returns a pointer in x0 or uses hidden sret.
pico_ot_call_noarg_with_sret_scratch:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x16, x0
    mov x8, x1
    mov x0, xzr
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    blr x16
    ldp x29, x30, [sp], #16
    ret

.size pico_ot_call_noarg_with_sret_scratch, .-pico_ot_call_noarg_with_sret_scratch
