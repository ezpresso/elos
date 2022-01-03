#
# Standard PC (32bit, Intel, SMP) build script
#
"""
defaultconf("i686", "i686-linux-musl-gcc", ncpu=8)
conf("DEVELOPMENT", YES)

addrule("image/hdd.img", "$(shell find image/fsroot)",
	"\tgenext2fs -b 1048576 -m 0 -d image/fsroot/ $@")

image("image/boot.iso image/hdd.img",
	"\tcp image/boot.iso $@")

addrule("qemu", "elos.img image/hdd.img",
	"\tqemu-system-i386 -cdrom $< -hda $(word 2,$^) -smp " +
		str(getconf("NCPU")) + " \\\n" +
	"\t-serial mon:stdio \\\n" +
	"\t-global ide-hd.physical_block_size=4096 \\\n",
	phony=True)

	"\t-drive file=hdd.img,if=none,id=mydisk,format=raw \\\n" +
	"\t-device ich9-ahci,id=ahci \\\n" +
	"\t-device ide-drive,drive=mydisk,bus=ahci.0 \\\n" +
"""

Toolchain("i686-linux-musl")
Architecture("i686", archdir="i386")
Loader("grub", "i386")

with AddTarget("kernel", type=ProgType.KERNEL):
	Config('DEVELOPMENT', "CONFIG_YES")
	Config('MODULAR', "CONFIG_YES")
	Config('INVARIANTS', "CONFIG_YES")
	Config('ASAN', "CONFIG_YES")
	Config('UBSAN', "CONFIG_YES")
	Config('STACKCHK', "CONFIG_YES")

	Config('STATIC_MODULES', [
		"elf32",
		"script",
		"ext2",
		"acpi",
		"atpit",
		"atrtc",
		"hpet",
		"uart-pc",
		"i8259a",
		"ioapic",
		"i8042",
		"ata",
		"isa",
		"pci",
		"i8254x",
		"bochs",
		"nvidia",
		"termios",
		"font-8x16", # TODO
		"pty",

		"i440fx", # TODO
		"piix",
		"qemu-i440fx"
	])
	Config('DYNAMIC_MODULES', [])

AddTarget("init", type=ProgType.EXE, location=Location.INITRD)

# Temporary
AddTarget("teststartup", type=ProgType.EXE, location=Location.INITRD)
