.PHONY: all iso update-iso clean

all: update-iso

iso:
	mkdir -p build iso_root/boot
	objcopy -O binary build/kernel/hellcore.elf hellcore.bin
	cp hellcore.bin iso_root/boot/hellcore.bin
	xorriso -as mkisofs -R -o build/hellcore-gaming-os.iso iso_root

update-iso:
	mkdir -p build iso_root/boot
	objcopy -O binary build/kernel/hellcore.elf hellcore.bin
	cp hellcore.bin iso_root/boot/hellcore.bin
	xorriso -as mkisofs -R -o build/hellcore-gaming-os.iso iso_root

clean:
	rm -rf build hellcore.bin iso_root/boot/hellcore.bin
