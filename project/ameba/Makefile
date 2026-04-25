.PHONY: all clean
default: all

all: amebadplus amebagreen2

SHELL := /bin/bash

amebadplus:
	@source env.sh && python $$AMEBA_SDK/ameba.py build RTL8711Dx
	@dd if=/dev/zero status=none bs=1 count=$$((0x14000 + $$(wc -c < ./build_RTL8711Dx/km0_km4_app.bin))) | tr '\000' '\377' > ./build_RTL8711Dx/Image_All.bin
	@dd if=./build_RTL8711Dx/km4_boot_all.bin status=none of=./build_RTL8711Dx/Image_All.bin bs=1 seek=0 conv=notrunc
	@dd if=./build_RTL8711Dx/km0_km4_app.bin status=none of=./build_RTL8711Dx/Image_All.bin bs=1 seek=$$((0x14000)) conv=notrunc
	@md5sum ./build_RTL8711Dx/Image_All.bin

amebagreen2:
	@source env.sh && python $$AMEBA_SDK/ameba.py build RTL8711F
	@dd if=/dev/zero status=none bs=1 count=$$((0x14000 + $$(wc -c < ./build_RTL8711F/amebagreen2_app.bin))) | tr '\000' '\377' > ./build_RTL8711F/Image_All.bin
	@dd if=./build_RTL8711F/amebagreen2_boot.bin status=none of=./build_RTL8711F/Image_All.bin bs=1 seek=0 conv=notrunc
	@dd if=./build_RTL8711F/amebagreen2_app.bin status=none of=./build_RTL8711F/Image_All.bin bs=1 seek=$$((0x14000)) conv=notrunc
	@md5sum ./build_RTL8711F/Image_All.bin

clean:
	@rm -rf ./build_*
