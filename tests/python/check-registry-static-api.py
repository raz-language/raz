#!/usr/bin/env python3
from __future__ import annotations
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
PACKAGES=ROOT.parent/'packages'

def main()->int:
    subprocess.run([sys.executable,'scripts/generate_index.py','--check'],cwd=PACKAGES,check=True)
    subprocess.run([sys.executable,'scripts/generate_api.py','--check'],cwd=PACKAGES,check=True)
    subprocess.run([sys.executable,'scripts/generate_search.py','--check'],cwd=PACKAGES,check=True)
    index=json.loads((PACKAGES/'api/v1/index.json').read_text(encoding='utf-8'))
    assert index['schema']=='raz-registry-v1'
    assert index['registry']=='raz-language/packages'
    assert index['packages']
    for row in index['packages']:
        name=row['name']; package=json.loads((PACKAGES/f'api/v1/packages/{name}.json').read_text(encoding='utf-8'))
        assert package['name']==name
        assert row['owners']==package['owners'] and row['owners']
        assert row['latest']==next((v['version'] for v in reversed(package['versions']) if not v['yanked']),None)
        assert row['versions']==[v['version'] for v in package['versions']]
        for version in package['versions']:
            detail=json.loads((PACKAGES/f"api/v1/packages/{name}/{version['version']}.json").read_text(encoding='utf-8'))
            assert detail==version
            assert isinstance(detail['yanked'],bool)
            assert detail['owners']==package['owners']
            assert detail['download'].startswith('https://raw.githubusercontent.com/raz-language/packages/main/packages/')
    search=(PACKAGES/'search.txt').read_text(encoding='utf-8').splitlines()
    assert len(search)==len(index['packages'])
    assert all(len(row.split('\t'))==4 for row in search)
    print(f"registry static api: PASS ({len(index['packages'])} packages; rich search projection)")
    return 0
if __name__=='__main__': raise SystemExit(main())
