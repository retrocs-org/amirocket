#!/usr/bin/env python3
"""Prepare Aminet names/readme from the already verified release bytes."""
import argparse, os, re, shutil, sys, textwrap
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
VERSION=(ROOT/'VERSION').read_text().strip()
def readme(author,uploader,archive_name):
    if any(c in author+uploader for c in '\r\n'):raise ValueError('Contacts must be single lines')
    changes=(ROOT/'CHANGELOG.md').read_text()
    section=re.search(r'^## '+re.escape(VERSION)+r'[^\n]*\n(.*?)(?=^## |\Z)',changes,re.M|re.S)
    if not section:raise ValueError('Missing changelog version')
    notes=section.group(1).replace('### ','').replace('`','').strip()
    text=f'''Short:        Native Rocket.Chat client for AmigaOS 3.2
Author:       {author}
Uploader:     {uploader}
Type:         comm/net
Version:      {VERSION}
Architecture: m68k-amigaos >= 3.2.0
Requires:     68020+, AmigaOS 3.2, TCP/IP stack, AmiSSL 5.1+ for HTTPS

AmiRocket is a native ReAction Rocket.Chat client with multiple servers,
separate Preferences, plugins and ARexx dock integration.

INSTALLATION
Unpack {archive_name}, open the AmiRocket drawer and double-click Install.
Choose install/update and the parent drawer. AmiRocketPrefs stays beside
the client. Existing ENVARC:AmiRocket.prefs and custom plugins are preserved.
Workbench startup is optional. Close the client and Preferences before updating.
The uninstaller removes only managed files and keeps configuration/custom files.
Read AmiRocket.guide for usage, dependencies and removal instructions.

LICENCE AND SOURCE
GPL-3.0-or-later. See LICENSE and COPYRIGHT, including third-party notices.
The Source drawer in this archive and AmiRocket-{VERSION}-source.zip provide
build scripts and tests. The plugin SDK is a separate versioned ZIP.
No personal settings or credentials are included in these archives.

LIMITS
This release uses polling, Latin-1 text with readable Unicode fallbacks,
a maximum 8 MiB download size and 2 MiB encoded inline preview size.
Server permissions still apply. Teams/discussions are not implemented.
Hardware/runtime testing is separate from a successful release build.

CHANGES
{notes}
'''
    replacements={'\u2014':'-','\u2013':'-','\u2019':"'",'\u201c':'"','\u201d':'"','ö':'oe','Ö':'Oe','ä':'ae','å':'a','é':'e'}
    for a,b in replacements.items():text=text.replace(a,b)
    return text.encode('ascii','strict')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--author',default='AmiRocket contributors');p.add_argument('--uploader',default='');p.add_argument('--runtime-readme',type=Path);p.add_argument('--output',type=Path,default=ROOT/'outputs'/VERSION);a=p.parse_args()
    if a.runtime_readme:
        a.runtime_readme.parent.mkdir(parents=True,exist_ok=True);a.runtime_readme.write_bytes(readme(a.author,a.uploader,'AmiRocket-'+VERSION+'.lha'));return
    if not re.fullmatch(r'[^\s@<>]+@[^\s@<>]+\.[^\s@<>]+',a.uploader):p.error('Supply a valid --uploader contact for Aminet')
    sys.path.insert(0,str(ROOT/'scripts'));import release
    tool=release.Release(ROOT,release.config_read(ROOT,ROOT/'work/release-config.json'),output=a.output);tool.check()
    folder=tool.state/'aminet-manual';folder.mkdir(exist_ok=True)
    shutil.copyfile(tool.out/('AmiRocket-'+VERSION+'.lha'),folder/'AmiRocket.lha')
    (folder/'AmiRocket.readme').write_bytes(readme(a.author,a.uploader,'AmiRocket.lha'));print(folder)
if __name__=='__main__':main()
