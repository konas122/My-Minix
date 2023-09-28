DA_32       EQU 4000h   ; 32 位段
DA_LIMIT_4K EQU 8000h   ; 段界限粒度为 4K 字节

DA_DPL0     EQU 00h
DA_DPL1     EQU 20h
DA_DPL2     EQU 40h
DA_DPL3     EQU 60h

; 存储段描述符类型值说明
DA_DR		EQU	90h	    ; 存在的只读数据段类型值
DA_DRW		EQU	92h	    ; 存在的可读写数据段属性值
DA_DRWA		EQU	93h	    ; 存在的已访问可读写数据段类型值
DA_C		EQU	98h	    ; 存在的只执行代码段属性值
DA_CR		EQU	9Ah	    ; 存在的可执行可读代码段属性值
DA_CCO		EQU	9Ch	    ; 存在的只执行一致代码段属性值
DA_CCOR		EQU	9Eh	    ; 存在的可执行可读一致代码段属性值

; 系统段描述符类型值说明
DA_LDT		EQU	  82h	; 局部描述符表段类型值
DA_TaskGate	EQU	  85h	; 任务门类型值
DA_386TSS	EQU	  89h	; 可用 386 任务状态段类型值
DA_386CGate	EQU	  8Ch	; 386 调用门类型值
DA_386IGate	EQU	  8Eh	; 386 中断门类型值
DA_386TGate	EQU	  8Fh	; 386 陷阱门类型值


; 选择子类型值说明
; SA_** : Selector Attribute
SA_RPL0		EQU	0	
SA_RPL1		EQU	1
SA_RPL2		EQU	2
SA_RPL3		EQU	3

SA_TIG		EQU	0
SA_TIL		EQU	4

; 分页机制使用的常量说明
PG_P		EQU	1	; 页存在属性位
PG_RWR		EQU	0	; R/W 属性位值, 读/执行
PG_RWW		EQU	2	; R/W 属性位值, 读/写/执行
PG_USS		EQU	0	; U/S 属性位值, 系统级
PG_USU		EQU	4	; U/S 属性位值, 用户级


; 描述符 宏
%macro Descriptor 3
	dw	%2 & 0FFFFh
	dw	%1 & 0FFFFh
	db	(%1 >> 16) & 0FFh
	dw	((%2 >> 8) & 0F00h) | (%3 & 0F0FFh)
	db	(%1 >> 24) & 0FFh
%endmacro

; 门 宏
%macro Gate 4
    dw  (%2 & 0FFFFh)
    dw  %1
    dw  (%3 & 1Fh) | ((%4 << 8) & 0FF00h)
    dw  ((%2 >> 16) & 0FFFFh)
%endmacro

