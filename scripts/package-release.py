#!/usr/bin/env python3
"""Package the complete verified project and runtime together, without private files."""
from pathlib import Path
from release_support import source_entries,runtime_entries,zip_write,verify_zip,sha
import release
ROOT=Path(__file__).resolve().parents[1]
if __name__=='__main__':
    tool=release.Release(ROOT,release.config_read(ROOT,ROOT/'work/release-config.json'));tool.check()
    entries=source_entries(ROOT)+[('AmiRocket/Runtime/'+n,d,False) for n,d in runtime_entries(ROOT).items()]
    output=ROOT/'dist';output.mkdir(exist_ok=True);path=output/('AmiRocket-'+tool.version+'-project.zip');temp=path.with_suffix('.zip.new')
    zip_write(temp,entries);verify_zip(temp,{n:d for n,d,_ in entries});temp.replace(path)
    path.with_suffix('.zip.sha256').write_text(sha(path)+'  '+path.name+'\n');print(path)
