#!/usr/bin/env python3
"""Prepare an isolated native Installer smoke test from verified release bytes."""
import importlib.util, os, subprocess
from pathlib import Path
import release
from release_support import runtime_inventory
ROOT=Path(__file__).resolve().parents[1]
if __name__=='__main__':
    tool=release.Release(ROOT,release.config_read(ROOT,ROOT/'work/release-config.json'));tool.check()
    out=ROOT/'build/installer-qa';runtime=out/'AmiRocket';tools=out/'QATools';tools.mkdir(parents=True,exist_ok=True)
    for name,source in runtime_inventory(ROOT).items():
        target=runtime/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes((ROOT/source).read_bytes())
    spec=importlib.util.spec_from_file_location('installer',ROOT/'scripts/make-installer.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
    (tools/'InstallTest').write_text(mod.render(qa=True));(tools/'UninstallTest').write_text(mod.render(qa=True,uninstall=True));(tools/'UninstallRemoveTest').write_text(mod.render(qa=True,uninstall=True,remove_settings=True))
    cc=os.environ.get('AMIGA_CC',str(Path.home()/'Development/tools/amiga/m68k-amigaos-gcc-16.2/bin/m68k-amigaos-gcc'))
    subprocess.run([cc,'-m68020','-noixemul','-Os','-Wall','-Wextra','-s','-o',str(tools/'InstallerQA'),str(ROOT/'tools/installer_qa.c')],check=True)
    print(out)
