#!/usr/bin/env python3
"""Build the supplied SDK's Eclipse CS projects without flashing the board.

--variant ras-single creates a build-local SDK copy and changes only the demo's
TL323X ranging/antenna configuration. Source lists, defines and flags come from
the original .project/.cproject. Results include RAM-resident code, not just BSS.
--summary compares the four built profiles with the current firmware ELF.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def options(tool, name):
    matches = [n for n in tool.findall('option') if n.get('name') == name]
    if not matches:
        return []
    node = matches[0]
    return [n.get('value') for n in node] if len(node) else node.get('value', '')

def project_config(sdk, name):
    directory = sdk / 'build/TL323X'
    links = {}
    for node in ET.parse(directory / '.project').findall('./linkedResources/link'):
        loc = node.findtext('locationURI')
        assert loc.startswith('PARENT-2-PROJECT_LOC/')
        links[node.findtext('name')] = sdk / loc.split('/', 1)[1]
    cfg = next(n for n in ET.parse(directory / '.cproject').findall('.//cconfiguration')
               if n.find('.//configuration').get('name') == name)
    chain = cfg.find('.//folderInfo[@resourcePath=""]/toolChain')
    assert not cfg.findall('.//fileInfo'), 'Per-file options need explicit handling'
    assert len(cfg.findall('.//folderInfo')) == 1
    assert options(chain, 'Optimization Level').endswith('.more')
    for label in ['Function sections (-ffunction-sections)', 'Data sections (-fdata-sections)',
                  'Link-time optimizer (-flto)']:
        assert options(chain, label) == 'true'
    common = ['-O2', '-g3', '-ffunction-sections', '-fdata-sections', '-flto', '-fmessage-length=0']
    def path(value):
        value = value.strip('"')
        if value == '../../..':
            return sdk
        match = re.fullmatch(r'\$\{workspace_loc:/\$\{ProjName\}(.*?)\}', value)
        if match:
            rel = match[1].lstrip('/')
            if not rel:
                return sdk
            top, _, rest = rel.partition('/')
            return links[top] / rest
        return (directory / name / value).resolve()
    tools = {}
    for key, title, other in [('c','GNU RISC-V Cross C Compiler','Other compiler flags'),
                              ('asm','GNU RISC-V Cross Assembler','Other assembler flags'),
                              ('link','GNU RISC-V Cross C Linker','Other linker flags')]:
        t = next(n for n in chain.findall('tool') if n.get('name') == title)
        flags = common + shlex.split(options(t, other))
        if key != 'link':
            flags += ['-D' + s for s in options(t, 'Defined symbols (-D)') if s]
            flags += ['-I' + str(path(s)) for s in options(t, 'Include paths (-I)')]
        else:
            assert options(t, 'Do not use standard start files (-nostartfiles)') == 'true'
            flags += ['-nostartfiles', '-Wl,--gc-sections']
            flags += ['-T' + str(path(s)) for s in options(t, 'Script files (-T)')]
            tools['libs'] = ['-L' + str(path(s)) for s in options(t, 'Library search path (-L)')]
            tools['libs'] += ['-l' + s for s in options(t, 'Libraries (-l)')]
        tools[key] = flags
    excludes = cfg.find('.//sourceEntries/entry').get('excluding').split('|')
    sources = []
    for virtual, real in links.items():
        for file in real.rglob('*') if real.is_dir() else [real]:
            if file.suffix not in ('.c', '.s', '.S'):
                continue
            v = Path(virtual) / file.relative_to(real) if real.is_dir() else Path(virtual)
            if any(v.as_posix() == x or v.as_posix().startswith(x + '/') for x in excludes):
                continue
            sources.append((v.as_posix(), file))
    return tools, sorted(sources)

def make_variant(source, output):
    target = output / 'sdk-ras-single'
    if not target.exists():
        shutil.copytree(source, target)
    changes = []
    for role in ('initiator', 'reflector'):
        rel = f'vendor/cs_{role}_demo/app_config.h'
        text = (source / rel).read_text()
        begin = text.index('#if (MCU_CORE_TYPE == MCU_CORE_TL323X)\n#define APP_IPT_SUPPORT_EN')
        end = text.index('#else', begin)
        block = text[begin:end]
        block = re.sub(r'(#define APP_IPT_SUPPORT_EN\s+)1', r'\g<1>0', block)
        block = re.sub(r'(#define APP_RAS_SUPPORT_EN\s+)0', r'\g<1>1', block)
        text = text[:begin] + block + text[end:]
        text = text.replace('BOARD_SELECT             BOARD_323X_EVK_C1T395A102',
                            'BOARD_SELECT             BOARD_323X_EVK_C1T388A20')
        (target / rel).write_text(text)
        changes.append(dict(file=rel, original_sha256=sha(source / rel), variant_sha256=sha(target / rel)))
    return target, changes

def image_info(elf, prefix):
    symbols = subprocess.check_output([str(prefix) + 'nm', '-S', '--defined-only', str(elf)], text=True)
    values, sizes, ram_symbols = {}, {}, []
    for line in symbols.splitlines():
        parts = line.split()
        if len(parts) not in (3, 4):
            continue
        address, name = int(parts[0], 16), parts[-1]
        values[name] = address
        if len(parts) == 4:
            size = int(parts[1], 16)
            sizes[name] = size
            if 0x68000 <= address < 0x90000 and parts[2] in 'bBdDsS':
                ram_symbols.append(dict(name=name, address=address, size=size))
    # PROVIDE symbols can be absent unless referenced. In the stock SDK this
    # hides _IRAM_BSS_VMA_END even though .iram_noinit_data reserves the 4608 B
    # print FIFO after .ram_code. Use allocated ELF sections as the authority.
    sections = {}
    lines = subprocess.check_output([str(prefix) + 'objdump', '-h', str(elf)], text=True).splitlines()
    for index, line in enumerate(lines):
        parts = line.split()
        if len(parts) >= 7 and parts[0].isdigit() and 'ALLOC' in lines[index + 1]:
            sections[parts[1]] = (int(parts[3], 16), int(parts[2], 16))
    ram_sections = [(address, size) for address, size in sections.values()
                    if size and 0x68000 <= address < 0x90000]
    ram_code_end = values['_RAMCODE_VMA_END']
    iram_end = max(values.get('_IRAM_BSS_VMA_END', ram_code_end), ram_code_end,
                   *(address + size for address, size in ram_sections if address < 0x80000))
    dram_end = values['_end']
    assert iram_end <= 0x80000 and dram_end <= 0x90000, 'RAM overflow'
    assert all(address + size <= dram_end for address, size in ram_sections if address >= 0x80000)
    result = dict(iram_allocated=iram_end-0x68000, dram_allocated=dram_end-0x80000,
                  ram_allocated_total=iram_end-0x68000+dram_end-0x80000,
                  iram_unused=0x80000-iram_end, dram_unassigned_before_stack=0x90000-dram_end,
                  ram_code_bytes=ram_code_end-values['_RAMCODE_VMA_START'],
                  retention_bytes=values['_RETENTION_DATA_VMA_END']-values['_RETENTION_DATA_VMA_START'],
                  iram_noinit_bytes=sections.get('.iram_noinit_data', (0, 0))[1],
                  iram_bss_bytes=sections.get('.iram_bss', (0, 0))[1],
                  data_bytes=values['_DATA_VMA_END']-values['_DATA_VMA_START'],
                  bss_bytes=values['_BSS_VMA_END']-values['_BSS_VMA_START'],
                  sdk_host_pool=sizes.get('g_cs_app_host_malloc_buffer',0),
                  freertos_heap=sizes.get('ucHeap',0),
                  largest_ram_objects=sorted(ram_symbols,key=lambda x:x['size'],reverse=True)[:25])
    return result

def configuration_values(out, gcc, flags, prefix):
    # Compile constants only into a separate object; never link this probe into
    # the firmware whose size is being compared. No target execution is needed.
    keys = ['FREERTOS_ENABLE','APP_IPT_SUPPORT_EN','APP_RAS_SUPPORT_EN','NUM_ANT_SUPPORT',
            'MAX_ANT_PATHS_SUPPORT','ACL_CENTRAL_MAX_NUM','ACL_PERIPHR_MAX_NUM',
            'STEP_NUM_PER_SUBEVENT','CS_SUBEVENT_PER_PROCEDURE_MAX','CS_STEPS_PER_PROCEDURE_MAX',
            'RAS_PROCEDURE_COUNT','PROCEDURE_DATA_LEN','CS_APP_HOST_RAS_CLIENT_MALLOC_BUFF_SIZE',
            'CS_APP_HOST_RAS_SERVER_MALLOC_BUFF_SIZE','APP_HOST_MALLOC_BUFFER_SIZE']
    src = out/'configuration-probe.c'
    src.write_text('#include "tl_common.h"\n#include "stack/ble/ble.h"\n'
                   '#include "vendor/common/channel_sounding/cs_buffer.h"\n'
                   '__attribute__((used,section(".memory_config")))\n'
                   'const unsigned int memory_config[] = {'+', '.join(keys)+'};\n')
    subprocess.run([gcc,*flags,'-fno-lto','-c',str(src),'-o',str(out/'configuration-probe.o')],check=True)
    subprocess.run([str(prefix)+'objcopy','-O','binary','-j','.memory_config',
                    str(out/'configuration-probe.o'),str(out/'configuration-probe.bin')],check=True)
    data = (out/'configuration-probe.bin').read_bytes()
    return dict(zip(keys,struct.unpack('<'+'I'*len(keys),data)))

def write_summary(output, current_elf, prefix, verification):
    # Tie every allocation result to the BIN that its hash identifies, even if
    # an ELF was rebuilt after the last successful objcopy.
    elfs = [output / f'{variant}-{role}' / 'firmware.elf'
            for variant in ('default', 'ras-single') for role in ('initiator', 'reflector')]
    with tempfile.TemporaryDirectory(prefix='check-', dir=output) as temporary:
        extracted = Path(temporary) / 'firmware.bin'
        for elf in [*elfs, current_elf]:
            subprocess.run([str(prefix) + 'objcopy', '-O', 'binary', str(elf), str(extracted)], check=True)
            if extracted.read_bytes() != elf.with_suffix('.bin').read_bytes():
                raise SystemExit(f'ELF and BIN do not match: {elf}')
    profiles = {}
    for variant in ('default', 'ras-single'):
        for role in ('initiator', 'reflector'):
            name = f'{variant}-{role}'
            directory = output / name
            saved = json.loads((directory / 'memory.json').read_text())
            if sha(directory / 'firmware.bin') != saved['firmware_sha256']:
                raise SystemExit(f'Firmware changed; rebuild {name} before summarizing')
            info = image_info(directory / 'firmware.elf', prefix)
            if any(info[key] != saved[key] for key in info):
                raise SystemExit(f'ELF changed; rebuild {name} before summarizing')
            info.update({key: saved[key] for key in (
                'firmware_bytes', 'firmware_sha256', 'compiler', 'sdk_library_sha256',
                'configuration_values', 'flashed', 'runtime_heap_measured',
                'original_boot_stack_minimum')})
            info['build_manifest'] = str(directory / 'build-manifest.json')
            profiles[name] = info
    current = image_info(current_elf, prefix)
    current_bin = current_elf.with_suffix('.bin')
    current.update(firmware_bytes=current_bin.stat().st_size,
                   firmware_sha256=sha(current_bin), elf_sha256=sha(current_elf),
                   boot_stack_reserved=2048, runtime=None)
    if verification.is_file():
        checked = json.loads(verification.read_text(encoding='utf-8-sig'))
        if checked['firmware_sha256'] != current['firmware_sha256']:
            raise SystemExit('Verification report belongs to a different firmware')
        if checked['result'] != 'PASS':
            raise SystemExit('Verification report did not pass')
        current['runtime'] = {key: checked[key] for key in (
            'heap_before', 'heap_after', 'stack_free_bytes', 'rf_result')}
        current['runtime']['source'] = str(verification)
    differences = {}
    for name, info in profiles.items():
        delta = current['ram_allocated_total'] - info['ram_allocated_total']
        differences[name] = dict(bytes=delta,
                                 percent=round(delta / info['ram_allocated_total'] * 100, 3))
    result = dict(
        units='bytes',
        basis='IRAM: maximum allocated section/symbol end - 0x68000; '
              'DRAM: _end - 0x80000; excludes runtime/startup stack gap above _end',
        profiles=profiles, current=current, current_minus_sdk=differences,
        limitations=[
            'SDK profiles are link-time results; no SDK firmware was flashed or profiled.',
            'Allocated RAM includes entire heap/pool capacities, not just live allocations.',
            'Different roles, services, algorithms, record capacities and platform versions '
            'prevent attributing the difference solely to NimBLE.',
            'Current runtime report covers local tests, not wireless CS/RAS peak usage.',
        ])
    report = output / 'comparison.json'
    report.write_text(json.dumps(result, indent=2) + '\n')
    print('profile                         IRAM B   DRAM B  total KiB  heap/pool B')
    for name, info in [*profiles.items(), ('current', current)]:
        pool = info['freertos_heap'] + info['sdk_host_pool']
        print(f"{name:30s} {info['iram_allocated']:7d} {info['dram_allocated']:8d} "
              f"{info['ram_allocated_total']/1024:10.2f} {pool:12d}")
    print(f'Report: {report}')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=ROOT/'build/telink_deps/tl_ble_sdk-custom-V4048_TL323X_CS/tl_ble_sdk')
    parser.add_argument('--toolchain', type=Path, default=ROOT/'build/tl3238_hello/toolchain/nds32le-elf-mculib-v5f')
    parser.add_argument('--output', type=Path, default=ROOT/'build/tl3238/sdk-memory-comparison')
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--role', choices=['initiator','reflector'])
    action.add_argument('--summary', action='store_true')
    parser.add_argument('--current-elf', type=Path, default=ROOT/'build/tl3238/firmware/tl3238_freertos.elf')
    parser.add_argument('--verification', type=Path, default=ROOT/'build/tl3238/cs-verification.json')
    parser.add_argument('--variant', choices=['default','ras-single'], default='default')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    original = args.sdk.resolve(); args.output = args.output.resolve(); args.output.mkdir(parents=True,exist_ok=True)
    prefix = args.toolchain.resolve() / 'bin/riscv32-elf-'
    if args.summary:
        write_summary(args.output, args.current_elf.resolve(), prefix, args.verification.resolve())
        return
    sdk, changes = make_variant(original,args.output) if args.variant=='ras-single' else (original,[])
    name = 'cs_' + args.role + '_demo'
    out = args.output / f'{args.variant}-{args.role}'; out.mkdir(exist_ok=True)
    gcc = str(prefix) + 'gcc'
    flags, sources = project_config(sdk,name)
    if args.variant=='ras-single':
        for key in ['c','asm']:
            flags[key] += ['-DNUM_ANT_SUPPORT=1','-DMAX_ANT_PATHS_SUPPORT=1','-DANTENNA_SWITCHING_AUTO_EN=0']
    commands = []
    for virtual, src in sources:
        obj = out / 'objects' / (virtual + '.o'); obj.parent.mkdir(parents=True,exist_ok=True)
        commands.append([gcc, *flags['c' if src.suffix=='.c' else 'asm'], '-c',str(src),'-o',str(obj)])
    manifest = dict(role=args.role, variant=args.variant, sdk=str(sdk), config_changes=changes,
                    eclipse_config=name, flags=flags, source_count=len(sources),
                    source_hashes={v:sha(s) for v,s in sources},
                    compiler=subprocess.check_output([gcc,'--version'],text=True).splitlines()[0],
                    sdk_library_sha256=sha(sdk/'proj_lib/liblt_TL323X.a'))
    (out/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Building {args.variant}/{args.role}: {len(sources)} sources',flush=True)
    def compile_one(command):
        result = subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        return result.returncode, result.stdout
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        outputs = list(pool.map(compile_one,commands))
    (out/'compile.log').write_text(''.join(text for _,text in outputs))
    if any(rc for rc,_ in outputs):
        raise SystemExit(f'Compilation failed: {out}/compile.log')
    elf = out/'firmware.elf'
    command = [gcc,*flags['link'],*[c[-1] for c in commands],*flags['libs'],
               '-Wl,-Map='+str(out/'firmware.map'),'-o',str(elf)]
    (out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n')
    result = subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    (out/'link.log').write_text(result.stdout)
    if result.returncode:
        raise SystemExit(f'Link failed: {out}/link.log')
    subprocess.run([str(prefix)+'objcopy','-O','binary',str(elf),str(out/'firmware.bin')],check=True)
    info = image_info(elf,prefix)
    info.update(manifest)
    info['configuration_values'] = configuration_values(out,gcc,flags['c'],prefix)
    info.update(firmware_bytes=(out/'firmware.bin').stat().st_size, firmware_sha256=sha(out/'firmware.bin'),
                flashed=False, runtime_heap_measured=False, original_boot_stack_minimum=600)
    (out/'memory.json').write_text(json.dumps(info,indent=2)+'\n')
    print(json.dumps({k:v for k,v in info.items() if k not in manifest and k!='largest_ram_objects'},indent=2),flush=True)

if __name__=='__main__':
    main()
