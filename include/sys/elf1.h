#ifndef	_KONIX_ELF_H_
#define	_KONIX_ELF_H_

#define EI_NIDENT 16

/* Conglomeration of the identification bytes, for easy testing as a word. */
#define	ELFMAG		"\177ELF"
#define	SELFMAG		4


typedef unsigned int    Elf32_Addr;
typedef unsigned int    Elf32_Off;


typedef struct {
    u8 e_ident[EI_NIDENT];      // ELF 标识
    u16         e_type;         // 对象文件类型
    u16         e_machine;      // 架构类型
    u32         e_version;      // 对象文件版本
    Elf32_Addr  e_entry;        // 程序入口虚拟地址
    Elf32_Off   e_phoff;        // 程序头部偏移量
    Elf32_Off   e_shoff;        // 节头部偏移量
    u32         e_flags;        // 处理器特定标志
    u16         e_ehsize;       // ELF 头部长度
    u16         e_phentsize;    // 程序头部表项大小
    u16         e_phnum;        // 程序头部表项数目
    u16         e_shentsize;    // 节头部表项大小
    u16         e_shnum;        // 节头部表项数目
    u16         e_shstrndx;     // 节头部字符串表索引
} Elf32_Ehdr;


typedef struct {
    u32         sh_name;        // 节头部名称索引
    u32         sh_type;        // 节类型
    u32         sh_flags;       // 节头部属性
    Elf32_Addr  sh_addr;        // 节在镜像中的地址
    Elf32_Off   sh_offset;      // ELF 二进制文件中节的位置（以字节为单位）
    u32         sh_size;        // 节大小（以字节为单位）
    u32         sh_link;        // 节头部表链接索引，取决于节类型
    u32         sh_info;        // 节信息，取决于节类型
    u32         sh_addralign;   // 节地址对齐
    u32         sh_entsize;     // 节包含固定大小条目，每个条目大小为 sh_entsize 字节
} Elf32_Shdr;

#endif
