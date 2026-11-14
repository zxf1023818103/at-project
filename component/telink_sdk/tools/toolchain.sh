#!/usr/bin/env bash
# Source from a TL3238 build entry point. Only the compiler is downloaded;
# vendor and open-source runtime inputs are already in component/.
if [[ -z "${TL3238_TOOLCHAIN:-}" ]]; then
    compiler_cache="$REPO_DIR/build/telink_deps/toolchain"
    if [[ -d "$REPO_DIR/build/tl3238_hello/toolchain" ]]; then
        compiler_cache="$REPO_DIR/build/tl3238_hello/toolchain"
    fi
    if [[ ! -d "$compiler_cache" ]]; then
        mkdir -p "$REPO_DIR/build/telink_deps"
        curl -fL https://doc.telink-semi.cn/tools/vsc/linux/toolchains/V53xf_Linux_X64.tar.xz \
            -o "$REPO_DIR/build/telink_deps/V53xf_Linux_X64.tar.xz"
        mkdir -p "$compiler_cache"
        tar -xJf "$REPO_DIR/build/telink_deps/V53xf_Linux_X64.tar.xz" -C "$compiler_cache"
    fi
    TL3238_TOOLCHAIN="$(python3 - "$compiler_cache" <<'PY'
from pathlib import Path
import sys
print(next(Path(sys.argv[1]).rglob('bin/riscv32-elf-gcc')).parent.parent)
PY
)"
fi
if [[ ! -x "$TL3238_TOOLCHAIN/bin/riscv32-elf-gcc" ]]; then
    echo "Missing compiler: $TL3238_TOOLCHAIN/bin/riscv32-elf-gcc" >&2
    exit 1
fi
