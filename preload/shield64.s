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
.align 2
table:
.hword 142,143,144,145,146,149,151,152,159,161,162

.text
.global syscall
syscall:
    mov x9, x0
    adrp x10, table
    add x10, x10, :lo12:table
    mov x11, #11
1:
    ldrh w12, [x10], #2
    cmp w9, w12
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
