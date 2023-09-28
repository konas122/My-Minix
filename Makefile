# Entry point of KonasUnix
# It must have the same value with 'KernelEntryPointPhyAddr' in `load.inc`
ENTRYPOINT = 0x30400

# Offset of entry point in kernel file
# It depends on ENTRYPOINT
ENTRYOFFSET = 0x400


# flags
ASM = nasm
DASM = ndisasm
CC = gcc
LD = ld
ASMBFLAGS 	= -I boot/include/
ASMKFLAGS 	= -I include/ -f elf
CFLAGS		= -I include/ -c -fno-builtin -m32
LDFLAGS 	= -s -Ttext $(ENTRYPOINT) -m elf_i386
DASMFLAGS 	= -u -o $(ENTRYPOINT) -e $(ENTRYOFFSET)


# This program
KONIXBOOT	= boot/boot.bin boot/loader.bin
KONIXKERNEL	= kernel.bin
OBJS		= kernel/kernel.o kernel/start.o kernel/main.o kernel/clock.o\
			kernel/i8259.o kernel/global.o kernel/protect.o\
			lib/kliba.o lib/klib.o lib/string.o
DASMOUTPUT	= kernel_bin.asm


# All Phony Targets
.PHONY : everything final image clean realclean disasm all building

# Default starting position
everything : $(KONIXBOOT) $(KONIXKERNEL)

all : realclean everything

final : all clean

image : final building

clean : 
	rm -f $(OBJS)

realclean :
	rm -rf $(OBJS) $(KONIXBOOT) $(KONIXKERNEL)

disasm:
	$(DASM) $(DASMFLAGS) $(KONIXKERNEL) > $(DASMOUTPUT)


# We assume that "a.img" exists in current folder
building : 
	dd if=boot/boot.bin of=a.img bs=512 count=1 conv=notrunc
	sudo mount -o loop a.img /mnt/floppy/
	sudo cp -fv boot/loader.bin /mnt/floppy/
	sudo cp -fv kernel.bin /mnt/floppy
	sudo umount /mnt/floppy

boot/boot.bin : boot/boot.asm boot/include/load.inc boot/include/fat12hdr.inc
	$(ASM) $(ASMBFLAGS) -o $@ $<

boot/loader.bin : boot/loader.asm boot/include/load.inc \
			boot/include/fat12hdr.inc boot/include/pm.inc
	$(ASM) $(ASMBFLAGS) -o $@ $<

$(KONIXKERNEL) : $(OBJS)
	$(LD) $(LDFLAGS) -o $(KONIXKERNEL) $(OBJS)

kernel/kernel.o : kernel/kernel.asm include/sconst.inc
	$(ASM) $(ASMKFLAGS) -o $@ $<

kernel/start.o: kernel/start.c include/type.h include/const.h include/protect.h include/string.h include/proc.h include/proto.h \
			include/global.h
	$(CC) $(CFLAGS) -o $@ $<

kernel/main.o: kernel/main.c include/type.h include/const.h include/protect.h include/string.h include/proc.h include/proto.h \
			include/global.h
	$(CC) $(CFLAGS) -o $@ $<

kernel/clock.o: kernel/clock.c
	$(CC) $(CFLAGS) -o $@ $<

kernel/i8259.o: kernel/i8259.c include/type.h include/const.h include/protect.h include/proto.h
	$(CC) $(CFLAGS) -o $@ $<

kernel/global.o: kernel/global.c include/type.h include/const.h include/protect.h include/proc.h \
			include/global.h include/proto.h
	$(CC) $(CFLAGS) -o $@ $<

kernel/protect.o: kernel/protect.c include/type.h include/const.h include/protect.h include/proc.h include/proto.h \
			include/global.h
	$(CC) $(CFLAGS) -o $@ $<

lib/klib.o: lib/klib.c include/type.h include/const.h include/protect.h include/string.h include/proc.h include/proto.h \
			include/global.h
	$(CC) $(CFLAGS) -o $@ $<

lib/kliba.o : lib/kliba.asm
	$(ASM) $(ASMKFLAGS) -o $@ $<

lib/string.o : lib/string.asm
	$(ASM) $(ASMKFLAGS) -o $@ $<
