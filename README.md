# AmiRocket

A native Rocket.Chat client for AmigaOS 3.2 that connects to ordinary self-hosted Rocket.Chat servers over HTTPS. It uses `bsdsocket.library` (Roadshow, AmiTCP or Miami) for networking, AmiSSL v5 for TLS and a ReAction interface.

Status: **0.1, first milestone**. You can log in, browse your rooms and direct messages, read history, send messages and receive new messages, edits and deletions by polling. Threads, reactions, file upload and the realtime WebSocket API come later; see [Roadmap](#roadmap).

## Requirements

| | |
| --- | --- |
| OS | AmigaOS 3.2 (ReAction classes V44+, so 3.1.4 should work too) |
| CPU | 68020 or better. AmiSSL 5 requires it. A 68040/060 or an emulated/accelerated machine makes the TLS handshake fast |
| Memory | About 1 MB free while running |
| TCP/IP | Any `bsdsocket.library` stack: Roadshow, AmiTCP or Miami |
| TLS | [AmiSSL](https://github.com/jens-maus/amissl) 5.1 or newer for `https://` servers |
| Server | Rocket.Chat 6.x–8.x. Uses only long-standing REST endpoints |

## Using it

Start `AmiRocket` from Workbench or the Shell. In the login window, enter the server (`chat.example.com`, `https://chat.example.com:3000` or a sub-path such as `https://example.com/chat`), your user name and your password. If the account uses two-factor authentication, AmiRocket asks for the code. Authenticator app (TOTP) and e-mail codes both work.

With **Remember login** ticked, AmiRocket saves the Rocket.Chat user id and auth token in `ENVARC:AmiRocket.prefs` and logs in without a password next time. The password is never stored. **Project → Log out and forget login** revokes the token on the server and deletes it locally.

The layout follows the Rocket.Chat desktop client. On the left, rooms are grouped into collapsible **Favorites**, **Channels** and **Direct messages** sections (`#` public channel, `*` private group, `@` direct message, unread count in brackets). On the right, a header shows the open room. Messages are grouped under a "name time" line per sender, with a separator for each day. New messages in the open room arrive within the poll interval, 5 seconds by default (`interval=` in the prefs file). Messages that mention you are highlighted.

The room list, room header (`# general * topic`) and message view are drawn by AmiRocket itself, in the style of the desktop client. Each sender's messages are grouped under an avatar block with their initial, a **bold** name and a dim time. Reactions show as pills (`<3 1`) and days are separated by Today/Yesterday lines. Scroll with the scrollers or the mouse wheel.

Emoji are drawn as small pixel-art images, an original set of 50 at five sizes matched to the font. That covers real Unicode emoji, `:shortcodes:` and reactions; others fall back to emoticons such as `:)` or `(y)`. Typing `:` and two letters (`:smi`) suggests emoji by shortcode.

Typing `@` in the input line opens a **People** list with recent senders first, then the room's members and `@here`/`@all`, filtered as you type. Cursor up/down moves through the list, Return or Tab inserts the selected entry, Esc closes it, and clicking inserts any of them.

**Settings → Theme** switches the colours: Workbench (default, the system colours), Catppuccin Latte, Frappé, Macchiato and Mocha, Nord, Dracula, Gruvbox Dark, Solarized Light and Kickstart 1.3. By default AmiRocket stays in a window on Workbench and reserves the theme's colours as shared pens. That is exact on 256-colour or RTG Workbenches and the nearest available colours on 8- or 16-colour ones. With **Settings → Theme on its own screen**, AmiRocket opens on its own public screen, `AMIROCKET`, with the same resolution and font as Workbench. The theme's palette and pens are then used everywhere, including frames, title bars and menus; switch screens with Amiga+M. The choices are saved as `theme=` and `ownscreen=` in the prefs file.

Message markup is shown as plain text: `*bold*`, `_italic_` and code fences lose their markers, links become "text (url)", and common `:emoji:` codes become emoticons. AmigaOS 3.2's listbrowser cannot mix text styles in one line.

Text is converted between UTF-8 and the Amiga's ISO-8859-1. Typographic quotes and dashes become ASCII, and common emoji become emoticons (`🙂` → `:)`, `👍` → `(y)`). Anything else shows as `?`.

### Shell options

```
AmiRocket SELFTEST
AmiRocket PROBE=https://chat.example.com [USER=name PASSWORD=secret [CODE=123456]] [INSECURE]
```

`SELFTEST` runs the core unit tests on the Amiga itself (68k, big-endian, 32-bit `long`) and reports whether `bsdsocket.library` and AmiSSL are usable. `PROBE` checks DNS, TCP, TLS and the server from the Shell without opening a window. With credentials, it also logs in, lists rooms and prints the last messages of the first room. `INSECURE` skips certificate verification.

## Building

Cross-compile on the Mac with the AmigaPorts toolchain in `~/Development/tools/amiga/m68k-amigaos-gcc-16.2`:

```sh
make                    # build/amiga/AmiRocket (68020), copied to ~/Development/builds/AmiRocket
make AMIGA_CPU=-m68040  # other CPU targets
make release            # build/release/AmiRocket-0.1.lha
```

Set `AMIGA_PREFIX=` if the toolchain lives somewhere else, and `BUILDS_DIR=` to copy the executable elsewhere. On the everyday emulator desktop, where `~/Development/builds` is `Builds:`, start it as `Builds:AmiRocket/AmiRocket`.

The Workbench icon (`assets/icon/AmiRocket.info`) is generated by `make icon` from `tools/icon/`: a rocket climbing out of a chat bubble on a cone of copper-bar raster stripes. It holds a 4-colour classic image for any Workbench and a 64-colour OS 3.5+ ColorIcon, which AmigaOS 3.2 shows by default. `make` copies it next to the executable.

### Host tools and tests

The protocol core (`src/core`) is portable C99 and also builds for macOS and Linux against OpenSSL. On the Mac, `brew install openssl@3` is enough, because the Makefile finds it through `brew --prefix`.

```sh
make host     # build/host/rcli and build/host/test_core
make test     # unit tests + integration tests against the mock server
```

`rcli` is a command-line Rocket.Chat client built from exactly the code the Amiga client uses. It is handy for checking a real server:

```sh
build/host/rcli -s https://chat.example.com -u tommy -p secret rooms
build/host/rcli -s https://chat.example.com -u tommy -p secret history general 20
build/host/rcli -s https://chat.example.com -i USERID -t TOKEN watch general 5
build/host/rcli -L ...        # print through the Amiga ISO-8859-1 conversion
```

`tests/mock_rocketchat.py` is a mock Rocket.Chat server with the response shapes of the real one, plus hooks to inject, edit and delete messages and remove rooms. The integration tests run it with keep-alive, with connections silently dropped by the server, chunked responses, `\u`-escaped JSON, a sub-path, TLS with and without verification, 2FA and token login.

### Testing in the emulator

The tests use `/Applications/Amiga Workbench.app` from AmigaMac:

```sh
make qa-selftest   # core unit tests on the emulated 68k + bsdsocket/AmiSSL check
make qa-probe      # DNS, TCP and verified TLS against open.rocket.chat
```

Both mount only `~/Development/builds/AmiRocket`, as the volume `AmiRocket:`, because test mode copies every mounted folder. Results go to `qa-results/<test>-<timestamp>/`. The program's output is in `profile/.../AutoRun/launch.log`.

The `probe-public` test needs the emulator's Roadshow network (en0 bridge) and internet access. To try the GUI against the mock server, run `python3 tests/mock_rocketchat.py --host 0.0.0.0 --port 3000` on the Mac. Then log in from the emulator to `http://<mac-lan-ip>:3000` as `tommy` / `secret` (the `secure` / `secret` user needs 2FA code `123456`).

## Architecture

```
src/core/      portable C99, no OS calls
  http.c         HTTP/1.1, persistent connection, chunked, reconnect on stale keep-alive
  json.c         two-pass tokenizer (one allocation per document), builder
  rc_api.c       Rocket.Chat REST calls, error/2FA mapping, data model and merge
  session.c      login state, room list, polling strategy
  charset.c      UTF-8 <-> ISO-8859-1 with readable fallbacks
  mdtext.c       Rocket.Chat markup to plain text
  emoji.c        emoji table (code point, shortcode, fallback); generated by tools/emoji
  timeutil.c     ISO 8601 / HTTP dates without libc time functions
src/amiga/
  main.c         ReadArgs: GUI, SELFTEST, PROBE (quoted keywords accepted)
  gui.c          ReAction windows, menus, list rendering
  worker.c       network process; NetMsg commands, replies and poll events
  net_amiga.c    bsdsocket.library + AmiSSL transport
  mem_amiga.c    semaphore-protected exec memory pool
  prefs.c        ENV:/ENVARC:AmiRocket.prefs
  theme.c        colour themes (palette + DrawInfo pens for the AMIROCKET screen)
  view.c         custom-drawn sidebar, room header and message view
  emoji_data.c   pixel-art emoji bitmaps (generated by make emoji)
src/posix/       sockets + OpenSSL transport for rcli and tests
```

**Networking off the GUI task.** Every network call runs in a separate process (`AmiRocket network`), so the window never freezes during a multi-second TLS handshake on a 68020. The GUI and the worker exchange `NetMsg` messages. Commands come back as replies, and poll results arrive as events the GUI returns. `bsdsocket.library` and AmiSSL are opened by the worker, because their bases are per task. Quitting sends Ctrl-C to the worker to abort a blocking socket call, then waits for it to finish.

**One allocator for both tasks.** libnix `malloc()` is not safe to call from two tasks at once, so all core allocations go through `ar_malloc()`, an exec memory pool guarded by a semaphore. Deleting the pool on exit releases anything that leaked.

**Polling.** The client uses REST only. Each poll requests `subscriptions.get?updatedSince=` for the room list and `chat.syncMessages?lastUpdate=` for the open room, both over the same keep-alive connection. Cursors always come from server timestamps (`_updatedAt`, HTTP `Date`), never the Amiga clock. Local time is derived from the server's `Date` header compared with the Amiga clock, falling back to the Locale preferences.

**Server compatibility.** Endpoints: `/api/info`, `login`, `me`, `logout`, `subscriptions.get`, `subscriptions.read`, `channels|groups|im.history`, `chat.syncMessages`, `chat.sendMessage`. Rocket.Chat 9.0 changed REST status codes, so success is judged from the JSON `success`/`status` fields rather than the HTTP status alone.

## Roadmap

- Realtime updates over DDP/WebSocket (`stream-room-messages`, `stream-notify-user`) instead of polling
- Threads view, reactions, message editing/deleting, `@` completion
- Load older history when scrolling up
- File download through datatypes; upload
- Markdown rendering (bold/italic via soft styles), clickable links via OpenURL
- Notifications (screen title, ScreenNotify/Ringhio), AppIcon/iconify, Locale catalogs
- 68000 build without TLS (plain HTTP inside a LAN)
