%include "sconst.inc"


; 导入函数
extern	cstart
extern	kernel_main
extern	exception_handler
extern	spurious_irq
extern	disp_str
extern	delay
extern  clock_handler

; 导入全局变量
extern	gdt_ptr
extern	idt_ptr
extern	p_proc_ready
extern	tss
extern	disp_pos
extern	k_reenter

bits 32

[SECTION .data]
clock_int_msg		db	"^", 0

[SECTION .bss]
StackSpace      resb    2 * 1024
StackTop

[SECTION .text]
global _start	                ; 导出 _start

global restart

global	divide_error
global	single_step_exception
global	nmi
global	breakpoint_exception
global	overflow
global	bounds_check
global	inval_opcode
global	copr_seg_overrun
global	copr_not_available
global	double_fault
global	inval_tss
global	segment_not_present
global	stack_exception
global	general_protection
global	page_fault
global	copr_error
global  hwint00
global  hwint01
global  hwint02
global  hwint03
global  hwint04
global  hwint05
global  hwint06
global  hwint07
global  hwint08
global  hwint09
global  hwint10
global  hwint11
global  hwint12
global  hwint13
global  hwint14
global  hwint15


; ---------------------------------
_start:
    ; 把 esp 从 LOADER 挪到 KERNEL
    mov esp, StackTop           ; 堆栈在 bss 段中

    mov dword [disp_pos], 0

    sgdt    [gdt_ptr]           ; cstart() 中将会用到 gdt_ptr
    call cstart                 ; 在此函数中改变了gdt_ptr，让它指向新的GDT
    lgdt    [gdt_ptr]           

    lidt    [idt_ptr]           ; 使用新的GDT

    jmp SELECTOR_KERNEL_CS:csinit
; 这个跳转指令强制使用刚刚初始化的结构
csinit:		

    ; jmp 0x40:0
	; ud2

    xor eax, eax
    mov ax, SELECTOR_TSS
    ltr ax
    
    ; sti
    jmp kernel_main
    ; hlt


; 中断和异常 -- 异常
; ---------------------------------
divide_error:
    push	0xFFFFFFFF	
    push    0
    jmp exception

single_step_exception:
    push	0xFFFFFFFF	
    push    1
    jmp exception

nmi:
    push	0xFFFFFFFF	
    push    2
    jmp exception

breakpoint_exception:
    push	0xFFFFFFFF	
    push    3
    jmp exception

overflow:
    push	0xFFFFFFFF	
    push    4
    jmp exception

bounds_check:
    push	0xFFFFFFFF	
    push    5
    jmp exception

inval_opcode:
    push	0xFFFFFFFF	
    push    6
    jmp exception

copr_not_available:
    push	0xFFFFFFFF	
    push    7
    jmp exception

double_fault:
    push    8
    jmp exception

copr_seg_overrun:
    push	0xFFFFFFFF	
    push    9
    jmp exception

inval_tss:
    push    10
    jmp exception

segment_not_present:
    push    11
    jmp exception

stack_exception:
    push    12
    jmp exception

general_protection:
    push    13
    jmp exception

page_fault:
    push    14
    jmp exception

copr_error:
    push	0xFFFFFFFF	
    push    16
    jmp exception
    
exception:
	call	exception_handler
	add	esp, 4*2	; 让栈顶指向 EIP，堆栈中从顶向下依次是：EIP、CS、EFLAGS
	hlt


; 中断和异常 -- 硬件中断
; ---------------------------------
%macro  hwint_master    1
        push    %1
        call    spurious_irq
        add     esp, 4
        hlt
%endmacro

ALIGN   16
hwint00:                    ; Interrupt routine for irq 0 (the clock).
    sub esp, 4              ; 这个跳过的4字节是 retaddr
    pushad
    push	ds
	push	es	
	push	fs
	push	gs
	mov	dx, ss              ; ss is kernel data segment
    mov	ds, dx              ; load rest of kernel segments
	mov	es, dx              ; kernel does not use fs, gs

    inc byte [gs:0]         ; 改变屏幕第 0 行, 第 0 列的字符

    ; reenable master 8259
    mov al, EOI             
    out INT_M_CTL, al      

    inc	dword [k_reenter]
	cmp	dword [k_reenter], 0
    jne .1                  ; 重入则跳到.1，通常情况下是顺序执行
	; jne	.re_enter

    mov	esp, StackTop		; 切到内核栈

    push    .restart_v2
    jmp .2

.1: ; 中断重入
    push    .restart_reenter_v2
.2: ; 没有中断重入

	sti

    push    0
    call    clock_handler
    add	esp, 4

    ; push	1
 	; call	delay
 	; add	esp, 4
    cli

    ret                     ; 重入时跳到.restart_reenter_v2，通常情况下是到.restart_v2

.restart_v2:
    mov esp, [p_proc_ready] ; 离开内核栈
    lldt [esp + P_LDT_SEL]

    ; 设置 tss.esp0 的值，准备下一次进程被中断时使用
    lea eax, [esp + P_STACKTOP]
    mov dword [tss + TSS3_S_SP0], eax

; .re_enter:                
.restart_reenter_v2:        ; 如果(k_reenter != 0)，会跳到此处
    dec dword [k_reenter]
    pop     gs
    pop     fs
    pop     es
    pop     ds
    popad
    add	esp, 4
    iret



ALIGN   16
hwint01:                ; Interrupt routine for irq 1 (keyboard)
        hwint_master    1

ALIGN   16
hwint02:                ; Interrupt routine for irq 2 (cascade!)
        hwint_master    2

ALIGN   16
hwint03:                ; Interrupt routine for irq 3 (second serial)
        hwint_master    3

ALIGN   16
hwint04:                ; Interrupt routine for irq 4 (first serial)
        hwint_master    4

ALIGN   16
hwint05:                ; Interrupt routine for irq 5 (XT winchester)
        hwint_master    5

ALIGN   16
hwint06:                ; Interrupt routine for irq 6 (floppy)
        hwint_master    6

ALIGN   16
hwint07:                ; Interrupt routine for irq 7 (printer)
        hwint_master    7

; ---------------------------------
%macro  hwint_slave     1
        push    %1
        call    spurious_irq
        add     esp, 4
        hlt
%endmacro

ALIGN   16
hwint08:                ; Interrupt routine for irq 8 (realtime clock).
        hwint_slave     8

ALIGN   16
hwint09:                ; Interrupt routine for irq 9 (irq 2 redirected)
        hwint_slave     9

ALIGN   16
hwint10:                ; Interrupt routine for irq 10
        hwint_slave     10

ALIGN   16
hwint11:                ; Interrupt routine for irq 11
        hwint_slave     11

ALIGN   16
hwint12:                ; Interrupt routine for irq 12
        hwint_slave     12

ALIGN   16
hwint13:                ; Interrupt routine for irq 13 (FPU exception)
        hwint_slave     13

ALIGN   16
hwint14:                ; Interrupt routine for irq 14 (AT winchester)
        hwint_slave     14

ALIGN   16
hwint15:                ; Interrupt routine for irq 15
        hwint_slave     15

    
; restart
restart:
    mov esp, [p_proc_ready]
    lldt    [esp + P_LDT_SEL]

    lea eax, [esp + P_STACKTOP]
    mov dword [tss + TSS3_S_SP0], eax

    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 4
    iret

