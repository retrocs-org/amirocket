# Releasing AmiRocket

The release workflow is adapted from Beam68. Build once, inspect the manifest,
and publish the same verified bytes. Local commands do not upload or create tags.

Git tags use the plain version (`0.1`). Release titles include the application
name (`AmiRocket 0.1`), matching the NovaDock release presentation.
The tag annotation is `AmiRocket 0.1`. GitHub and GitLab receive identical
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
no external compressor is required. ZIP timestamps/order are fixed. An optional
configured `LHA` program uses compressed LHA with extraction verification.

A native build alone does not certify real hardware or a Rocket.Chat server.
Run `make qa-gui` for native client checks. For the actual Installer utility:

```
python3 -B scripts/build-installer-qa.py
"/Applications/Amiga Workbench.app/Contents/MacOS/AmigaWorkbench" \
  --workbench latest \
  --mount "AmiRocket:$PWD/build/installer-qa/AmiRocket" \
  --mount "QATools:$PWD/build/installer-qa/QATools" \
  --test qa/installer.json --output qa-results/installer
```

Use a new results directory each run and wait for other emulator tests to finish.
The fixture installs to RAM inside a disposable profile, upgrades with a held
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
python3 -B scripts/release.py prepare --tag 0.1
python3 -B scripts/release.py publish --tag 0.1 --to github,gitlab --confirm-publish
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

Install/update is destination-selected and preserves the single
`ENVARC:AmiRocket.prefs` file, customized icons and third-party plugins. No personal
configuration is shipped. Installation creates application/drawer icons only;
there is no autostart prompt or Workbench startup registration. The Preferences executable stays beside the client so PROGDIR-based
plugins/helpers keep working. Uninstall removes only the declared runtime files
and any owned startup entry from older installers. Existing copies offer Update
or Uninstall. Uninstall defaults to Keep settings; Remove settings explicitly
deletes the shared ENVARC:AmiRocket.prefs and old ENV fallback for all installations.
Custom plugins and unrelated files remain in either mode.

## Licence

AmiRocket is GPL-3.0-or-later. The release-tool/installer adaptations retain the
Beam68 MIT notice in `licenses/Beam68-release-tools.txt`. Do not remove either
notice when packaging. The runtime Source drawer and separate source ZIP are
created from the exact inputs whose fingerprints appear in the build record.
