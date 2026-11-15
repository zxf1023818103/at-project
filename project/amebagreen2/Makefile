.PHONY: all amebagreen2 clean
SHELL := /bin/bash
all: amebagreen2

amebagreen2:
	@source env.sh && python $$AMEBA_SDK/ameba.py build RTL8711F
	@dd if=/dev/zero status=none bs=1 count=$$((0x400000)) | tr '\000' '\377' > ./build_RTL8711F/Image_All.bin
	@dd if=./build_RTL8711F/amebagreen2_boot.bin status=none of=./build_RTL8711F/Image_All.bin bs=1 seek=0 conv=notrunc
	@dd if=./build_RTL8711F/amebagreen2_app.bin status=none of=./build_RTL8711F/Image_All.bin bs=1 seek=$$((0x10000)) conv=notrunc
	@dd if=../lfs.bin status=none of=./build_RTL8711F/Image_All.bin bs=1 seek=$$((0x300000)) conv=notrunc
	@md5sum ./build_RTL8711F/Image_All.bin

clean:
	@rm -rf ./build_*
