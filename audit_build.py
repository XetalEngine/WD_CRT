from pathlib import Path
import hashlib, re, subprocess

dumpbin = r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe'
paths = [Path('build/wd.dll')]
lines = []
for path in paths:
    imports = subprocess.check_output([dumpbin, '/imports', str(path)], text=True)
    module = ''
    counts = {}
    for line in imports.splitlines():
        match = re.match(r'^    (.+\.dll)$', line, re.I)
        if match:
            module = match[1]
            counts[module] = 0
        if re.match(r'^\s{20,}(?:[0-9A-F]+\s+\S+|Ordinal\s+\d+)\s*$', line):
            counts[module] += 1
    lines.append(f'{path.resolve()}\nSize: {path.stat().st_size} bytes ({path.stat().st_size / 1024:.1f} KiB)\nImports: {sum(counts.values())}\n' + '\n'.join(f'  {key}: {value}' for key,value in counts.items()) + '\nSHA256: ' + hashlib.sha256(path.read_bytes()).hexdigest())
    if path == paths[0]:
        Path('build/imports.txt').write_text(imports)
        for api in ('GetForegroundWindow', 'IsWindow', 'EnumWindows', 'ScreenToClient', 'Sleep', 'SuspendThread', 'ResumeThread', 'VirtualProtect'):
            if re.search(r'\s' + api + r'\s*$', imports, re.M):
                raise SystemExit('Unexpected removed API: ' + api)
lines.append('Previously measured rewrite before the hook fix: 173568 bytes (169.5 KiB), 99 imports; SHA256 f5be2d8a14ef7dad0cb8823300bd34a3603f6684fb99565a0ee6bc43e7f3fda6.')
lines.append('Previously measured original DLL: 1534464 bytes (1498.5 KiB), 174 imports; SHA256 8bf7f802c8b678b648f15fb7d0c428166fe75608bcd18df0c8c40306db280251. This was an existing binary, not a rebuild of the original source.')
lines.append('Source audit: no ImGui, JSON library, std::atomic/std::mutex/std::thread wrappers or lazy imports. The startup thread owns rendering; one additional thread owns game updates. One SRW lock protects snapshot swaps, copied settings and stop state. No game hook is compiled. No focus, window-existence, Sleep or hook-patching imports remain. Overlay dimensions are read only at initialization. The update thread samples QueryPerformanceCounter once per iteration for feature timing; rendering has no timing sample or software frame limit. The existing Desktop presentation interval of 1 (VSync) is preserved.')
for p in Path('src').glob('*'):
    if p.suffix not in ('.h', '.cpp'): continue
    if re.search(r'ImGui|imgui|nlohmann|std::(?:atomic|mutex|thread|shared_ptr|unique_ptr)',p.read_text()):
        raise SystemExit('Unexpected removed dependency in ' + str(p))
test_imports = subprocess.check_output([dumpbin, '/imports', 'build/wd-tests.exe'], text=True)
Path('build/test-imports.txt').write_text(test_imports)
if re.search(r'\sPeekMessageW\s*$', test_imports, re.M):
    raise SystemExit('Regression test must not import PeekMessageW')
lines.append('Tests include paused/incomplete scans, a busy handoff lock, 3000 concurrent publications with settings updates, invalid-world clearing and producer shutdown. Renderer tests retain asynchronous completion and fixed-size target coverage. Test-only waits and readback are excluded from the DLL.')
report = '\n\n'.join(lines)
Path('build/size-and-imports.txt').write_text(report)
print(report)
