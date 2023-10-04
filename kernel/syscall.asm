%include "sconst.inc"

_NR_get_ticks       equ 0   ; 要跟 global.c 中 sys_call_table 的定义相对应
_NR_write           equ 1

; 这里我们把系统调用中断号设为 0x90，它只要不和原来的中断号重复即可（Linux相应的中断号为 0x80）
INT_VECTOR_SYS_CALL equ 0x90


; global  _NR_get_ticks
global  get_ticks
global  write


bits 32
[SECTION .text]

get_ticks:
    mov eax, _NR_get_ticks
    int INT_VECTOR_SYS_CALL
    ret


write:
    mov eax, _NR_write
    mov ebx, [esp + 4]
    mov ecx, [esp + 8]
    int INT_VECTOR_SYS_CALL
    ret













;        write()                    get_ticks()
;           |                            |
;           |                            |
;           |                            |
;           ------------------------------
;                          |
;                          |                                      用户态
; ______________________sys_call________________________________________                      
;                          |                                      内核态
;                          |
;                          |

