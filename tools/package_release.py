"""Package the game for players: a launcher, the Release build, the disc importer with a bundled Python, and a README.

    python tools/package_release.py [--public]   (--public: the project's own launcher icon, for release downloads)

Needs the Release build first:

    cmake -S decompilation/cpp -B decompilation/cpp/build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
    cmake --build decompilation/cpp/build-release --target recovered_level_explorer

and Python 3.13 with Pillow, which the package carries (players install nothing). Writes
Release/Alien_Trilogy_Remastered_v<version>/ and a zip of it. Fails if any game data, or this machine's user folder, ends up
in the package.
"""
from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile

import PIL

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "decompilation/cpp/build-release"
VERSION = re.search(r'TRILOGY_VERSION_TEXT "([^"]+)"', (ROOT / "decompilation/cpp/ExplorerVersion.h").read_text()).group(1)
NAME = f"Alien_Trilogy_Remastered_v{VERSION}"
OUT = ROOT / "Release" / NAME
GAME = OUT / "game"
PYTHON = Path(sys.base_prefix)


def toolchain():
    for line in (BUILD / "CMakeCache.txt").read_text().splitlines():
        if line.startswith("CMAKE_CXX_COMPILER:"):
            return Path(line.split("=", 1)[1]).parents[1]
    raise SystemExit("Configure the Release build first (see this script's docstring)")


def copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def main():
    tc = toolchain()
    exe = BUILD / "recovered_world_explorer.exe"
    if not exe.exists():
        raise SystemExit("Build the Release game first (see this script's docstring)")
    if OUT.exists():
        shutil.rmtree(OUT)
    # the game, laid out as it expects (it finds its data four folders above the executable)
    bin_ = GAME / "decompilation/cpp/build"
    for f in (exe, BUILD / "SDL3.dll", BUILD / "TrilogyCoopTransport.exe", tc / "bin/libc++.dll", tc / "bin/libunwind.dll"):
        copy(f, bin_ / f.name)
    subprocess.run([str(tc / "bin/llvm-strip.exe"), "--strip-all", str(bin_ / "libc++.dll"), str(bin_ / "libunwind.dll"), str(bin_ / exe.name)], check=True)
    # crash reports give module+offset: keep this build's unstripped copy (line tables) locally to decode them
    symbols = ROOT / "Release" / "symbols" / f"v{VERSION}"
    symbols.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exe, symbols / exe.name)
    shutil.copytree(ROOT / "decompilation/cpp/build/shaders", bin_ / "shaders")
    (GAME / "decompilation/reports").mkdir(parents=True)
    # the importer and the level builders
    for rel in ("tools/import_pc_ps1.py", "tools/build_game_data.py", "tools/build_extermination_decks.py", "tools/stitch_maps.py",
                "tools/package/package_import.py", "DemoWork/importer.py", "DemoWork/disc_input.py", "DemoWork/chd_reader.py",
                "DemoWork/mdec.py", "DemoWork/supported_files.json", "Xbox360/tools/trilogy_level.py",
                "Xbox360/tools/make_survival_map.py", "Xbox360/tools/make_survival_lairs.py"):
        copy(ROOT / rel, GAME / rel)
    (GAME / "Xbox360/assets").mkdir(parents=True)
    # a private Python (standard library zipped, plus Pillow), so players install nothing
    py = GAME / "tools/python"
    for name in ("python.exe", "python3.dll", f"python{sys.version_info.major}{sys.version_info.minor}.dll", "vcruntime140.dll", "vcruntime140_1.dll", "LICENSE.txt"):
        if (PYTHON / name).exists():
            copy(PYTHON / name, py / name)
    for f in (PYTHON / "DLLs").iterdir():
        if f.suffix.lower() in (".pyd", ".dll") and not f.name.startswith(("_tkinter", "tcl", "tk", "_test")):
            copy(f, py / "DLLs" / f.name)
    stdlib = py / f"python{sys.version_info.major}{sys.version_info.minor}.zip"
    with zipfile.ZipFile(stdlib, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted((PYTHON / "Lib").rglob("*.py")):
            rel = f.relative_to(PYTHON / "Lib")
            if {"site-packages", "__pycache__", "test", "tests", "tkinter", "idlelib", "ensurepip", "turtledemo", "venv", "lib2to3"} & set(rel.parts):
                continue
            z.write(f, rel.as_posix())
    site = Path(PIL.__file__).parents[1]
    shutil.copytree(site / "PIL", py / "site/PIL", ignore=shutil.ignore_patterns("__pycache__"))
    dist = next(site.glob("pillow-*.dist-info"))
    (py / f"python{sys.version_info.major}{sys.version_info.minor}._pth").write_text(f"{stdlib.name}\nDLLs\nsite\n..\n../../Xbox360/tools\n../package\n")   # embedded mode leaves the script's folder off the path
    # the launcher, with the version and an application manifest (visual styles, no admin rights)
    manifest = OUT / "launcher.manifest"
    manifest.write_text('''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
<assemblyIdentity version="1.0.0.0" processorArchitecture="amd64" name="AlienTrilogyRemastered.Launcher" type="win32"/>
<trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security><requestedPrivileges><requestedExecutionLevel level="asInvoker" uiAccess="false"/></requestedPrivileges></security></trustInfo>
<application xmlns="urn:schemas-microsoft-com:asm.v3"><windowsSettings><longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware></windowsSettings></application>
<dependency><dependentAssembly><assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls" version="6.0.0.0" processorArchitecture="amd64" publicKeyToken="6595b64144ccf1df" language="*"/></dependentAssembly></dependency>
</assembly>''')
    numbers = [int(n) for n in re.findall(r"\d+", VERSION)[:3]] + [0]
    rc = OUT / "launcher.rc"
    # --public (release downloads): the project's own icon. Otherwise a local tools/package/icon.ico, if there is one.
    icon = ROOT / "tools/package/icon.ico"
    if "--public" in sys.argv or not icon.exists():
        icon = ROOT / "tools/package/public-icon.ico"
    if icon.exists():
        shutil.copy2(icon, OUT / "launcher.ico")
    rc.write_text(('1 ICON "launcher.ico"\n' if icon.exists() else "") + f'''1 24 "launcher.manifest"
1 VERSIONINFO FILEVERSION {numbers[0]},{numbers[1]},{numbers[2]},0 PRODUCTVERSION {numbers[0]},{numbers[1]},{numbers[2]},0
BEGIN BLOCK "StringFileInfo" BEGIN BLOCK "040904b0" BEGIN
 VALUE "CompanyName", "Atlantean / NobleElite"
 VALUE "FileDescription", "Alien Trilogy: Remastered launcher"
 VALUE "FileVersion", "{VERSION}"
 VALUE "ProductName", "Alien Trilogy: Remastered"
 VALUE "ProductVersion", "{VERSION}"
 VALUE "LegalCopyright", "Copyright (c) 2026 Atlantean / NobleElite (MIT License)"
END END BLOCK "VarFileInfo" BEGIN VALUE "Translation", 0x409, 1200 END END
''')
    subprocess.run([str(tc / "bin/llvm-rc.exe"), "/fo", "launcher.res", "launcher.rc"], cwd=OUT, check=True)
    launcher = OUT / "Alien Trilogy Remastered.exe"
    subprocess.run([str(tc / "bin/clang++.exe"), "-std=c++20", "-O2", "-s", "-static", "-municode", "-mwindows",
                    f"-ffile-prefix-map={ROOT}=.", f"-ffile-prefix-map={tc}=toolchain",
                    "-I", str(ROOT / "decompilation/cpp"), str(ROOT / "tools/package/launcher.cpp"), str(OUT / "launcher.res"),
                    "-o", str(launcher), "-lcomctl32", "-lcomdlg32", "-lshell32", "-luser32", "-lgdi32"], check=True)
    for f in ("launcher.rc", "launcher.res", "launcher.manifest", "launcher.ico"):
        (OUT / f).unlink(missing_ok=True)
    # documents
    (OUT / "README.md").write_text((ROOT / "tools/package/README.md").read_text(encoding="utf-8").replace("{VERSION}", VERSION), encoding="utf-8")
    copy(ROOT / "LICENSE", OUT / "LICENSE.txt")
    copy(ROOT / "Co-op Readme.txt", OUT / "Co-op Readme.txt")
    notices = {
        "SDL3 (zlib License)": ROOT / "decompilation/third_party/sdl3/SDL3-3.4.20/LICENSE.txt",
        "bgfx (BSD 2-Clause)": ROOT / "decompilation/cpp/build/_deps/bgfx-src/LICENSE",
        "bx (BSD 2-Clause)": ROOT / "decompilation/cpp/build/_deps/bx-src/LICENSE",
        "bimg (BSD 2-Clause)": ROOT / "decompilation/cpp/build/_deps/bimg-src/LICENSE",
        "Python (PSF License)": PYTHON / "LICENSE.txt",
        "Pillow (MIT-CMU License)": next((dist / n for n in ("LICENSE", "licenses/LICENSE") if (dist / n).exists()), None),
    }
    for f in sorted((ROOT / "DemoWork/third_party/llvm-licenses").glob("*")):
        notices[f"LLVM runtime: {f.stem} (Apache 2.0 with LLVM Exceptions)"] = f
    text = ("THIRD-PARTY NOTICES\n===================\n\nAlien Trilogy: Remastered includes the following components. "
            "Their licenses follow.\n")
    for title, path in notices.items():
        if path and path.exists():
            text += f"\n{'=' * 72}\n{title}\n{'=' * 72}\n\n{path.read_text(encoding='utf-8', errors='replace').strip()}\n"
    (OUT / "THIRD_PARTY_NOTICES.txt").write_text(text, encoding="utf-8")

    # checks: no game data, no disc images, no personal paths
    forbidden = set()
    for base in (ROOT / "GameData",):
        for f in base.rglob("*"):
            if f.is_file() and f.stat().st_size < 64 << 20:
                forbidden.add(hashlib.sha256(f.read_bytes()).hexdigest())
    home = Path.home()
    paths = [s.encode(e) for s in {str(home), home.as_posix()} for e in ("utf-8", "utf-16-le")]
    name = [home.name.encode(e) for e in ("utf-8", "utf-16-le")] if len(home.name) >= 4 else []
    files = sorted(p for p in OUT.rglob("*") if p.is_file())
    for f in files:
        rel = f.relative_to(OUT).as_posix()
        shader = rel.startswith("game/decompilation/cpp/build/shaders/")   # compiled shaders are .bin too
        if f.suffix.lower() in {".bin", ".cue", ".iso", ".chd", ".map", ".bnd", ".wav", ".fmv", ".rgba", ".pcm", ".str", ".pdb"} and not (shader and f.suffix.lower() == ".bin"):
            raise SystemExit("Game data or symbols in the package: " + rel)
        data = f.read_bytes()
        if len(data) >= 64 and hashlib.sha256(data).hexdigest() in forbidden:   # (empty marker files match empty game files)
            raise SystemExit("Original game file in the package: " + rel)
        if f.suffix.lower() in {".exe", ".dll", ".pyd", ".py", ".md", ".txt", ".json", ".bin"}:
            third_party = rel.startswith("game/tools/python/")   # common words like a first name can occur in their data
            for p in paths + ([] if third_party else name):
                if p.lower() in data.lower():
                    raise SystemExit(f"Personal path or user name in the package: {rel}")
    archive = OUT.parent / (NAME + ".zip")
    archive.unlink(missing_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in files:
            z.write(f, (Path(NAME) / f.relative_to(OUT)).as_posix())
    print(f"Packaged {len(files)} files: {OUT}")
    print(f"Zip: {archive} ({archive.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
