# AmiRocket

A native Rocket.Chat client for AmigaOS 3.2 that connects to ordinary self-hosted Rocket.Chat servers over HTTPS. It uses `bsdsocket.library` (Roadshow, AmiTCP or Miami) for networking, AmiSSL v5 for TLS and a ReAction interface.

Status: **0.1, first milestone**. You can log in, browse your rooms and direct messages, read history, send messages and receive new messages, edits and deletions by polling. Font selection, a multiline composer, room filtering, attachment downloads and older history are included. Threads, reaction editing, file upload and the realtime WebSocket API come later; see [Roadmap](#roadmap).

## Requirements

| | |
| --- | --- |
| OS | AmigaOS 3.2 (texteditor.gadget V47 and ReAction classes V44+) |
| CPU | 68020 or better. AmiSSL 5 requires it. A 68040/060 or an emulated/accelerated machine makes the TLS handshake fast |
| Memory | Depends on font/history; allow an additional 8 MiB for large attachment downloads |
| TCP/IP | Any `bsdsocket.library` stack: Roadshow, AmiTCP or Miami |
| TLS | [AmiSSL](https://github.com/jens-maus/amissl) 5.1 or newer for `https://` servers |
| Server | Rocket.Chat 6.x–8.x. Uses only long-standing REST endpoints |

## Using it

Start `AmiRocket` from Workbench, or run `Stack 65536` before launching it from the Shell. The Workbench icon already requests that stack size. In the login window, enter the server (`chat.example.com`, `https://chat.example.com:3000` or a sub-path such as `https://example.com/chat`), your user name and your password. If the account uses two-factor authentication, AmiRocket asks for the code. Authenticator app (TOTP) and e-mail codes both work.

With **Remember login** ticked, AmiRocket saves the Rocket.Chat user id and auth token in `ENVARC:AmiRocket.prefs` and logs in without a password next time. The password is never stored. **Project → Log out and forget login** revokes the token on the server and deletes it locally.

The layout follows the Rocket.Chat desktop client. On the left, rooms are grouped into collapsible **Favorites**, **Channels** and **Direct messages** sections (`#` public channel, `*` private group, `@` direct message, unread count in brackets). On the right, a header shows the open room. Messages are grouped under a "name time" line per sender, with a separator for each day. New messages in the open room arrive within the poll interval, 5 seconds by default (`interval=` in the prefs file). Messages that mention you are highlighted.

The room list, room header (`# general * topic`) and message view are drawn by AmiRocket itself, in the style of the desktop client. Each sender's messages are grouped under their avatar (with an initial as fallback), a **bold** name and a dim time. Reactions show as pills (`<3 1`) and days are separated by Today/Yesterday lines. Scroll with the scrollers or the mouse wheel.

Emoji are drawn as small pixel-art images, an original set of 51 at five sizes matched to the font. That covers real Unicode emoji, `:shortcodes:` and reactions; others fall back to emoticons such as `:)` or `(y)`. Typing `:` and two letters (`:smi`) suggests emoji by shortcode.

The **Find rooms / people** field above the sidebar filters room labels as you type. Clear it to show all rooms. Group DMs show participant names (omitting your own name) instead of ambiguous `+1` labels.

The composer shows at least three lines and wraps text. **Enter** sends; **Shift+Enter** adds a new line. Click **Send** or press **Amiga+S** to send; the draft is retained if sending fails. Messages are limited to 4000 characters. The label above it identifies the destination room. Draft text survives a theme change.

Typing `@` in the composer opens a **People** list with recent senders first, then the room's members and `@here`/`@all`, filtered as you type. Use **Up/Down** to select, **Return/Tab** to insert, and **Esc** to dismiss. Clicking also inserts a suggestion; **Amiga+J/K** and **Amiga+I** remain available as shortcuts. Emoji shortcode suggestions use the same controls.

Choose **Preferences → Appearance** for a font and size; Helvetica 13 is the default, with the screen font as a fallback when unavailable. An empty font name restores the native screen font. The selection is saved. Message spacing and sidebar rows follow the chosen font while controls retain the screen font. Group DMs have participant-count badges including your own account, and long labels use a middle ellipsis to retain the distinguishing final participants.

Scroll to the top to fetch the next 50 older messages, or choose **Room → Load older messages** (Amiga+O). The visible message stays anchored while the page is prepended. History loading stops at the start of the room or at 1000 loaded messages to bound memory use. Reopen the room to reset to recent history. Old history requests never advance the live-update cursor.

Attachments with download links appear as underlined **[File]** or **[Image]** rows. Click to choose a save location. Downloads run in the network worker, support binary data up to 8 MiB and use temporary files before replacing a destination. Only links on the configured server are downloaded; external links and redirects are reported rather than followed with your credentials. HTML login/error pages are rejected. At most 32 linked attachments are shown per message. **Preferences → Helpers → Open saved images in viewer** opens saved images in `SYS:Utilities/MultiView`, using installed datatypes. Image attachments stay collapsed; click their label to fetch and expand an inline preview, then click again to collapse. Use **Chat → Save selected attachment…** to save an image without expanding it. Inline previews use installed picture datatypes; the cache holds eight images and at most 8 MiB of encoded data. **Chat → Clear image previews** releases that cache.

**Preferences → Appearance → Theme** switches the colours: Workbench (default, the system colours), Catppuccin Latte, Frappé, Macchiato and Mocha, Nord, Dracula, Gruvbox Dark, Solarized Light and Kickstart 1.3. By default AmiRocket stays in a window on Workbench and reserves the theme's colours as shared pens. That is exact on 256-colour or RTG Workbenches and the nearest available colours on 8- or 16-colour ones. With **Preferences → Appearance → Theme on its own screen**, AmiRocket opens on its own public screen, `AMIROCKET`, with the same resolution and font as Workbench. The theme's palette and pens are then used everywhere, including frames, title bars and menus; switch screens with Amiga+M. The choices are saved as `theme=` and `ownscreen=` in the prefs file.

Single-person DMs show an avatar from the workspace when available, with a coloured initial as fallback. Visible avatars download one at a time in the network process and are cached for the session (up to 64 entries, 2 MiB encoded data total, 256 KiB per image). Picture datatypes decode and remap them to the current screen. A temporary file in `T:` is used if the installed datatype cannot open memory sources; it is deleted on release. Large or unsupported avatars fall back to initials. Avatars are fetched only from the configured workspace, without following redirects.

Presence indicators show online, away and busy states supplied by `users.presence`, refreshed approximately every 30 seconds (or the configured poll interval if longer). A hollow indicator means offline or unavailable/hidden presence; the client does not infer that a user is online. Servers that disable this API still support normal chat. Group DMs use a group marker rather than one person's status.

The composer heading keeps the destination on the left and a **Enter sends | Shift+Enter new line** hint on the right. Long destination labels are shortened without running into the hint.

Message markup is shown as plain text: `*bold*`, `_italic_` and code fences lose their markers, links become "text (url)", and common `:emoji:` codes become emoticons. The message view uses its own renderer; sender names are bold and metadata uses a secondary colour.

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
make release            # verified runtime, SDK and source archives in outputs/0.1/
```

Set `AMIGA_PREFIX=` if the toolchain lives somewhere else, and `BUILDS_DIR=` to copy the executable elsewhere. On the everyday emulator desktop, where `~/Development/builds` is `Builds:`, start it as `Builds:AmiRocket/AmiRocket`.

The Workbench icon (`assets/icon/AmiRocket.info`) is generated by `make icon` from `tools/icon/`: a rocket climbing out of a chat bubble on a cone of copper-bar raster stripes. It holds a 4-colour classic image for any Workbench and a 64-colour OS 3.5+ ColorIcon, which AmigaOS 3.2 shows by default. `make` copies it next to the executable.

### Host tools and tests

The protocol core (`src/core`) is portable C99 and also builds for macOS and Linux against OpenSSL. On the Mac, `brew install openssl@3` is enough, because the Makefile finds it through `brew --prefix`.

```sh
make host     # build/host/rcli and build/host/test_core
make test     # unit tests + integration tests against the mock server
make build/amiga/AmiRocket-GUIQA  # isolated GUI checks, no account needed
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
make qa-gui        # composer, filters, fonts, attachment hit areas and history anchors
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
  prefs.c        ENVARC:AmiRocket.prefs; preserved plugin keys
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
- Threads view, reactions, message editing/deleting
- File upload
- Inline Markdown styling (bold/italic via soft styles)
- Notifications (screen title, ScreenNotify/Ringhio), AppIcon/iconify, Locale catalogs
- 68000 build without TLS (plain HTTP inside a LAN)

Use **Edit → Copy/Cut/Paste** (**Amiga+C/X/V**) in the composer. Click a chat message to select it (a coloured bar marks it), then **Amiga+C** or **Edit → Copy selected message** copies its full plain text to the Amiga clipboard. Click the composer to return Copy to the draft.

Fenced code blocks (triple backticks, optionally followed by a language such as `yaml`) use a shaded fixed-width layout. Indentation, blank lines and literal Markdown are preserved; long code lines wrap to fit. HTTP and HTTPS links are underlined and clickable, including wrapped URLs. Link opening uses **C:OpenURL** with your configured Amiga browser. Chat avatars reuse the authenticated, size-limited avatar cache used by the sidebar.

**New conversations.** Use **New conversation…** or the Chat menu to open a direct/group message or create a public/private channel. Enter one or more comma-separated usernames; channel membership is optional. The server's permission and validation errors are displayed. Teams and discussions are not implemented yet.

**Composer toolbar.** The buttons insert emoji shortcodes, bold, italic, strike-through, inline code, fenced code blocks and links. Formatting wraps the selected text; Send remains available alongside Enter. Microphone/video controls are not shown because those features are not implemented.

**Separate preferences app.** Launch **AmiRocketPrefs** directly, or use the client's Preferences button. It needs no server connection. General controls polling, emoji graphics, avatars and inline images; Appearance controls themes and fonts; Plugins configures loadable modules; Helpers configures native browser/image-viewer integrations; Diagnostics and About provide application information. Use broadcasts temporary settings to running clients without a disk write. Save applies now and writes the single `ENVARC:AmiRocket.prefs` file. Cancel leaves settings unchanged. Updates reach running clients through native message ports without requiring a restart or focus change. Keep AmiRocketPrefs beside AmiRocket.

Native plugins, ARexx control/notifications and the example plugins are documented in [the SDK guide](sdk/README.md). Timestamp and Quick Reply are installed in `Plugins/` and can be enabled in Preferences. Their settings, and preserved future plugin options, use the same configuration file as the client. The client menu now contains only a Preferences launcher; settings controls live in AmiRocketPrefs. Temporary settings are not made permanent by login/window-state autosaves.

The Plugins page lists discovered modules with version and enabled/active status,
plus author, description and path. Use **Plugin settings...** for staged options.
Optional `.arp.meta` sidecars provide display metadata without executing disabled
plugins. Install modules and their sidecars together in the Plugins drawer.

Attachment downloads use session cookies as well as REST authentication and
follow bounded redirects. External HTTPS storage receives no workspace credentials.
Download failures report the HTTP status; image preview failures can be retried.


Multiple Rocket.Chat servers
---------------------------
AmiRocket can keep up to eight servers connected. The sidebar is a collapsible
server tree: **Rooms → Favorites / Non favorites**, and **Direct messages**.
Select a server or one of its rooms to work in that server. The title includes
the server; room IDs, remembered logins, histories, cursors and drafts are scoped
to that server. A single background worker owns independent sessions and polls
all connected servers. Background servers retain their unread messages.

In **AmiRocketPrefs → Servers**, use Add, Edit or Remove, and set **Auto-connect
on startup** separately for each entry. Use applies the list for this session;
Save also writes it to the shared configuration file. Sign in through the
client's Connect button once with Remember login enabled for unattended startup.
Passwords are never saved. An existing single-server configuration migrates
into the first server entry automatically. An empty server list stays empty.

Changing a server's address, account or certificate policy disconnects its old
session and clears its cached chat data. Removing another server does not
renumber existing slots. Preferences edits are staged until Use or Save.


Server events and context actions
---------------------------------
Click a server name to view its connection event history, including failures.
Offline servers show only their root entry, with a dimmed name; failures use
an error colour and a failed marker. Connected servers expose their room tree.
Right-click a server for Connect, Disconnect or Edit Server. Right-clicking
elsewhere retains the normal Amiga menu strip.

Edit Server starts AmiRocketPrefs directly in that server's editor, without the
full Preferences window. Use changes only that server for this session. Save
applies it and updates that entry in ENVARC:AmiRocket.prefs, preserving other
servers, global settings and plugin options. The client has a Create New button
at the top of the sidebar, a two-line composer, and no composer hint or bottom
status strip. Enter sends and Shift+Enter inserts a newline as before.


Reliability and workspace
-------------------------
Read receipts are issued by the UI after a conversation is visible in the
active window; server-event views, background servers and inactive windows do
not mark cached conversations read. Network failures retry after 5, 10, 20,
40, 80, 160 and then 300 seconds. Authentication failures stop retries.
Right-click a server and choose Retry Now to try immediately. F10 also opens
the selected server's actions while its event view is displayed.

Attachment and image-preview transfers have a progress window with Cancel
and Retry. Cancellation waits for the current network read (up to 30 seconds)
and never replaces the destination with a partial file. Files are limited to
8 MiB; previews to 2 MiB encoded and one million pixels, with an 8 MiB encoded
cache. Redirect errors and authentication failures are shown in the dialog.

The sidebar divider can be dragged. On clean exit, the selected server and
conversation, server/group expansion states, sidebar proportion and window
geometry are saved in the same AmiRocket.prefs file as all other settings.
Preferences titles show Unsaved changes, Session settings (not saved), or
Saved settings. Use changes the running session; Save applies and persists.


Iconification
-------------
Use the title-bar iconify gadget or Project / Iconify (Amiga+H) to hide the
client as an AmiRocket Workbench AppIcon. Double-click the icon to restore
its window. Connections, notifications, the selected chat and unsent draft
remain active; hidden conversations do not send read receipts. Own-screen
mode moves its screen behind Workbench while iconified.
ARexx ICONIFY hides to the AppIcon; SHOW restores and activates the window.
HIDE retains its existing send-to-back behaviour. STATUS includes iconified.

Dock integration: the ARexx v2 interface provides aggregate/per-server snapshots, CONNECT/DISCONNECT, room unread events and restart/drop recovery metadata. See the SDK guide and AmiRocketMonitor example; legacy subscribers remain supported.

The separate-process dock smoke checks cover aggregate badges, periodic recovery,
ARexx visibility actions and client/monitor restarts. See
[the validation procedure](docs/DOCK-VALIDATION.md) for coverage and limits.

Network recovery keeps drafts and never automatically resends a message after an
ambiguous response loss. “Delivery unconfirmed” means the server may have accepted
it: check the conversation before sending again. Safe reads can reconnect once
after a stale connection; POST requests are never automatically retried.
Polling and remembered-token login use exponential reconnect delays from 5 seconds
up to 5 minutes, including temporary HTTP 5xx/429 failures. Expired authentication
stops retries and requires sign-in again. Failed background sends appear in the
server event history and publish a server-qualified SENDFAILED ARexx event.

**Unread navigation.** Unread rooms are bold. Mention badges show `@mentions / unread`
(or just the available count). **Room → Next unread room** (**Amiga+U**) searches
connected servers, including rooms hidden by filters or collapsed sections, and
reveals the destination. **Room → Jump to new messages** (**Amiga+L**) returns to
the highlighted **New messages** divider. Opening an unread room positions the
history at that divider. The boundary uses the server's last-seen timestamp when
available; older servers fall back to the reported unread count within loaded
history. The marker remains stable while you read or load older messages.

**Attachment information.** File and image labels show the server-reported size,
or “size unknown” when it is missing. Known oversized files are rejected before
fetching; actual download limits are still enforced regardless of metadata.
Unsupported image previews offer **Save attachment…** rather than downloading the
same unsupported image repeatedly. Cancelling keeps the progress display on
“Cancelling…” until the worker finishes its current read; late progress packets
cannot overwrite that state. Closing a failed preview also releases its loading
state so it can be tried again.


## Release packages and installation

Release packages follow the staged Beam68 workflow. `make release` runs the
portable/release tests, forces a complete native build and creates verified LHA,
SDK and source archives with per-file SHA-256 checksums under `outputs/<VERSION>`.
`make release-check` detects changed inputs or artifacts; `make release-prepare`
freezes a reviewable manifest and exact publication copies. See
[docs/RELEASING.md](docs/RELEASING.md) for publication, staging and installer tests.

Extract the runtime LHA and double-click **Install** to install/update/uninstall.
Preferences stays beside the client. Updates preserve the single configuration,
custom icons and unrelated plugins. Startup is optional. Uninstall keeps settings
and custom files. The runtime archive includes a matching **Source** drawer; the
versioned source and plugin SDK ZIPs are also available as separate artifacts.

AmiRocket is **GPL-3.0-or-later**; see LICENSE and COPYRIGHT. Beam68-derived release
tools retain their MIT attribution in licenses/Beam68-release-tools.txt.
