#!/usr/bin/env python3
"""Record/check the exact native inputs and HUNK outputs of the release build."""
import argparse, hashlib, json, subprocess
from pathlib import Path
from release_support import ReleaseError, atomic_json, binaries, sha, source_files, fingerprint
ROOT=Path(__file__).resolve().parents[1]
RECORD=ROOT/'BUILD-PROVENANCE.json'
def code_hash():
    files=source_files(ROOT)
    return fingerprint({n:m for n,m in files.items() if n in ('VERSION','Makefile','scripts/build-amiga.sh') or Path(n).suffix in ('.c','.h','.inc','.s','.meta','.info')})
def verify():
    if not RECORD.is_file() or RECORD.is_symlink():raise ReleaseError('Rebuild with scripts/build-amiga.sh before packaging')
    record=json.loads(RECORD.read_text())
    if record.get('version')!=(ROOT/'VERSION').read_text().strip() or record.get('code_sha256')!=code_hash():raise ReleaseError('Native inputs changed; rebuild before packaging')
    if set(record.get('binaries',{}))!=binaries(ROOT):raise ReleaseError('Incomplete native binary inventory')
    for name,digest in record['binaries'].items():
        p=ROOT/name
        if p.is_symlink() or not p.is_file() or sha(p)!=digest:raise ReleaseError('Native output changed: '+name)
    return record
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['record','verify']);p.add_argument('--compiler');p.add_argument('--flags',default='');a=p.parse_args()
    if a.action=='verify':verify();print('Native source and binaries verified')
    else:
        if not a.compiler:p.error('--compiler is required')
        files={}
        for name in sorted(binaries(ROOT)):
            path=ROOT/name
            if path.is_symlink() or not path.is_file() or path.read_bytes()[:4]!=b'\0\0\3\xf3':raise ReleaseError('Missing native HUNK: '+name)
            files[name]=sha(path)
        result=subprocess.run([a.compiler,'--version'],text=True,capture_output=True,check=True)
        atomic_json(RECORD,{'schema':1,'version':(ROOT/'VERSION').read_text().strip(),'target':'m68020-amigaos3.2','compiler':Path(a.compiler).name,'compiler_version':result.stdout.splitlines()[0],'flags':a.flags,'code_sha256':code_hash(),'binaries':files})
        verify();print('Recorded '+str(len(files))+' native binaries')
