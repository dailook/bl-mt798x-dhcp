#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
collect_bl2_configs.py
Collect BL2 configuration files ending with `_defconfig` from the ATF configs directory.
Export summary as Markdown organized by platform (mt7981/mt7986/mt7987):
- Memory Type: If `_MT7981_BOARD_BGA=y` -> BGA_DDR3; else if `_DRAM_DDR4=y` -> DDR4; else DDR3
- Storage Type: Default SPIM_NAND; if `_BOOT_DEVICE_EMMC=y` -> EMMC
Default output file: bl2_config_summary.md

Usage:
    python collect_bl2_configs.py [config_dir] [output_filename]
    
Note: Supports both regular files and symbolic links.
"""
import os
import sys
import datetime


def parse_defconfig(filepath):
    """Parse defconfig file, following symlinks if necessary."""
    # Resolve symlink to get actual file path for reading
    actual_filepath = os.path.realpath(filepath)
    
    try:
        with open(actual_filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
    except Exception:
        # Fallback to binary-safe reading
        with open(actual_filepath, 'rb') as f:
            content = f.read().decode('utf-8', errors='ignore')

    filename = os.path.basename(filepath)
    name = filename[:-len('_defconfig')] if filename.endswith('_defconfig') else filename
    parts = name.split('_', 1)
    platform = parts[0].lower()
    model = parts[1] if len(parts) > 1 else ''

    # Memory detection: BGA (for mt7981) > DDR4 > DDR3/COMB (platform-specific default)
    if ('_MT7981_BOARD_BGA=y' in content) or ('MT7981_BOARD_BGA=y' in content):
        memory = 'BGA_DDR3'
    elif ('_DRAM_DDR4=y' in content) or ('DRAM_DDR4=y' in content):
        memory = 'DDR4'
    else:
        # mt7987 defaults to COMB (supports automatic dual-memory detection), others to DDR3
        memory = 'COMB' if platform == 'mt7987' else 'DDR3'

    # Storage detection
    if ('_BOOT_DEVICE_EMMC=y' in content) or ('BOOT_DEVICE_EMMC=y' in content):
        storage = 'EMMC'
    else:
        storage = 'SPIM_NAND'

    return {
        'filename': filename,
        'platform': platform,
        'model': model,
        'memory': memory,
        'storage': storage,
    }


def collect(dirpath):
    """Collect all defconfig files from the given directory."""
    results = []
    try:
        names = sorted(os.listdir(dirpath))
    except Exception as e:
        print(f'Error accessing directory {dirpath}: {e}', file=sys.stderr)
        return results

    for fn in names:
        # Only process files ending with _defconfig and starting with mt7981/mt7986/mt7987
        if not fn.endswith('_defconfig'):
            continue
        if not (fn.startswith('mt7981') or fn.startswith('mt7986') or fn.startswith('mt7987')):
            continue
        
        fp = os.path.join(dirpath, fn)
        # Check if it's a file or a symlink (both should be processed)
        if os.path.isfile(fp) or os.path.islink(fp):
            try:
                results.append(parse_defconfig(fp))
            except Exception as e:
                print(f'Error parsing file {fn}: {e}', file=sys.stderr)
    return results


def render_md(entries, outpath):
    """Render entries as Markdown table."""
    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    lines = []
    lines.append('# BL2 Configuration Summary\n\n')
    lines.append(f'Generated: {now}\n\n')

    for platform in ['mt7981', 'mt7986', 'mt7987']:
        lines.append(f'## {platform.upper()} Platform\n\n')
        filtered = [e for e in entries if e['platform'] == platform]
        if not filtered:
            lines.append('No data.\n\n')
            continue
        lines.append('| Filename | Model | Memory | Storage |\n')
        lines.append('|---|---|---|---|\n')
        for e in filtered:
            fname = f'`{e["filename"]}`'
            model = e['model'] or '-'
            lines.append(f'| {fname} | {model} | {e["memory"]} | {e["storage"]} |\n')
        lines.append('\n')

    with open(outpath, 'w', encoding='utf-8') as f:
        f.writelines(lines)

    return outpath


def main():
    # Default paths relative to this script location (document/tools/)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_config_dir = os.path.join(script_dir, '..', '..', 'atf-20240117-bacca82a8', 'configs')
    default_output_dir = os.path.join(script_dir, '..')
    default_outname = 'bl2_config_summary.md'
    
    config_dir = default_config_dir
    outname = default_outname
    
    if len(sys.argv) >= 2:
        config_dir = sys.argv[1]
    if len(sys.argv) >= 3:
        outname = sys.argv[2]

    entries = collect(config_dir)
    outpath = os.path.join(default_output_dir, outname)
    render_md(entries, outpath)
    print(f'Exported to: {outpath}, processed {len(entries)} configuration files.')


if __name__ == '__main__':
    main()
