# Releasing AmiRocket

The release workflow is adapted from Beam68. Build once, inspect the manifest,
and publish the same verified bytes. Local commands do not upload or create tags.

Git tags use the plain version (`0.2`). Release titles include the application
name (`AmiRocket 0.2`), matching the NovaDock release presentation.
The tag annotation is `AmiRocket 0.2`. GitHub and GitLab receive identical
change-only notes from the current CHANGELOG section under exactly these headings,
in order: `### Features`, `### Enhancements`, `### Bugs`. No other sections are
allowed; no overview, installation,
licence or download instructions are appended to the release body.

## Local build

Set `VERSION`, add the matching `CHANGELOG.md` entry and put its complete notes in
`docs/RELEASE-NOTES.md` (keep the same heading levels in both files).
Run `python3 -B scripts/make-installer.py` when the runtime inventory changes.
Workbench installer/guide icons can be regenerated with
`python3 -B scripts/generate_info_icons.py`.

```
python3 -B scripts/release.py plan
make release
make release-check
make release-prepare
```

The release directory is `outputs/<VERSION>/`:

- `AmiRocket-<VERSION>.lha`: one Workbench drawer, installer, client, Preferences,
  monitor, bundled plugins, scripts, manual, notices and a matching Source drawer.
- `AmiRocket-<VERSION>-plugin-sdk.zip`: plugin/native ARexx headers and examples.
- `AmiRocket-<VERSION>-source.zip`: complete corresponding source and build tools.
- One `.sha256` file per archive.

`release/build.json` records source/artifact hashes and build provenance.
`release/manifest.json` binds notes, destinations and hashes to reviewed copies
under `release/publish/outputs/`. `BUILD-PROVENANCE.json` records native HUNK
binaries, compiler identity, flags, version and their code-input fingerprint.
Builds force every native target to avoid stale objects after flag/version changes.
They run portable tests and offline release tests first. An LHA writer is included;
no external compressor is required. LHA and ZIP members use the current local packaging date/time, with the
two-second precision supported by their DOS timestamp format. All three archives
share one packaging clock value, recorded as UTC `packaged_at` in build.json.
Entry order remains fixed; independently rebuilt archives can have different
hashes because their creation times differ. An optional
configured `LHA` program uses compressed LHA with extraction verification.

A native build alone does not certify real hardware or a Rocket.Chat server.
Run `make qa-gui` for native client checks. For the actual Installer utility:

```
python3 -B scripts/build-installer-qa.py
"/Applications/Amiga Workbench.app/Contents/MacOS/AmigaWorkbench" \
  --workbench latest \
  --mount "ReleaseFiles:$PWD/build/installer-qa/AmiRocket" \
  --mount "QATools:$PWD/build/installer-qa/QATools" \
  --test qa/installer.json --output qa-results/installer
```

Use a new results directory each run and wait for other emulator tests to finish.
The fixture deliberately has no AmiRocket: assign and includes a stale saved
volume path, so missing-location probes must not raise disk requesters.
It installs to RAM inside a disposable profile, upgrades with a held
Workbench drawer lock, then uninstalls. It verifies settings, custom icons and
unrelated plugins survive. It does not install into the user's Workbench.

`make release-stage` verifies and backs up files before copying the runtime into
`BUILDS_DIR` and the LHA/checksum into its parent. Drawer inodes and unrelated
files/configuration survive. Use this target to stage the installer package;
ordinary `make amiga` remains the quick developer executable-copy workflow.
`make package-project` additionally creates a complete project/runtime ZIP in dist.

## Configuration and publication

Copy `examples/release-config.json` to `work/release-config.json` or use
`--config /path/to/external.json`. Empty default destinations allow local packages.
Set canonical HTTPS repository URLs and matching Git destinations to publish.
Allowed build overrides include AMIGA_CC, AMIGA_CPU, HOST_CC and LHA. A custom
`--output outputs/release-<VERSION>` keeps multiple independent runs and receipts.

Credentials come from GITHUB_TOKEN/GITLAB_TOKEN, or the inherited optional
1Password selectors. They are resolved in memory, excluded from build subprocesses
and not serialized into archives/manifests. Do not put token values into config.
For example a credential selector is `{"provider":"env","variable":"GITHUB_TOKEN"}`.

Publication requires a clean committed Git checkout with every release input
tracked. If destinations/notes/source/artifacts change, rebuild or prepare again
as appropriate. The command refuses conflicting existing release assets/tags.

```
python3 -B scripts/release.py prepare --tag 0.2
python3 -B scripts/release.py publish --tag 0.2 --to github,gitlab --confirm-publish
```

GitHub uploads are verified while draft, then published; GitLab package bytes are
verified before release links are advertised. Annotated tags must identify the
same commit on every selected destination. Per-destination receipts permit safe
recovery without replacing reviewed assets. The optional `source --dry-run` command
shows source synchronization without creating release tags; actual pushes require
`--confirm-push`. No remote is created or pushed by a local build.

For Aminet set `aminet: true` and a real uploader email before building/preparing.
`publish --to aminet --confirm-publish` submits the same runtime plus generated
Aminet ReadMe. A receipt records submission, not moderation/publication. A partial
submission is not retried blindly. The runtime includes corresponding source even
when distributed through Aminet alone. `scripts/make-aminet-release.py --uploader`
also stages manual submission files from a verified build.

## Installer behaviour

Novice mode detects recorded locations, standard drawers or an existing
AmiRocket assign (excluding the extracted distribution). Fresh installs use
SYS:Utilities for the client and SYS:Prefs for AmiRocketPrefs. Shared resources
use SYS:AmiRocket by default; Expert mode can select a separate shared-data drawer.
Intermediate and Expert modes select separate client and Preferences drawers;
Expert additionally selects a drawer for the shared AmiRocket.prefs file.
The default configuration location remains ENVARC:. A small persistent OS
AmiRocket-ConfigFile locator holds only the filename; all account, application
and plugin settings remain in the single selected configuration file.

Updates preserve customized icons and unrelated files. The archive's Install
launcher and scripts/Install-AmiRocket are not copied into the installed app;
use Install from a newly extracted release to Update or Uninstall. Older
installer copies and co-located Preferences executables are retired on update.
The client finds the separate Preferences app through its recorded drawer,
and Preferences reads plugin metadata beside the recorded client installation.
All three user levels offer Update/Uninstall before destination selection when
an existing client is detected; otherwise they install a fresh copy. Completion
shows the three installed paths and offers a one-time launch (off by default).
There is no autostart registration. Uninstall removes both installed applications
and offers Keep settings or Remove settings for the selected configuration.
Remove settings also clears its OS location pointer and legacy ENV fallback.
Custom plugins and unrelated files remain in either mode.

## Licence

AmiRocket is GPL-3.0-or-later. The release-tool/installer adaptations retain the
Beam68 MIT notice in `licenses/Beam68-release-tools.txt`. Do not remove either
notice when packaging. The runtime Source drawer and separate source ZIP are
created from the exact inputs whose fingerprints appear in the build record.

The focused native shared-resource check is `qa/shared-data.json`. Build it with
`make build/amiga/SharedDataQA`, mount that executable's drawer as `QATools:`
and a separate runtime drawer containing Plugins and Sounds as `SharedData:`.
It checks path resolution and loads the Timestamp plugin across the drawer split.

Native GUI regression tests now detach ListBrowser labels before disposing
search/message windows, exercise the actual close helpers and validate staged
low-resource controls. `AmiRocket-GUIQA OBSERVE` enables a separate observer that
records visible window titles and a `qa-stall.ppm` snapshot after a quiet phase;
it is diagnostic only and is disabled for normal test runs. The standard GUI
suite must finish without relying on that observer or a larger timeout.

Composer delivery recovery is covered by mock-server integration checks for a
lost POST acknowledgment, confirmation from a new client process, changed-text
rejection and rejection of duplicate IDs. Native checks cover private-record
persistence, account separation, transfer ETA and editor navigation shortcuts.
