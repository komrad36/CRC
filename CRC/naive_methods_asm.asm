.CODE

P equ 082f63b78h

; uint32_t f(const void* M, uint32_t bytes);

;; OPTION 1
option_1_cf_jump PROC
xor eax, eax

test edx, edx
jz END_OF_LOOP

add rcx, rdx
neg rdx

START_OF_LOOP:
xor al, byte ptr [rcx + rdx]

shr eax, 1
jnc SKIP_XOR_1
xor eax, P
SKIP_XOR_1:

shr eax, 1
jnc SKIP_XOR_2
xor eax, P
SKIP_XOR_2:

shr eax, 1
jnc SKIP_XOR_3
xor eax, P
SKIP_XOR_3:

shr eax, 1
jnc SKIP_XOR_4
xor eax, P
SKIP_XOR_4:

shr eax, 1
jnc SKIP_XOR_5
xor eax, P
SKIP_XOR_5:

shr eax, 1
jnc SKIP_XOR_6
xor eax, P
SKIP_XOR_6:

shr eax, 1
jnc SKIP_XOR_7
xor eax, P
SKIP_XOR_7:

shr eax, 1
jnc SKIP_XOR_8
xor eax, P
SKIP_XOR_8:

inc rdx
jnz START_OF_LOOP

END_OF_LOOP:
ret
option_1_cf_jump ENDP

;; OPTION 2
option_2_multiply_mask PROC
xor eax, eax

test edx, edx
jz END_OF_LOOP

add rcx, rdx
neg rdx
mov r9d, P

START_OF_LOOP:
xor al, byte ptr [rcx + rdx]

REPEAT 8
mov r8d, eax
shr eax, 1
and r8d, 1
imul r8d, r9d
xor eax, r8d
ENDM

inc rdx
jnz START_OF_LOOP

END_OF_LOOP:
ret
option_2_multiply_mask ENDP

;; OPTION 3
option_3_bit_mask PROC
xor eax, eax

test edx, edx
jz END_OF_LOOP

add rcx, rdx
neg rdx

START_OF_LOOP:
xor al, byte ptr [rcx + rdx]

REPEAT 8
mov r8d, eax
shr eax, 1
and r8d, 1
neg r8d
and r8d, P
xor eax, r8d
ENDM

inc rdx
jnz START_OF_LOOP

END_OF_LOOP:
ret
option_3_bit_mask ENDP

;; OPTION 4
option_4_cmove PROC
xor eax, eax

test edx, edx
jz END_OF_LOOP

add rcx, rdx
neg rdx
mov r9d, P

START_OF_LOOP:
xor al, byte ptr [rcx + rdx]

REPEAT 8
xor r8d, r8d
shr eax, 1
cmovc r8d, r9d
xor eax, r8d
ENDM

inc rdx
jnz START_OF_LOOP

END_OF_LOOP:
ret
option_4_cmove ENDP


END