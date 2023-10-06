%include "sconst.inc"

; 这里我们把系统调用中断号设为 0x90，它只要不和原来的中断号重复即可（Linux相应的中断号为 0x80）
INT_VECTOR_SYS_CALL equ 0x90
_NR_printx          equ 0
_NR_sendrec         equ 1


; 导出符号
global  printx
global  sendrec


bits 32
[section .text]


; Never call sendrec() directly, call send_recv() instead.
;   sendrec(int function, int src_dest, MESSAGE* msg);
sendrec:
    mov eax, _NR_sendrec
    mov ebx, [esp + 4]  ; function
    mov ecx, [esp + 8]  ; src_dest
    mov edx, [esp + 12] ; p_msg
    int INT_VECTOR_SYS_CALL
    ret


;   void printx(char* s);
printx:
    mov eax, _NR_printx
    mov edx, [esp + 4]
    int INT_VECTOR_SYS_CALL
    ret

