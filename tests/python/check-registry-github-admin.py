#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
PACKAGES=ROOT.parent/'packages'

def main()->int:
    transport=(ROOT/'compiler/src/driver/registry_transport.rz').read_text(encoding='utf-8')
    registry=(ROOT/'compiler/src/driver/registry.rz').read_text(encoding='utf-8')
    workflow=(PACKAGES/'.github/workflows/registry-admin.yml').read_text(encoding='utf-8')
    assert 'fn registry_http_post(' in transport
    assert 'registry_github_admin_dispatch' in transport
    assert 'api.github.com' not in transport  # encoded literals keep accidental plaintext/token diagnostics out of binaries
    assert 'fn registry_admin_command' in registry
    for action in ('owner-add','owner-remove','yank','unyank'):
        assert action in workflow
    # Workflow must regenerate every public projection after mutable metadata changes.
    for script in ('generate_index.py','generate_api.py','generate_search.py','validate_registry.py'):
        assert script in workflow
    assert 'permissions:\n  contents: write' in workflow
    print('registry github admin contract: PASS')
    return 0
if __name__=='__main__': raise SystemExit(main())
