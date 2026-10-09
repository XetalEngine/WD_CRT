from pathlib import Path
import ast
import re
import subprocess
import sys

root = Path(__file__).resolve().parent
dll = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/wd.dll'
tokens = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|^[ \t]*\#[^\n]*|(?:L|u8|u|U)?\'(?:\\[\s\S]|[^\'\\])*\'|(?P<string>(?:L|u8|u|U)?"(?:\\[\s\S]|[^"\\])*")', re.M)
wrapped = 0
values = set()
unwrapped = []
compile_only = []
for path in sorted((root / 'src').glob('*')):
    if path.suffix not in ('.h', '.cpp') or path.name == 'xor_text.h':
        continue
    source = path.read_text()
    for match in tokens.finditer(source):
        if not match.group('string'):
            continue
        literal = match.group()
        line_start = source.rfind('\n', 0, match.start()) + 1
        before = source[line_start:match.start()]
        location = f'{path.relative_to(root)}:{source.count(chr(10), 0, match.start()) + 1}'
        if 'static_assert(' in before or path.name == 'stdafx.h' and 'sizeof(' in before:
            compile_only.append(location)
            continue
        if not re.search(r'xor_(?:text|a)\(\s*$', before):
            unwrapped.append(location)
            continue
        wrapped += 1
        wide = literal.startswith('L')
        value = ast.literal_eval(literal[literal.index('"'):])
        values.add((value, wide))

binary = dll.read_bytes()
dumpbin = r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe'
imports = subprocess.check_output([dumpbin, '/imports', str(dll)], text=True)
import_names = re.findall(r'^\s{20,}[0-9A-F]+\s+(\S+)\s*$', imports, re.M)
import_ranges = []
for name in import_names:
    encoded = (name + '\0').encode('ascii')
    start = binary.find(encoded)
    while start >= 0:
        import_ranges.append((start, start + len(encoded), name))
        start = binary.find(encoded, start + 1)
found = []
import_matches = []
checked = 0
for value, wide in sorted(values):
    # Short byte sequences commonly occur in machine code by coincidence.
    if len(value) < 4:
        continue
    checked += 1
    encoded = (value + '\0').encode('utf-16le' if wide else 'utf-8')
    start = binary.find(encoded)
    while start >= 0:
        imported = next((name for first, last, name in import_ranges if not wide and first <= start and start + len(encoded) <= last), None)
        if imported:
            import_matches.append(f'{value!r} is part of PE import {imported}')
        else:
            found.append(('wide ' if wide else '') + repr(value))
        start = binary.find(encoded, start + 1)
report = f'DLL: {dll.resolve()}\nWrapped runtime literal occurrences: {wrapped}\nCompile-only sizeof/static_assert literals: {len(compile_only)}\nUnique source strings checked in binary (4+ characters, null-terminated): {checked}\nUnwrapped runtime literals: {len(unwrapped)}\nPlaintext source strings found: {len(found)}\n'
if unwrapped:
    report += '\n'.join(unwrapped) + '\n'
if found:
    report += '\n'.join(found) + '\n'
if import_matches:
    report += 'Import-name substring matches (loader metadata):\n' + '\n'.join(import_matches) + '\n'
report += 'Scope: application-owned runtime literals in src. Includes, comments, diagnostics and literal lengths are compile-time syntax. PE import/export names and CRT-generated messages are outside xor.h; runtime-generated names/labels are not compile-time literals. Strings shorter than four characters are covered by the source check, not a conclusive binary search.\n'
(root / 'build/strings-audit.txt').write_text(report)
print(report)
sys.exit(bool(unwrapped or found))
