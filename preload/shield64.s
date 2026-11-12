.macro STUB name
.global \name
\name:
    mov w0, #0
    ret
.endm

STUB setuid
STUB setgid
STUB setreuid
STUB setregid
STUB setresuid
STUB setresgid
STUB setfsuid
STUB setfsgid
STUB setgroups
STUB initgroups

.data
.align 3
table:
.quad 105, 106, 107, 108, 109, 110, 111, 112, 113, 114

.text
.global syscall
syscall:
    mov x9, x0
    adrp x10, table
    add x10, x10, :lo12:table
    mov x11, #10
1:
    ldr x12, [x10], #8
    cmp x9, x12
    b.eq 2f
    subs x11, x11, #1
    b.ne 1b
    mov x8, x9
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    mov x4, x5
    mov x5, x6
    svc #0
    ret
2:
    mov x0, #0
    ret
