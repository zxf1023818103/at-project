#!/usr/bin/env python3
"""Compile vendor BLE + Telink OTA size probes. Never flash or change partitions.

The vendor OTA server implements multi-boot A/B, not resident bootloader + app.
These builds establish a measured starting point for that later adaptation.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
spec = importlib.util.spec_from_file_location("sdk_memory", ROOT / "project/tl3238/compare_sdk_memory.py")
memory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(memory)
PROFILES = ("stock-4", "stock-1", "lean-smp", "lean-open", "lean-smp-os", "lean-open-os",
            "lean-smp-board-os")


def define(text, key, value):
    result, count = re.subn(r"(?m)^(#define\s+" + re.escape(key) + r"\s+)\S+",
                            lambda m: m[1] + str(value), text)
    if count != 1:
        raise ValueError(f"Expected one definition of {key}, got {count}")
    return result


def prepare(source, out, profile):
    sdk = out / "vendor"
    # Isolated configuration copies of component-owned TL323X inputs.
    shutil.copytree(source, sdk, dirs_exist_ok=True)
    changes = {}
    rel = "vendor/acl_peripheral_demo/app_config.h"
    text = (sdk / rel).read_text()
    if profile != "stock-4":
        changes["ACL_PERIPHR_MAX_NUM"] = 1
    if profile.startswith("lean"):
        changes.update(BLE_APP_PM_ENABLE=0, PM_DEEPSLEEP_RETENTION_ENABLE=0,
                       UI_LED_ENABLE=0, UI_KEYBOARD_ENABLE=0, TLKAPI_DEBUG_ENABLE=0,
                       APP_LOG_EN=0, APP_CONTR_EVT_LOG_EN=0, APP_HOST_EVT_LOG_EN=0,
                       APP_KEY_LOG_EN=0, APP_BUTTON_LOG_EN=0,
                       ACL_PERIPHR_SMP_ENABLE=int(profile.startswith("lean-smp")))
        shutil.copy2(HERE / "app_att.c", sdk / "vendor/acl_peripheral_demo/app_att.c")
        app = sdk / "vendor/acl_peripheral_demo/app.c"
        content = app.read_text()
        for variable in ("tbl_advData", "tbl_scanRsp"):
            content, count = re.subn(r"const u8 " + variable + r"\[\] = \{.*?\n\};",
                "const u8 " + variable + "[] = {\n"
                "    2, DT_FLAGS, 0x06,\n"
                "    17, DT_COMPLETE_LOCAL_NAME, 'T','L','3','2','3','8','-',"
                "'O','T','A','-','P','r','o','b','e',\n};", content, flags=re.S)
            assert count == 1
        app.write_text(content)
    for key, value in changes.items():
        text = define(text, key, value)
    (sdk / rel).write_text(text)
    return sdk, changes


def board_drivers(args, sdk, out, flags, sources):
    """Use the same V4.1.0 system/clock/PM + matching vendor RF ABI as the app."""
    chip = out / "board/drivers/TL323X"
    platform_chip = args.platform / "drivers"
    shutil.copytree(platform_chip, chip, dirs_exist_ok=True)
    shutil.copytree(sdk / "drivers/TL323X/ext_driver", chip / "ext_driver", dirs_exist_ok=True)
    shutil.copytree(sdk / "drivers/TL323X/lib/include/rf", chip / "lib/include/rf", dirs_exist_ok=True)
    prefix = args.toolchain / "bin/riscv32-elf-"
    archive = out / "libtl3238_ble.a"
    shutil.copy2(sdk / "proj_lib/liblt_TL323X.a", archive)
    members = subprocess.check_output([str(prefix) + "gcc-ar", "t", str(chip / "lib/libdriver.a")], text=True).splitlines()
    subprocess.run([str(prefix) + "gcc-ar", "d", str(archive),
                    *[m for m in members if m and not m.startswith("rf_")]], check=True)
    for key in ("c", "asm"):
        flags[key] = ["-I" + str(out / "board"), "-I" + str(chip),
                      "-I" + str(chip / "lib/include"), *flags[key]]
    flags["libs"] = [str(archive) if f == "-llt_TL323X" else f for f in flags["libs"]]
    flags["libs"].append(str(chip / "lib/libdriver.a"))
    sources = [(v, p) for v, p in sources if not v.startswith("drivers/")]
    sources += [("board/drivers/" + p.relative_to(chip).as_posix(), p) for p in sorted(chip.rglob("*.c"))]
    main = sdk / "vendor/acl_peripheral_demo/main.c"
    content = main.read_text()
    begin = content.index("#elif (MCU_CORE_TYPE == MCU_CORE_TL323X)")
    end = content.index("#elif", begin + 1)
    section = content[begin:end]
    assert "sys_init(DCDC_1P25_LDO_1P8," in section
    content = content[:begin] + section.replace("sys_init(DCDC_1P25_LDO_1P8,", "sys_init(LDO_1P25_LDO_1P8,") + content[end:]
    main.write_text(content)
    linker = sdk / "boot/TL323X/boot_general.link"
    content = linker.read_text().replace("_STACK_TOP-600", "_STACK_TOP-2048")
    content = content.replace("_DLM_USE_SIZE = _BSS_VMA_END", "_DLM_USE_SIZE = _end")
    content += '\nASSERT(_IRAM_BSS_VMA_END <= 0x80000, "IRAM buffers overflow");\n'
    content += 'ASSERT(BIN_SIZE <= 0x20000 - 19, "128 KiB probe overflow including CRC/padding");\n'
    linker.write_text(content)
    return sources


def configuration(out, gcc, flags):
    keys = ["ACL_CENTRAL_MAX_NUM", "ACL_PERIPHR_MAX_NUM", "ACL_PERIPHR_SMP_ENABLE",
            "BLE_OTA_SERVER_ENABLE", "BLE_APP_PM_ENABLE", "APP_FLASH_PROTECTION_ENABLE",
            "TLKAPI_DEBUG_ENABLE", "FREERTOS_ENABLE", "UI_KEYBOARD_ENABLE"]
    src = out / "configuration.c"
    src.write_text('#include "tl_common.h"\n')
    result = subprocess.check_output([gcc, *flags, "-dM", "-E", str(src)], text=True)
    (out / "configuration.macros").write_text(result)
    values = {}
    for key in keys:
        match = re.search(r"(?m)^#define " + key + r"\s+(\d+)$", result)
        assert match, key
        values[key] = int(match[1])
    assert values["BLE_OTA_SERVER_ENABLE"] == 1
    assert values["FREERTOS_ENABLE"] == 0
    assert values["APP_FLASH_PROTECTION_ENABLE"] == 1
    return values


def check_binary(binary):
    """Independently check the SDK postprocessor's complete OTA file."""
    data = binary.read_bytes()
    length = struct.unpack_from("<I", data, 0x18)[0]
    crc = struct.unpack_from("<I", data, len(data) - 4)[0]
    assert length == len(data), "Image header length disagrees with file size"
    assert len(data) % 16 == 4, "Expected 16-byte payload alignment plus CRC32"
    assert data[6:8] == b"\x5d\x02", "Expected SDK CRC32 image format"
    assert data[0x20:0x24] == b"KNLT", "Missing Telink ROM boot marker"
    assert data[0x15] == 0x5a, "Flash-protection image flag was lost"
    # SDK reflected CRC32 uses init=FFFFFFFF and no final XOR.
    assert crc == (zlib.crc32(data[:-4]) ^ 0xffffffff), "Image CRC32 mismatch"
    return dict(header_length=length, crc32=f"{crc:08x}", verified=True)


def build(args, profile):
    out = args.output / profile
    out.mkdir(parents=True, exist_ok=True)
    # A failed rebuild must not leave an old result appearing current.
    (out / "memory.json").unlink(missing_ok=True)
    (args.output / "comparison.json").unlink(missing_ok=True)
    sdk, changes = prepare(args.sdk, out, profile)
    # Source lists and flags were extracted once from the pinned upstream
    # project. Builds do not parse or depend on an SDK Eclipse project.
    config = json.loads((sdk / "acl_peripheral.json").read_text())
    flags = {key: [v.replace("@BLE@", str(sdk)) for v in values]
             for key, values in config["flags"].items()}
    sources = [(virtual, sdk / relative) for virtual, relative in config["sources"]]
    if "-board-" in profile:
        sources = board_drivers(args, sdk, out, flags, sources)
    if profile.endswith("-os"):
        for key in ("c", "asm", "link"):
            flags[key] = ["-Os" if f == "-O2" else f for f in flags[key]]
    prefix = args.toolchain / "bin/riscv32-elf-"
    gcc = str(prefix) + "gcc"
    cfg = configuration(out, gcc, flags["c"])
    commands = []
    for virtual, source in sources:
        obj = out / "objects" / (virtual + ".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        commands.append([gcc, *flags["c" if source.suffix == ".c" else "asm"],
                         "-c", str(source), "-o", str(obj)])
    manifest = dict(profile=profile, sdk=str(args.sdk), overrides=changes, flags=flags,
                    compiler=subprocess.check_output([gcc, "--version"], text=True).splitlines()[0],
                    sdk_library_sha256=memory.sha(sdk / "proj_lib/liblt_TL323X.a"),
                    configuration=cfg, source_hashes={v: memory.sha(p) for v, p in sources},
                    input_config_sha256=memory.sha(args.sdk / "vendor/acl_peripheral_demo/app_config.h"),
                    linker_sha256=memory.sha(sdk / "boot/TL323X/boot_general.link"))
    if "-board-" in profile:
        manifest.update(platform=str(args.platform),
                        platform_driver_sha256=memory.sha(out / "board/drivers/TL323X/lib/libdriver.a"),
                        patched_ble_library_sha256=memory.sha(out / "libtl3238_ble.a"))
    (out / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Building {profile}: {len(commands)} sources", flush=True)

    def compile_one(command):
        return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(compile_one, commands))
    (out / "compile.log").write_text("".join(r.stdout for r in results))
    if any(r.returncode for r in results):
        raise RuntimeError(f"Compile failed: {out}/compile.log")
    elf = out / "firmware.elf"
    command = [gcc, *flags["link"], *[c[-1] for c in commands], *flags["libs"],
               "-Wl,-Map=" + str(out / "firmware.map"), "-o", str(elf)]
    (out / "link-command.json").write_text(json.dumps(command, indent=2) + "\n")
    result = compile_one(command)
    (out / "link.log").write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f"Link failed: {out}/link.log")
    binary = out / "firmware.bin"
    subprocess.run([str(prefix) + "objcopy", "-O", "binary", str(elf), str(binary)], check=True)
    raw_bytes = binary.stat().st_size
    # Use the original SDK's CRC/image postprocessor, as its Eclipse build does.
    checker = sdk / "check_fw"
    checker.chmod(checker.stat().st_mode | 0o100)
    result = compile_one([str(checker), str(binary)])
    (out / "check-fw.log").write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f"Firmware postprocess failed: {out}/check-fw.log")
    symbols = subprocess.check_output([str(prefix) + "nm", "-S", "--defined-only", str(elf)], text=True)
    (out / "symbols.txt").write_text(symbols)
    # Ensure OTA has not been made small by garbage-collecting its real handler.
    required = ["otaWrite", "ota_save_data", "blt_ota_procOtaFinish",
                "blc_ota_initOtaServer_module", "blc_ota_setOtaProcessTimeout"]
    present = {s: bool(re.search(r"\b" + s + r"(?:\.[^\s]+)?$", symbols, re.M)) for s in required}
    assert all(present.values()), present
    forbidden = ["vTaskStartScheduler", "nimble_port_run", "mcumgr_init"]
    assert not any(re.search(r"\b" + s + r"$", symbols, re.M) for s in forbidden)
    info = memory.image_info(elf, prefix)
    info.update(profile=profile, configuration=cfg, raw_image_bytes=raw_bytes,
                firmware_bytes=binary.stat().st_size, firmware_sha256=memory.sha(binary),
                required_ota_symbols=present, flashed=False, runtime_tested=False,
                binary_checks=check_binary(binary), resident_bootloader=False,
                linker_stack_guard_bytes=2048 if "-board-" in profile else 600,
                free_ram_before_stack=info["iram_unused"] + info["dram_unassigned_before_stack"])
    (out / "memory.json").write_text(json.dumps(info, indent=2) + "\n")
    print(json.dumps({k: info[k] for k in ["profile", "firmware_bytes", "iram_allocated",
          "dram_allocated", "ram_allocated_total", "free_ram_before_stack"]}), flush=True)
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor", dest="sdk", type=Path,
                        default=ROOT / "component/telink_sdk/ble/standard")
    parser.add_argument("--toolchain", type=Path,
                        default=ROOT / "build/tl3238_hello/toolchain/nds32le-elf-mculib-v5f")
    parser.add_argument("--platform", type=Path, default=ROOT / "component/telink_sdk/platform")
    parser.add_argument("--output", type=Path, default=ROOT / "build/tl3238_bootloader")
    parser.add_argument("--profile", choices=("all", *PROFILES), default="all")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    args.sdk, args.output, args.toolchain = args.sdk.resolve(), args.output.resolve(), args.toolchain.resolve()
    args.platform = args.platform.resolve()
    if args.output == args.sdk or args.output.is_relative_to(args.sdk):
        parser.error("Output must be outside the source SDK")
    profiles = PROFILES if args.profile == "all" else [args.profile]
    for profile in profiles:
        build(args, profile)
    # A single-profile rebuild must not discard other measured profiles.
    results = [json.loads((args.output / p / "memory.json").read_text()) for p in PROFILES
               if (args.output / p / "memory.json").exists()]
    (args.output / "comparison.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
