# Native screenshots

Website images are captured from AmiRocket's actual ReAction interface running
inside AmigaOS 3.2 on the Amiga Workbench emulator (68060 profile). They are not
HTML mockups. Workspace names and conversations are deterministic sample data;
no account, credentials or network connection is used.

The fixture in `tools/screenshots.c` includes the application's GUI renderer and
uses its real room tree, message layout, themes and Preferences panels. It does
not demonstrate a live connection to a Rocket.Chat server. The second workspace
is intentionally offline to show that sidebar state.

To reproduce, wait for other emulator sessions to close, then run from the root:

```sh
make build/amiga/AmiRocket-Screenshots
mkdir -p work/screenshots
"/Applications/Amiga Workbench.app/Contents/MacOS/AmigaWorkbench" \
  --workbench latest --mount "AmiRocket:$PWD/build/amiga" \
  --mount "Shots:$PWD/work/screenshots" \
  --test qa/screenshots.json --output /tmp/amirocket-screenshots-unique
```

The runner copies guest mounts into its disposable profile. Captures are PPMs in
`profile/Installations/workbench-3.2.1/CPUs/68060/TestShared/Shots` under the output
directory. Convert them losslessly to PNG (for example with `sips -s format png`)
and place them in `public-web/assets/screenshots`. No retouching, recolouring or
compositing is applied. Captures include the native window border and title bar.
Run `python3 public-web/prepare.py` to update the served site.
