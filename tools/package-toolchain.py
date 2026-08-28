#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Stage and archive a relocatable Raz toolchain from a qualified workspace."""
from __future__ import annotations
import argparse, gzip, hashlib, json, os, platform, shutil, stat, subprocess, tarfile, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def sha256(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024), b''): h.update(b)
    return h.hexdigest()

def version(root: Path) -> str:
    for line in (root/'compiler'/'raz.toml').read_text(encoding='utf-8').splitlines():
        if line.strip().startswith('version ='):
            return line.split('=',1)[1].strip().strip('"')
    raise RuntimeError('compiler version not found')

def platform_tag() -> str:
    sys={'Linux':'linux','Windows':'windows','Darwin':'macos'}.get(platform.system(), platform.system().lower())
    machine=platform.machine().lower()
    arch='x86_64' if machine in {'x86_64','amd64'} else 'aarch64' if machine in {'aarch64','arm64'} else machine
    return f'{sys}-{arch}'

def copy_executable(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src,dst)
    dst.chmod(dst.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

def find_tool(build: Path, name: str) -> Path | None:
    candidates=[build/'src'/'forge'/name, build/'src'/'oblink'/name, build/'src'/'forge'/f'{name}.exe', build/'src'/'oblink'/f'{name}.exe']
    for p in candidates:
        if p.is_file(): return p
    return None

def write_manifest(stage: Path):
    rows=[]
    for p in sorted(stage.rglob('*')):
        if p.is_file() and p.name != 'manifest.sha256':
            rows.append(f'{sha256(p)}  {p.relative_to(stage).as_posix()}')
    (stage/'manifest.sha256').write_text('\n'.join(rows)+'\n', encoding='utf-8', newline='\n')

def normalized_tar(stage: Path, archive: Path):
    archive.parent.mkdir(parents=True, exist_ok=True)
    with archive.open('wb') as raw:
        with gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as gz:
            with tarfile.open(fileobj=gz, mode='w', format=tarfile.PAX_FORMAT) as tf:
                for p in [stage, *sorted(stage.rglob('*'))]:
                    arcname=p.relative_to(stage.parent).as_posix()
                    info=tf.gettarinfo(str(p), arcname=arcname)
                    info.uid=0; info.gid=0; info.uname=''; info.gname=''; info.mtime=0
                    if p.is_file():
                        with p.open('rb') as f: tf.addfile(info,f)
                    else: tf.addfile(info)

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--root', type=Path, default=ROOT)
    ap.add_argument('--compiler', type=Path)
    ap.add_argument('--build-dir', type=Path)
    ap.add_argument('--output-dir', type=Path, required=True)
    ap.add_argument('--archive', action='store_true')
    args=ap.parse_args()
    root=args.root.resolve(); out=args.output_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
    ver=version(root); tag=platform_tag(); name=f'raz-{ver}-{tag}'; stage=out/name
    if stage.exists(): shutil.rmtree(stage)
    exe='.exe' if platform.system()=='Windows' else ''
    if args.compiler:
        compiler=args.compiler.resolve()
    else:
        retained=root/'target'/'bootstrap'/'release'/'bin'/f'raz{exe}'
        legacy=root/'compiler'/'target'/'release'/'bin'/f'raz-compiler{exe}'
        compiler=(retained if retained.is_file() else legacy).resolve()
    build=(args.build_dir or root/'build'/'release').resolve()
    if not compiler.is_file(): raise SystemExit(f'missing compiler: {compiler}')
    for alias in ('raz','razc','razup'):
        copy_executable(compiler, stage/'bin'/f'{alias}{exe}')
    retained_profile=root/'target'/'bootstrap'/'release'
    for tool in ('forge','forge-as','forge-dis','forge-opt','forge-codegen','forge-run','oblink'):
        src=(retained_profile/'bin'/f'{tool}{exe}') if tool == 'oblink' and (retained_profile/'bin'/f'{tool}{exe}').is_file() else find_tool(build,tool)
        if not src:
            raise SystemExit(f'missing required shipping tool: {tool} in {build}')
        copy_executable(src, stage/'bin'/src.name)
    retained_lib=root/'target'/'bootstrap'/'release'/'lib'
    legacy_lib=root/'compiler'/'target'/'release'/'lib'
    libsrc=retained_lib if retained_lib.is_dir() else legacy_lib
    if not libsrc.is_dir(): raise SystemExit(f'missing retained runtime/lib directory: {retained_lib}')
    shutil.copytree(libsrc, stage/'lib')
    shutil.copytree(root/'library', stage/'share'/'raz'/'library')
    lic=stage/'licenses'; lic.mkdir(parents=True)
    for n in ('LICENSE','NOTICE','AUTHORS.md'):
        p=root/n
        if p.is_file(): shutil.copy2(p,lic/n)
    for comp in ('forge','oblink'):
        for n in ('LICENSE','NOTICE'):
            p=root/'src'/comp/n
            if p.is_file(): shutil.copy2(p,lic/f'{comp}-{n}')
    shutil.copy2(root/'README.md', stage/'README.md')
    (stage/'VERSION').write_text(ver+'\n',encoding='utf-8',newline='\n')
    compdir=stage/'share'/'raz'/'completions'; compdir.mkdir(parents=True)
    raz=stage/'bin'/f'raz{exe}'
    for shell in ('bash','zsh','fish','powershell'):
        cp=subprocess.run([str(raz),'completions',shell],text=True,capture_output=True)
        if cp.returncode != 0: raise SystemExit(f'completion generation failed for {shell}: {cp.stderr}')
        ext={'bash':'bash','zsh':'zsh','fish':'fish','powershell':'ps1'}[shell]
        (compdir/f'raz.{ext}').write_text(cp.stdout,encoding='utf-8',newline='\n')
    build_info={'schema':'raz-toolchain-build-v1','version':ver,'platform':tag,'compiler_sha256':sha256(compiler),'tools':sorted(p.name for p in (stage/'bin').iterdir())}
    (stage/'BUILD_INFO.json').write_text(json.dumps(build_info,sort_keys=True,separators=(',',':'))+'\n',encoding='utf-8',newline='\n')
    write_manifest(stage)
    print(stage)
    if args.archive:
        archive=out/f'{name}.tar.gz'
        normalized_tar(stage,archive)
        print(archive)
        print(sha256(archive))
    return 0
if __name__=='__main__': raise SystemExit(main())
