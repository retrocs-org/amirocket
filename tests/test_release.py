#!/usr/bin/env python3
"""Offline archive, provenance, installer and publication-adapter regression tests."""
import contextlib, copy, hashlib, importlib.util, io, json, os, re, shutil, sys, tempfile, unittest, zipfile
from pathlib import Path
from unittest.mock import patch
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import release as rel
from release_support import ReleaseError, source_files, source_entries, runtime_entries, binaries, sha, zip_write, verify_zip, lha_write, verify_lha

def load_module(path,name):
    spec=importlib.util.spec_from_file_location(name,path);result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result

class FixtureRelease(rel.Release):
    def plan(self):return []
    def native(self):
        record=rel.load(self.root/'BUILD-PROVENANCE.json')
        for name,digest in record['binaries'].items():
            rel.need(sha(self.root/name)==digest,'Changed native output')
        return record

class GithubAPI:
    def __init__(self):
        self.r = None
        self.assets = []
        self.events = []
        self.bad_digest = False
        self.fail_upload = None
    def pages(self, url):
        return copy.deepcopy(self.assets if '/assets?' in url or url.endswith('/assets') else ([self.r] if self.r else []))
    def json(self, url, method='GET', data=None, file=None, missing=False):
        self.events.append((method, url))
        if method == 'GET':
            if url.endswith('/releases/latest'):
                return copy.deepcopy(self.r)
            return {'html_url': 'https://github.com/retrocs-org/amirocket', 'permissions': {'push': True}}
        if file:
            if self.fail_upload == file.name:
                raise ReleaseError('Injected interrupted upload')
            a = {'name': file.name, 'digest': None if self.bad_digest else 'sha256:' + sha(file),
                 'state': 'uploaded', 'size': file.stat().st_size}
            self.assets.append(a)
            return copy.deepcopy(a)
        if method == 'POST':
            self.r = dict(data, id=1)
            return copy.deepcopy(self.r)
        if method == 'PATCH':
            self.r.update(data)
            return copy.deepcopy(self.r)
        raise AssertionError((method, url))

class GitlabAPI:
    def __init__(self, url):
        self.url, self.r, self.files, self.events = url, None, {}, []
        self.corrupt = False
    def json(self, url, method='GET', data=None, file=None, missing=False):
        self.events.append((method, url))
        if method == 'PUT' and '/releases/' in url:
            self.r.update(data)
            self.r['upcoming_release'] = False
            return self.r
        if method == 'PUT':
            self.files[url] = file.read_bytes() + (b'bad' if self.corrupt else b'')
            return {'message': '201 Created'}
        if method == 'POST':
            self.r = data
            return data
        if '/releases/' in url:
            return self.r
        return {'web_url': self.url, 'permissions': {'group_access': {'access_level': 40}}}
    def download_hash(self, url, size):
        self.events.append(('VERIFY', url))
        data = self.files.get(url)
        return hashlib.sha256(data).hexdigest() if data is not None else None

class FTP:
    uploads = []
    fail = False
    existing = []
    def __init__(self, host, timeout):
        assert host == 'main.aminet.net'
    def login(self, user, contact):
        assert user == 'anonymous' and '@' in contact
    def cwd(self, name):
        assert name == '/new'
    def nlst(self):
        return self.existing
    def storbinary(self, command, stream):
        if self.fail and self.uploads:
            raise OSError('interrupted')
        self.uploads.append((command, stream.read()))
    def close(self):
        pass
    def __enter__(self):
        return self
    def __exit__(self, *args):
        pass

class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name).resolve()
        for name in source_files(ROOT):
            target=self.root/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,target)
        for name in binaries(self.root):
            target=self.root/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b'\0\0\3\xf3test-native-'+name.encode())
        (self.root/'build/amiga/AmiRocket.readme').write_text('test readme\n')
        rel.atomic_json(self.root/'BUILD-PROVENANCE.json',{'compiler':'fixture','compiler_version':'fixture','binaries':{n:sha(self.root/n) for n in binaries(self.root)}})
        self.config=rel.config_read(self.root,self.root/'work/release-config.json');self.tool=FixtureRelease(self.root,self.config)
    def tearDown(self):self.tmp.cleanup()
    def build(self):
        with contextlib.redirect_stdout(io.StringIO()):self.tool.build()
        return self.tool
    def manifest(self):
        self.build();m=self.tool.prepare(self.root/'docs/RELEASE-NOTES.md')
        m.update(github='https://github.com/retrocs-org/amirocket',gitlab='https://gitlab.example/org/amirocket',commit='a'*40)
        return m
    def test_exact_runtime_sdk_source_and_checksum_inventory(self):
        self.build();files=self.tool.check();self.assertEqual(len(files),6)
        verify_lha(self.tool.out/'AmiRocket-0.1.lha',runtime_entries(self.root))
        with zipfile.ZipFile(self.tool.out/self.tool.source_name) as z:
            self.assertIn('AmiRocket/LICENSE',z.namelist());self.assertIn('AmiRocket/src/amiga/gui.c',z.namelist())
            self.assertFalse(any('/build/' in n or n.endswith('.prefs') for n in z.namelist()))
        with zipfile.ZipFile(self.tool.out/self.tool.sdk_name) as z:self.assertIn('AmiRocket-PluginSDK/include/amirocket_rexx.h',z.namelist())
    def test_prepare_freezes_exact_reviewed_assets(self):
        m=self.manifest();stage=self.tool.state/'publish/outputs'
        for name,digest in m['files'].items():self.assertEqual(sha(stage/name),digest)
        self.assertEqual(m['version'],'0.1')
    def test_changed_source_refused(self):
        self.build();(self.root/'src/amiga/gui.c').write_text('changed')
        with self.assertRaises(ReleaseError):self.tool.check()
    def test_changed_binary_refused(self):
        self.build();(self.root/'build/amiga/AmiRocket').write_bytes(b'changed')
        with self.assertRaises(ReleaseError):self.tool.check()
    def test_changed_asset_and_extra_inventory_refused(self):
        self.build();p=self.tool.out/self.tool.source_name;p.write_bytes(p.read_bytes()+b'changed')
        with self.assertRaises(ReleaseError):self.tool.check()
    def test_source_rejects_symlink_and_personal_config(self):
        (self.root/'AmiRocket.prefs').write_text('private')
        with self.assertRaises(ReleaseError):source_files(self.root)
        (self.root/'AmiRocket.prefs').unlink();(self.root/'linked').symlink_to(self.root/'VERSION')
        with self.assertRaises(ReleaseError):source_files(self.root)
    def test_work_and_generated_outputs_not_packaged(self):
        (self.root/'work').mkdir();(self.root/'work/release-config.json').write_text('private')
        files=source_files(self.root);self.assertFalse(any(n.startswith(('work/','build/','outputs/')) for n in files))
    def test_deterministic_lha_zip_and_corruption_rejection(self):
        a=self.root/'a.lha';b=self.root/'b.lha';entries={'AmiRocket/a':b'abc'}
        lha_write(a,entries);lha_write(b,entries);self.assertEqual(a.read_bytes(),b.read_bytes());verify_lha(a,entries)
        a.write_bytes(a.read_bytes()[:-2]);
        with self.assertRaises(ReleaseError):verify_lha(a,entries)
        a=self.root/'a.zip';b=self.root/'b.zip';zip_write(a,[('a',b'abc',False)]);zip_write(b,[('a',b'abc',False)]);self.assertEqual(a.read_bytes(),b.read_bytes())
    def test_release_notes_cannot_be_partial_or_stale(self):
        with self.assertRaises(ReleaseError):rel.validate_release_notes(self.root,'0.1','Incomplete notes')
        notes=(self.root/'docs/RELEASE-NOTES.md').read_text().strip()
        rel.validate_release_notes(self.root,'0.1',notes)
        with self.assertRaises(ReleaseError):rel.validate_release_notes(self.root,'0.1',notes+'\n\n## Installation\nUnpack the archive.')
    def test_release_notes_require_exact_three_sections(self):
        notes=(self.root/'docs/RELEASE-NOTES.md').read_text().strip()
        self.assertEqual(re.findall(r'^#{1,6}[^\n]*',notes,re.M),['### Features','### Enhancements','### Bugs'])
        for bad in (notes.replace('### Bugs','### Fixed'), notes.replace('### Features','## Features'),
                    notes+'\n\n### Installation\n- Unpack.', 'Overview\n\n'+notes):
            (self.root/'CHANGELOG.md').write_text('# Changelog\n\n## 0.1\n\n'+bad+'\n')
            with self.assertRaises(ReleaseError):rel.validate_release_notes(self.root,'0.1',bad)

    def test_config_destination_and_build_env_validation(self):
        path=self.root/'settings.json';path.write_text(json.dumps({'env':{'GITHUB_TOKEN':'not-allowed'}}))
        with self.assertRaises(ReleaseError):rel.config_read(self.root,path)
        with self.assertRaises(ReleaseError):rel.repo_url('http://github.com/org/repo',True)
    def test_publication_requires_explicit_action_and_committed_source(self):
        with self.assertRaises(ReleaseError):self.tool.publish(['github'],False)
        self.build();self.tool.prepare(self.root/'docs/RELEASE-NOTES.md')
        with self.assertRaises(ReleaseError):self.tool.manifest_check()
    def test_github_publishes_only_after_upload_verification(self):
        m=self.manifest();api=GithubAPI();adapter=rel.GitHub(m,self.tool.state/'publish/outputs',api=api)
        adapter.preflight();adapter.publish();self.assertFalse(api.r['draft']);self.assertEqual(len(api.assets),6);self.assertEqual(api.r['name'],'AmiRocket 0.1');self.assertEqual(api.r['tag_name'],'0.1');self.assertEqual(api.r['body'],m['notes'])
    def test_github_wrong_hash_keeps_draft(self):
        m=self.manifest();api=GithubAPI();api.bad_digest=True
        with self.assertRaises(ReleaseError):rel.GitHub(m,self.tool.state/'publish/outputs',api=api).publish()
        self.assertTrue(api.r['draft'])
    def test_gitlab_checks_package_bytes_before_advertising(self):
        m=self.manifest();api=GitlabAPI(m['gitlab']);adapter=rel.GitLab(m,self.tool.state/'publish/outputs',api=api)
        adapter.preflight();adapter.publish();self.assertEqual(len(api.r['assets']['links']),6);self.assertEqual(api.r['name'],'AmiRocket 0.1');self.assertEqual(api.r['tag_name'],'0.1');self.assertEqual(api.r['description'],m['notes'])
    def test_gitlab_corruption_never_creates_release(self):
        m=self.manifest();api=GitlabAPI(m['gitlab']);api.corrupt=True
        with self.assertRaises(ReleaseError):rel.GitLab(m,self.tool.state/'publish/outputs',api=api).publish()
        self.assertIsNone(api.r)
    def test_staging_preserves_directory_locks_custom_plugins_and_config(self):
        mod=load_module(ROOT/'scripts/stage-builds.py','stage_test');src=self.root/'fixture';dst=self.root/'installed';src.mkdir();dst.mkdir()
        (src/'AmiRocket').write_bytes(b'new');(dst/'custom.arp').write_bytes(b'keep');(dst/'AmiRocket.prefs').write_text('keep')
        inode=dst.stat().st_ino;mod.copy_files(src,dst,['AmiRocket']);self.assertEqual(dst.stat().st_ino,inode)
        self.assertEqual((dst/'custom.arp').read_bytes(),b'keep');self.assertEqual((dst/'AmiRocket.prefs').read_text(),'keep')
        (dst/'AmiRocket').unlink();(dst/'AmiRocket').symlink_to(self.root/'VERSION')
        with self.assertRaises(ReleaseError):mod.copy_files(src,dst,['AmiRocket'])
    def test_generated_installer_is_balanced_and_preserves_settings(self):
        mod=load_module(ROOT/'scripts/make-installer.py','installer_test')
        self.assertEqual((ROOT/'installer/Install').read_text(),mod.render())
        for qa,remove in ((False,False),(True,False),(True,True)):
            text=mod.render(qa,remove);tokens=re.findall(r'"(?:\\.|[^"\\])*"|;[^\n]*|[()]',text);depth=0
            for token in tokens:
                if token=='(':depth+=1
                elif token==')':depth-=1;self.assertGreaterEqual(depth,0)
            self.assertEqual(depth,0)
            self.assertNotRegex(text,r'\(delete[^\n]*ENVARC:')
        self.assertIn('(choices "Update AmiRocket" "Uninstall AmiRocket")',mod.render())
        self.assertIn('(choices "Keep settings" "Remove settings") (default 0)',mod.render())
        self.assertIn('(set #remove-settings 0)',mod.render(qa=True,uninstall=True))
        self.assertIn('(set #remove-settings 1)',mod.render(qa=True,uninstall=True,remove_settings=True))
        self.assertIn('(set #prefs "RAM:',mod.render(qa=True,uninstall=True,remove_settings=True))
        self.assertIn('(if (= #remove-settings 1) (',mod.render())
        self.assertIn('ICONS',mod.render());self.assertIn('REMOVE-STARTUP',mod.render())
        self.assertNotIn('#startup',mod.render())
        self.assertNotIn('Start AmiRocket automatically',mod.render())
        self.assertNotIn(' NOSTARTUP',mod.shell())
        self.assertNotIn(' STARTUP',mod.render())
    def test_aminet_receipt_records_submission_and_does_not_repeat(self):
        m=self.manifest();folder=self.tool.state/'aminet-fixture';folder.mkdir()
        (folder/'AmiRocket.lha').write_bytes(b'fixture');(folder/'AmiRocket.readme').write_bytes(b'contact')
        m.update(aminet=True,uploader='release@example.invalid',aminet_files={n:sha(folder/n) for n in ('AmiRocket.lha','AmiRocket.readme')})
        FTP.uploads=[];FTP.fail=False;FTP.existing=[]
        adapter=rel.Aminet(m,folder,self.tool.state/'aminet-receipt.json','fixture-hash',ftp_factory=FTP)
        adapter.publish();adapter.publish();self.assertEqual(len(FTP.uploads),2)
        self.assertEqual(rel.load(self.tool.state/'aminet-receipt.json')['state'],'submitted_awaiting_moderation')
    def test_aminet_partial_submission_requires_manual_recovery(self):
        m=self.manifest();folder=self.tool.state/'aminet-fixture';folder.mkdir()
        (folder/'AmiRocket.lha').write_bytes(b'fixture');(folder/'AmiRocket.readme').write_bytes(b'contact')
        m.update(aminet=True,uploader='release@example.invalid',aminet_files={n:sha(folder/n) for n in ('AmiRocket.lha','AmiRocket.readme')})
        FTP.uploads=[];FTP.fail=True;FTP.existing=[]
        adapter=rel.Aminet(m,folder,self.tool.state/'aminet-receipt.json','fixture-hash',ftp_factory=FTP)
        with self.assertRaises(OSError):adapter.publish()
        with self.assertRaises(ReleaseError):adapter.publish()
        FTP.fail=False
    def test_workbench_icon_types_and_install_default_tool(self):
        import struct
        for name,kind in (('installer/Install.info',4),('installer/AmiRocketDrawer.info',2),('assets/icon/AmiRocket.info',3)):
            data=(ROOT/name).read_bytes();self.assertEqual(data[:2],b'\xe3\x10');self.assertEqual(data[48],kind)
        self.assertIn(b'Installer\0',(ROOT/'installer/Install.info').read_bytes())
    def test_runtime_inventory_excludes_personal_settings_and_test_binaries(self):
        names=runtime_entries(self.root)
        self.assertIn('AmiRocket/Install',names);self.assertIn('AmiRocket.info',names)
        self.assertIn('AmiRocket/Source/src/amiga/gui.c',names)
        self.assertFalse(any(n.endswith('.prefs') or 'GUIQA' in n or 'MonitorQA' in n for n in names))
if __name__=='__main__':unittest.main()
