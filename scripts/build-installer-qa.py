#!/usr/bin/env python3
"""Prepare an isolated native Installer smoke test from verified release bytes."""
import argparse, importlib.util, os, subprocess
from pathlib import Path
import release
from release_support import runtime_inventory
ROOT=Path(__file__).resolve().parents[1]
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path);args=parser.parse_args()
    tool=release.Release(ROOT,release.config_read(ROOT,ROOT/'work/release-config.json'),output=args.output);tool.check()
    out=ROOT/'build/installer-qa';runtime=out/'AmiRocket';tools=out/'QATools';tools.mkdir(parents=True,exist_ok=True)
    for name,source in runtime_inventory(ROOT).items():
        target=runtime/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes((ROOT/source).read_bytes())
    spec=importlib.util.spec_from_file_location('installer',ROOT/'scripts/make-installer.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
    (tools/'InstallTest').write_text(mod.render(qa=True));(tools/'UninstallTest').write_text(mod.render(qa=True,uninstall=True));(tools/'UninstallRemoveTest').write_text(mod.render(qa=True,uninstall=True,remove_settings=True))
    for name,remove in [('CustomUninstallKeepTest',False),('CustomUninstallRemoveTest',True)]:
        text=mod.render(qa=True,uninstall=True,remove_settings=remove).replace('RAM:AmiRocketInstallerQA/settings/AmiRocket.prefs','RAM:AmiRocketInstallerQA/Config/AmiRocket.prefs')
        (tools/name).write_text(text)
    production=mod.render().replace('SYS:AmiRocket','RAM:AmiRocketInstallerQA/Data').replace('"HELP:"','"RAM:AmiRocketInstallerQA/Help"')
    fresh=production.replace('(set #dest (getenv "AmiRocket-InstallDir"))','(set #dest "")').replace('SYS:Utilities','RAM:AmiRocketInstallerQA/Auto').replace('(set #prefsdest (getenv "AmiRocket-PrefsDir"))','(set #prefsdest "")').replace('SYS:Prefs','RAM:AmiRocketInstallerQA/Prefs').replace('SYS:Downloads','RAM:AmiRocketInstallerQA/Downloads')
    (tools/'NoviceFreshTest').write_text(fresh)
    (tools/'NoviceUpdateTest').write_text(production.replace('SYS:Prefs','RAM:AmiRocketInstallerQA/Prefs').replace('SYS:Downloads','RAM:AmiRocketInstallerQA/Downloads'))
    cc=os.environ.get('AMIGA_CC',str(Path.home()/'Development/tools/amiga/m68k-amigaos-gcc-16.2/bin/m68k-amigaos-gcc'))
    subprocess.run([cc,'-m68020','-noixemul','-Os','-Wall','-Wextra','-s','-o',str(tools/'InstallerQA'),str(ROOT/'tools/installer_qa.c')],check=True)
    subprocess.run([cc,'-m68020','-noixemul','-Os','-Wall','-Wextra','-s','-o',str(tools/'LaunchQA'),str(ROOT/'tools/installer_launch_qa.c')],check=True)
    print(out)
