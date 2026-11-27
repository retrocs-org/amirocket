#!/usr/bin/env python3
"""Offline archive, provenance, installer and publication-adapter regression tests."""
import contextlib, copy, hashlib, importlib.util, io, json, os, re, shutil, struct, subprocess, sys, tempfile, time, unittest, zipfile
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
        if '/releases/permalink/latest' in url:
            raise ReleaseError('Fixture: GitLab permalink returns HTTP 302')
        if '/releases?order_by=released_at&sort=desc&per_page=1' in url:
            return [self.r] if self.r else []
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
        self.config=rel.config_read(self.root,self.root/'work/release-config.json');self.tool=FixtureRelease(self.root,self.config);self.version=self.tool.version
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
        verify_lha(self.tool.out/f'AmiRocket-{self.version}.lha',runtime_entries(self.root))
        with zipfile.ZipFile(self.tool.out/self.tool.source_name) as z:
            self.assertIn('AmiRocket/LICENSE',z.namelist());self.assertIn('AmiRocket/src/amiga/gui.c',z.namelist())
            self.assertFalse(any('/build/' in n or n.endswith('.prefs') for n in z.namelist()))
        with zipfile.ZipFile(self.tool.out/self.tool.sdk_name) as z:self.assertIn('AmiRocket-PluginSDK/include/amirocket_rexx.h',z.namelist())
    def test_prepare_freezes_exact_reviewed_assets(self):
        m=self.manifest();stage=self.tool.state/'publish/outputs'
        for name,digest in m['files'].items():self.assertEqual(sha(stage/name),digest)
        self.assertEqual(m['version'],self.version)
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
        (self.root/'AmiRocket.prefs').unlink()
        for name in ('AmiRocket.drafts','AmiRocket.drafts.new','AmiRocket.drafts.bak','amirocket.DRAFTS'):
            (self.root/name).write_text('private draft text')
            with self.assertRaises(ReleaseError):source_files(self.root)
            (self.root/name).unlink()
        (self.root/'linked').symlink_to(self.root/'VERSION')
        with self.assertRaises(ReleaseError):source_files(self.root)
    def test_work_and_generated_outputs_not_packaged(self):
        (self.root/'work').mkdir();(self.root/'work/release-config.json').write_text('private')
        files=source_files(self.root);self.assertFalse(any(n.startswith(('work/','build/','outputs/')) for n in files))
    def test_deterministic_lha_zip_and_corruption_rejection(self):
        a=self.root/'a.lha';b=self.root/'b.lha';entries={'AmiRocket/a':b'abc'}
        lha_write(a,entries,timestamp=1700000000);lha_write(b,entries,timestamp=1700000000);self.assertEqual(a.read_bytes(),b.read_bytes());verify_lha(a,entries)
        a.write_bytes(a.read_bytes()[:-2]);
        with self.assertRaises(ReleaseError):verify_lha(a,entries)
        a=self.root/'a.zip';b=self.root/'b.zip';zip_write(a,[('a',b'abc',False)],timestamp=1700000000);zip_write(b,[('a',b'abc',False)],timestamp=1700000000);self.assertEqual(a.read_bytes(),b.read_bytes())
    def test_archive_members_use_current_packaging_clock(self):
        stamp=time.mktime((2026,11,20,12,34,57,0,0,-1)); expected=(2026,11,20,12,34,56)
        lha=self.root/'clock.lha'; zpath=self.root/'clock.zip'
        with patch('release_support.time.time',return_value=stamp):
            lha_write(lha,{'AmiRocket/a':b'contents'})
            zip_write(zpath,[('a',b'contents',False)])
        with zipfile.ZipFile(zpath) as z:self.assertEqual(z.getinfo('a').date_time,expected)
        dos=struct.unpack_from('<I',lha.read_bytes(),15)[0]
        actual=((dos>>25)+1980,(dos>>21)&15,(dos>>16)&31,(dos>>11)&31,(dos>>5)&63,(dos&31)*2)
        self.assertEqual(actual,expected)
        self.assertGreaterEqual(lha.stat().st_mtime,time.time()-60)
        verify_lha(lha,{'AmiRocket/a':b'contents'})

    def test_external_compressor_staging_uses_current_file_dates(self):
        stamp=time.mktime((2026,11,20,12,34,56,0,0,-1)); expected=(2026,11,20,12,34,56)
        archive=self.root/'external-clock.lha';entries={'AmiRocket/a':b'contents'*100}
        def compress(args,cwd,**kwargs):
            self.assertEqual(time.localtime((Path(cwd)/'AmiRocket/a').stat().st_mtime)[:6],expected)
            lha_write(Path(args[2]),entries,timestamp=stamp)
            return subprocess.CompletedProcess(args,0)
        with patch('release_support.time.time',return_value=stamp), patch('release_support.subprocess.run',side_effect=compress):
            lha_write(archive,entries,tool='fixture-lha-compressor')
        verify_lha(archive,entries)

    def test_independent_lha_reader_extracts_current_file_dates(self):
        tool=shutil.which('lha')
        if not tool:self.skipTest('Independent LhA reader is not installed')
        stamp=time.mktime((2026,11,20,12,34,56,0,0,-1));expected=(2026,11,20,12,34,56)
        archive=self.root/'reader-clock.lha';lha_write(archive,{'AmiRocket/a':b'contents'},timestamp=stamp)
        target=self.root/'unpacked-clock';target.mkdir()
        subprocess.run([tool,'xq2',str(archive)],cwd=target,check=True,capture_output=True)
        self.assertEqual(time.localtime((target/'AmiRocket/a').stat().st_mtime)[:6],expected)

    def test_release_notes_cannot_be_partial_or_stale(self):
        with self.assertRaises(ReleaseError):rel.validate_release_notes(self.root,self.version,'Incomplete notes')
        notes=(self.root/'docs/RELEASE-NOTES.md').read_text().strip()
        rel.validate_release_notes(self.root,self.version,notes)
        with self.assertRaises(ReleaseError):rel.validate_release_notes(self.root,self.version,notes+'\n\n## Installation\nUnpack the archive.')
    def test_release_notes_require_exact_three_sections(self):
        notes=(self.root/'docs/RELEASE-NOTES.md').read_text().strip()
        self.assertEqual(re.findall(r'^#{1,6}[^\n]*',notes,re.M),['### Features','### Enhancements','### Bugs'])
        for bad in (notes.replace('### Bugs','### Fixed'), notes.replace('### Features','## Features'),
                    notes+'\n\n### Installation\n- Unpack.', 'Overview\n\n'+notes):
            (self.root/'CHANGELOG.md').write_text('# Changelog\n\n## '+self.version+'\n\n'+bad+'\n')
            with self.assertRaises(ReleaseError):rel.validate_release_notes(self.root,self.version,bad)

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
        adapter.preflight();adapter.publish();self.assertFalse(api.r['draft']);self.assertEqual(len(api.assets),6);self.assertEqual(api.r['name'],'AmiRocket '+self.version);self.assertEqual(api.r['tag_name'],self.version);self.assertEqual(api.r['body'],m['notes'])
    def test_github_wrong_hash_keeps_draft(self):
        m=self.manifest();api=GithubAPI();api.bad_digest=True
        with self.assertRaises(ReleaseError):rel.GitHub(m,self.tool.state/'publish/outputs',api=api).publish()
        self.assertTrue(api.r['draft'])
    def test_gitlab_checks_package_bytes_before_advertising(self):
        m=self.manifest();api=GitlabAPI(m['gitlab']);adapter=rel.GitLab(m,self.tool.state/'publish/outputs',api=api)
        adapter.preflight();adapter.publish();self.assertEqual(len(api.r['assets']['links']),6);self.assertEqual(api.r['name'],'AmiRocket '+self.version);self.assertEqual(api.r['tag_name'],self.version);self.assertEqual(api.r['description'],m['notes'])
        self.assertTrue(any('/releases?order_by=released_at&sort=desc&per_page=1' in url for _,url in api.events))
        self.assertFalse(any('/releases/permalink/latest' in url for _,url in api.events))
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
            self.assertNotRegex(text,r'\(delete[^\n]*ENVARC:AmiRocket\.prefs')
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
    def test_novice_uses_detected_or_default_directory_without_prompt(self):
        mod=load_module(ROOT/'scripts/make-installer.py','novice_test');text=mod.render()
        self.assertIn('(set #novice (= @user-level 0))',text)
        self.assertIn('(set #dest (getenv "AmiRocket-InstallDir"))',text)
        self.assertIn('(set #helpdest "HELP:")',text)
        self.assertIn('(copyfiles (source "AmiRocket.guide") (dest #helpdest)',text)
        self.assertIn('(if (= #dest "") (set #dest "SYS:Utilities"))',text)
        self.assertIn('(set #dest (if #novice #dest (askdir',text)
        self.assertNotIn('Choose the parent drawer',text)
        self.assertLess(text.index('(set #novice'),text.index('(user 1)'))
        self.assertIn(' RECORD',text);self.assertIn(' RECORD',mod.shell())

    def test_split_destinations_and_expert_configuration(self):
        mod=load_module(ROOT/'scripts/make-installer.py','split_test');text=mod.render();shell=mod.shell()
        self.assertIn('(set #prefsdest "SYS:Prefs")',text)
        self.assertIn('Choose the AmiRocketPrefs drawer',text)
        self.assertIn('(set #expert (= @user-level 2))',text)
        self.assertIn('(if (and #expert (= #action 0))',text)
        self.assertIn('Choose the AmiRocket configuration drawer',text)
        self.assertIn('(set #datadest "SYS:AmiRocket")',text)
        self.assertIn('(if (and #expert (= #action 0)) (set #datadest',text)
        self.assertIn('(source "Plugins/timestamp.arp") (dest (tackon #datadest "Plugins"))',text)
        self.assertIn(' DATA-CHECK ',text)
        self.assertIn('.def DATA "SYS:AmiRocket"',shell)
        self.assertIn('(source "AmiRocketPrefs") (dest #prefsdest)',text)
        for name in ('Install','Install.info','scripts/Install-AmiRocket'):
            self.assertNotIn('(copyfiles (source "'+name+'")',text)
            self.assertNotIn('Copy "'+name+'" TO',shell)
        self.assertIn('(delete (tackon #prefsdest "AmiRocketPrefs"))',text)
        self.assertIn('CLEAN-LEGACY',text)
        self.assertIn('(set #installed (<> #dest ""))',text)
        self.assertIn('(if #installed ((user 1)',text)
        self.assertLess(text.index('(if #installed'),text.index('Choose the AmiRocket application drawer'))
        self.assertLess(text.index('(if #installed'),text.index('Choose the AmiRocketPrefs drawer'))
        self.assertIn('AmiRocket-ConfigFile',text)
        self.assertIn('Choose the default attachment download drawer',text)
        self.assertIn('Choose the thumbnail and avatar cache drawer',text)
        self.assertIn('(if (= #novice 0) (set #cachedest',text)
        self.assertIn('DIRECTORIES',text)
        self.assertIn('SYS:Downloads',text)
        self.assertIn('Sounds/Notification.8svx',text)
        sound=(ROOT/'assets/sounds/Notification.8svx').read_bytes()
        self.assertEqual(sound[:4],b'FORM');self.assertEqual(sound[8:12],b'8SVX')
        self.assertLess(len(sound),8192)
        self.assertIn('(complete 100)',text)
        self.assertIn('(exit (quiet))',text)
        self.assertLess(text.index('(message "AmiRocket installed successfully.'),text.index('(prompt "Installation complete. Finish or start AmiRocket?'))
        self.assertIn('(choices "Finish and start AmiRocket" "Finish")',text)
        self.assertNotIn('Start AmiRocket when installation finishes?',text)
        self.assertNotIn(' FINISH ',text)



    def test_discovery_probes_suppress_and_restore_requesters(self):
        helper=(ROOT/'tools/create_icons.c').read_text()
        self.assertIn('process->pr_WindowPtr=(APTR)-1;',helper)
        self.assertIn('process->pr_WindowPtr=saved;',helper)
        self.assertIn('dir=quiet_lock(path);',helper)
        native=(ROOT/'tools/installer_qa.c').read_text()
        self.assertIn('FindDosEntry(list,"AmiRocket",flags)==NULL',native)
        self.assertIn('MissingAmiRocketDisk:OldInstall',native)

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
