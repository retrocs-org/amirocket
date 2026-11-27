#!/usr/bin/env python3
"""Build the PHP website; downloads are resolved from GitHub at request time."""
import argparse
import hashlib
import json
import subprocess
import re
import shutil
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
        self.ids = set()
    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            if attrs['id'] in self.ids:
                raise ValueError('Duplicate HTML id: ' + attrs['id'])
            self.ids.add(attrs['id'])
        for name in ('href', 'src'):
            if attrs.get(name):
                self.links.append(attrs[name])

def build():
    out = HERE / 'dist'
    out.mkdir(exist_ok=True)
    for name in ('styles.css', 'app.js', 'changelog.md'):
        shutil.copy2(HERE / name, out / name)
    shutil.copytree(HERE / 'assets', out / 'assets', dirs_exist_ok=True)
    shutil.copy2(HERE / 'index.php', out / 'index.php')
    (out / 'release.json').unlink(missing_ok=True)
    # Remove old generated local downloads: GitHub is the sole download source.
    if (out / 'downloads').exists():
        shutil.rmtree(out / 'downloads')
    for folder in ('includes', 'licenses'):
        shutil.copytree(HERE / folder, out / folder, dirs_exist_ok=True)
    shutil.copy2(HERE / 'releases.json', out / 'releases.json')
    (out / 'index.html').unlink(missing_ok=True)
    (out / 'docs').mkdir(exist_ok=True)
    for dest, source in {
        'sdk.md': 'sdk/README.md', 'AmiRocket.guide': 'docs/AmiRocket.guide',
        'changelog.md': 'CHANGELOG.md', 'releasing.md': 'docs/RELEASING.md', 'LICENSE': 'LICENSE'
    }.items():
        shutil.copy2(ROOT / source, out / 'docs' / dest)
    # Include SDK linked headers/scripts so its relative documentation links work.
    for folder in ('include', 'examples'):
        if (ROOT / 'sdk' / folder).exists():
            shutil.copytree(ROOT / 'sdk' / folder, out / 'docs' / folder, dirs_exist_ok=True)
    parser = Links()
    rendered = subprocess.run(['php', str(out / 'index.php')], check=True, capture_output=True, text=True).stdout
    if '<?php' in rendered or '<?= ' in rendered:
        raise ValueError('Unrendered PHP in page output')
    parser.feed(rendered)
    for link in parser.links:
        target = urlsplit(link)
        if target.scheme or target.netloc:
            if target.scheme != 'https' or target.netloc not in {'github.com', 'www.rocket.chat'}:
                raise ValueError('Unexpected external link: ' + link)
            continue
        if target.path and not (out / unquote(target.path)).is_file():
            raise ValueError('Broken site link: ' + link)
        if target.fragment and target.fragment not in parser.ids:
            raise ValueError('Broken section anchor: ' + link)
    print(f'Built AmiRocket website: {out}')
    print(f'Checked {len(parser.links)} page links and assets; release downloads use GitHub.')
    return out

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    args = parser.parse_args()
    build()
