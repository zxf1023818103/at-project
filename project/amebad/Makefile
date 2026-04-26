.PHONY: all clean
default: all

all: amebad

SHELL := /bin/bash

amebad_lp:
	@make -C ../../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_lp

amebad_hp: amebad_lp
	@make -C ../../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp

amebad: amebad_hp clean
	@mkdir out
	@cp ../../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_lp/asdk/image/km0_boot_all.bin ./out/
	@cp ../../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/image/km4_boot_all.bin ./out/
	@cp ../../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/image/km0_km4_image2.bin ./out/
	@cp ../../platform/ameba-rtos-d/project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/image/OTA_All.bin ./out/
	@dd if=/dev/zero status=none bs=1 count=$$((0x6000 + $$(wc -c < ./out/km0_km4_image2.bin))) | tr '\000' '\377' > ./out/Image_All.bin
	@dd if=./out/km0_boot_all.bin status=none of=./out/Image_All.bin bs=1 seek=0 conv=notrunc
	@dd if=./out/km4_boot_all.bin status=none of=./out/Image_All.bin bs=1 seek=$$((0x4000)) conv=notrunc
	@dd if=./out/km0_km4_image2.bin status=none of=./out/Image_All.bin bs=1 seek=$$((0x6000)) conv=notrunc
	@md5sum ./out/Image_All.bin

clean:
	@rm -rf ./out/
