#!/usr/bin/env python3
"""Stage verified release bytes without replacing guest-locked drawers or settings."""
import argparse, shutil, os, tempfile
from pathlib import Path
from release_support import ReleaseError, runtime_inventory, runtime_entries, verify_lha, sha
ROOT=Path(__file__).resolve().parents[1]
def copy_files(source,dest,names):
    dest=Path(dest)
    if dest.is_symlink():raise ReleaseError('Linked staging drawer')
    dest.mkdir(parents=True,exist_ok=True)
    pairs=[]
    for name in names:
        rel=Path(name)
        if rel.is_absolute() or '..' in rel.parts:raise ReleaseError('Unsafe staging path')
        src=source/rel;target=dest/rel
        if src.is_symlink() or not src.is_file():raise ReleaseError('Missing or linked staging source')
        p=target
        while p!=dest.parent:
            if p.is_symlink():raise ReleaseError('Linked staging destination')
            p=p.parent
        if target.exists() and not target.is_file():raise ReleaseError('Staging file conflicts with drawer')
        pairs.append((src,target))
    for src,target in pairs:
        target.parent.mkdir(parents=True,exist_ok=True)
        fd,temp=tempfile.mkstemp(prefix='.amirocket-copy-',dir=target.parent);os.close(fd)
        try:
            shutil.copy2(src,temp);os.replace(temp,target)
        finally:
            if os.path.exists(temp):os.unlink(temp)
        if sha(src)!=sha(target):raise ReleaseError('Staging verification failed')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--destination',type=Path,required=True);a=p.parse_args()
    import release
    tool=release.Release(ROOT,release.config_read(ROOT,ROOT/'work/release-config.json'),output=a.output);tool.check()
    if not a.destination.is_dir() or a.destination.is_symlink():p.error('Destination must be an existing real builds drawer')
    entries=runtime_entries(ROOT);archive='AmiRocket-'+tool.version+'.lha'
    backups=tool.state/'staging-backups';backups.mkdir(exist_ok=True);backup=Path(tempfile.mkdtemp(prefix='previous-',dir=backups))
    with tempfile.TemporaryDirectory() as temp:
        staging=Path(temp)
        for name,data in entries.items():
            target=staging/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
            old=a.destination/name
            if old.is_file() and not old.is_symlink():
                saved=backup/name;saved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(old,saved)
        # Keep a customized outer drawer icon as well as the drawer inode.
        names=[n for n in entries if n!='AmiRocket.info' or not (a.destination/n).exists()]
        copy_files(staging,a.destination,names)
    copy_files(tool.out,a.destination,[archive,archive+'.sha256'])
    print('Verified installer staged at '+str(a.destination/'AmiRocket'))
if __name__=='__main__':main()
