cd "`dirname "$0"`"
cd ..

# Build the image
/usr/bin/env python3 $(which scons) target=i686-pc mode=development

# BOOT!
qemu-system-i386 \
	-cdrom bin/i686-pc/image/boot.iso \
	-serial mon:stdio \
	-global ide-hd.physical_block_size=4096 \
	-hda private/hdd.img \
	-smp 4
