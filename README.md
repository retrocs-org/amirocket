# AmiRocket

A native Rocket.Chat client for AmigaOS 3.2 that connects to ordinary self-hosted Rocket.Chat servers over HTTPS. It uses `bsdsocket.library` (Roadshow, AmiTCP or Miami) for networking, AmiSSL v5 for TLS and a ReAction interface.

Status: **0.2 in development**. The latest published release is **0.1**. You can log in, browse your rooms and direct messages, read history, send messages and receive new messages, edits and deletions through WebSocket-triggered synchronization, with polling fallback. Font selection, a multiline composer, room filtering, attachment downloads and older history are included. Message actions include quotes, paged thread replies, own-message edits/deletion and reaction add/remove.

**Installer locations.** Novice mode uses `SYS:Utilities` for the client
(the executable is `SYS:Utilities/AmiRocket`, without an application subdrawer)
with shared resources in `SYS:AmiRocket` (plugins, sounds and supporting files).
The manual is installed directly as `HELP:AmiRocket.guide`, and Preferences
as `SYS:Prefs/AmiRocketPrefs`.
Intermediate and Expert modes can select separate application and Preferences
drawers. Expert mode additionally selects the shared-data drawer and a drawer for the single
`AmiRocket.prefs` configuration file (default `ENVARC:`). All client, Preferences
and plugin settings remain in that file. A small OS environment locator,
`AmiRocket-ConfigFile`, stores only its path so both apps and Shell launches can
find it after reboot. Existing settings are migrated before the old file is
retired; an unrelated existing destination configuration is not overwritten.

In all three modes, detecting an existing client offers **Update or Uninstall**
before any destination selectors. If none is found, a fresh install proceeds.
Run **Install from the extracted release archive** to update or uninstall.
The launcher, its icon and the Shell installer are not copied into the installed
application. Uninstall removes both applications from their selected drawers
and lets you keep or remove the selected shared settings. Updates preserve
custom icons and unrelated files. A completion summary lists all selected paths. The final page offers Finish
or Finish and start AmiRocket, followed by a quiet installer exit. No startup entry is created.
The client locates the separate Preferences
app automatically; Preferences reads plugins from the recorded shared-data drawer. Updates preserve custom plugins and drafts when moving shared data; unpacked developer builds retain PROGDIR defaults.


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

With **Remember login** ticked, AmiRocket saves the Rocket.Chat user id and auth token in the selected `AmiRocket.prefs` file (by default `ENVARC:`) and logs in without a password next time. The password is never stored. **Project → Log out and forget login** revokes the token on the server and deletes it locally.

The layout follows the Rocket.Chat desktop client. On the left, rooms are grouped into collapsible **Favorites**, **Channels** and **Direct messages** sections (`#` public channel, `*` private group, `@` direct message, unread count in brackets). On the right, a header shows the open room. Messages are grouped under a "name time" line per sender, with a separator for each day. Realtime event subscriptions trigger coalesced updates. A slower catch-up poll covers missed events; if WebSockets are unavailable, active-room polling starts at 5 seconds (`interval=` in the prefs file). Network pacing or server cooldowns can delay updates. Messages that mention you are highlighted.

The room list, room header (`# general * topic`) and message view are drawn by AmiRocket itself, in the style of the desktop client. Each sender's messages are grouped under their avatar (with an initial as fallback), a **bold** name and a dim time. Reactions show as pills (`<3 1`) and days are separated by Today/Yesterday lines. Scroll with the scrollers or the mouse wheel.

Emoji are drawn as small pixel-art images, an original set of 51 at five sizes matched to the font. That covers real Unicode emoji, `:shortcodes:` and reactions; others fall back to emoticons such as `:)` or `(y)`. Typing `:` and two letters (`:smi`) suggests emoji by shortcode.

The **Find rooms / people** field above the sidebar filters room labels as you type. Clear it to show all rooms. Group DMs show participant names (omitting your own name) instead of ambiguous `+1` labels.

The composer shows at least three lines and wraps text. **Enter** sends; **Shift+Enter** adds a new line. Click **Send** or press **Amiga+S** to send; the draft is retained if sending fails. Messages are limited to 4000 characters. The label above it identifies the destination room. Draft text survives a theme change.

Typing `@` in the composer opens a **People** list with recent senders first, then the room's members and `@here`/`@all`, filtered as you type. Use **Up/Down** to select, **Return/Tab** to insert, and **Esc** to dismiss. Clicking also inserts a suggestion; **Amiga+J/K** and **Amiga+I** remain available as shortcuts. Emoji shortcode suggestions use the same controls.

Choose **Preferences → Appearance** for a font and size; Helvetica 13 is the default, with the screen font as a fallback when unavailable. An empty font name restores the native screen font. The selection is saved. Message spacing and sidebar rows follow the chosen font while controls retain the screen font. Group DMs have participant-count badges including your own account, and long labels use a middle ellipsis to retain the distinguishing final participants.

Scroll to the top to fetch the next 50 older messages, or choose **Room → Load older messages** (Amiga+O). The visible message stays anchored while the page is prepended. History loading stops at the start of the room or at the configured message limit (1000 by default, at most 200 in low-memory mode). Reopen the room to reset to recent history. Old history requests never advance the live-update cursor.

Attachments appear as compact cards with an image/file indicator, a shortened filename and file size. **Preview / Hide** loads or collapses an inline thumbnail; **Open** sends an image original to the configured external viewer, and **Save** chooses a download destination. Loading and failure states appear on the card, with **Retry** after a failure. Original downloads retain their progress/cancel dialog. Only same-server attachment links are downloaded with credentials; redirects and HTML error pages are rejected. The thumbnail cache holds up to eight images within the configured encoded-data budget; reopening a cached preview does not download or decode it again. **Room → Clear image previews** releases that cache.

**Preferences → Appearance → Theme** switches the colours: Workbench (default, the system colours), Catppuccin Latte, Frappé, Macchiato and Mocha, Nord, Dracula, Gruvbox Dark, Solarized Light and Kickstart 1.3. By default AmiRocket stays in a window on Workbench and reserves the theme's colours as shared pens. That is exact on 256-colour or RTG Workbenches and the nearest available colours on 8- or 16-colour ones. With **Preferences → Appearance → Theme on its own screen**, AmiRocket opens on its own public screen, `AMIROCKET`, with the same resolution and font as Workbench. The theme's palette and pens are then used everywhere, including frames, title bars and menus; switch screens with Amiga+M. The choices are saved as `theme=` and `ownscreen=` in the prefs file.

Single-person DMs show an avatar from the workspace when available, with a coloured initial as fallback. Visible avatars download one at a time in the network process and are cached for the session (up to 64 entries, 2 MiB encoded data total, 256 KiB per image). Picture datatypes decode and remap them to the current screen. A temporary file in `T:` is used if the installed datatype cannot open memory sources; it is deleted on release. Large or unsupported avatars fall back to initials. Avatars are fetched only from the configured workspace, without following redirects.

Presence indicators show online, away and busy states supplied by `users.presence`, refreshed approximately every 60 seconds while foreground (or the configured poll interval if longer). A hollow indicator means offline or unavailable/hidden presence; the client does not infer that a user is online. Servers that disable this API still support normal chat. Group DMs use a group marker rather than one person's status.

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
make release            # verified runtime, SDK and source archives in outputs/0.2/
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
  session.c      login state, room list, delta synchronization
  websocket.c    bounded RFC 6455 framing, masked writes and verified upgrade
  realtime.c     DDP resume, subscriptions, heartbeat and reconnect state
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

**Realtime and catch-up.** Each authenticated server opens a separate same-origin WebSocket, resumes its existing login token and subscribes to active-room messages, deletions and user room/subscription changes. DDP and WebSocket pings are answered. Events coalesce into REST delta syncs at most once every two seconds; sends remain explicit REST requests and are never replayed. Healthy streams use a catch-up interval of at least 60 seconds, with room-list reconciliation every 120 seconds. Disconnects, rejected subscriptions, invalid frames or heartbeat loss restore polling and exponential WebSocket reconnect delays (5–160 seconds); a stable minute resets the delay. Upgrade requests share the request budget and respect numeric/date Retry-After headers. TLS checks match REST and credentials never cross an upgrade redirect. Disable realtime in Preferences → Network for a polling-only connection.

**REST synchronization.** Each poll requests `subscriptions.get?updatedSince=` for the room list and `chat.syncMessages?lastUpdate=` for the open room, both over the same keep-alive connection. Cursors always come from server timestamps (`_updatedAt`, HTTP `Date`), never the Amiga clock. Local time is derived from the server's `Date` header compared with the Amiga clock, falling back to the Locale preferences.

**Server compatibility.** Endpoints: `/api/info`, `login`, `me`, `logout`, `subscriptions.get`, `subscriptions.read`, `channels|groups|im.history`, `chat.syncMessages`, `chat.sendMessage`, `chat.getThreadMessages`, `chat.update`, `chat.delete`, `chat.react`. Rocket.Chat 9.0 changed REST status codes, so success is judged from the JSON `success`/`status` fields rather than the HTTP status alone.

## Roadmap

- Inline Markdown styling (bold/italic via soft styles)
- Notifications (screen title, ScreenNotify/Ringhio) and Locale catalogs
- 68000 build without TLS (plain HTTP inside a LAN)

Drag the horizontal divider above the composer to resize it. The client remembers
its height in the same configuration file as the other settings.

Right-click a message or select it and press **Amiga+T** (**Edit → Message / thread
actions**) to quote it, load its thread, page through replies or send a reply.
Select a thread reply to act on that message. **Load for editing** copies your own
selected message into the separate action editor; **Save edit** submits the new
text. Deletion requires confirmation. Reactions use a shortcode (for example
`smile`), with explicit Add and Remove buttons. Workspace permissions and edit
or delete time limits still apply; failed or unconfirmed actions retain the text
and are never automatically replayed. The main conversation draft stays intact.

In **AmiRocketPrefs → Servers → Edit**, each server has mentions-only, notification
beep/screen flash and quiet-mode controls. Quiet mode suppresses message alerts,
including message events sent to plugins/dock subscribers; unread counts remain
available. Use applies these immediately and Save also persists them in the single
`ENVARC:AmiRocket.prefs` file. Restart both client and Preferences after upgrading.

Use **Edit → Copy/Cut/Paste** (**Amiga+C/X/V**) in the composer. Click a chat message to select it (a coloured bar marks it), then **Amiga+C** or **Edit → Copy selected message** copies its full plain text to the Amiga clipboard. Click the composer to return Copy to the draft.

**Full-size images open externally by default.** Set the executable under
**AmiRocketPrefs → Helpers → Image viewer executable** (default:
`SYS:Utilities/MultiView`). With **Open full-size images in external viewer**
enabled, click an expanded thumbnail to download its original to a temporary
file and open the viewer without a Save requester. An image label toggles its
server thumbnail; if no separate thumbnail exists, it opens externally instead.
**Room → Open selected image externally** is also available. Use **Save attachment**
to choose a permanent file yourself.

Originals are not decoded by AmiRocket. Up to four originals share an 8 MiB
temporary-file budget (2 MiB in low-memory mode), within the existing download
limits. Reopening a cached original launches the viewer without another download.
Temporary originals remain in the configured cache drawer under `Originals` while the asynchronous viewer may need them,
including after the client exits. Close viewers before choosing **Room → Clear
temporary image files**; the client also offers this when its cache is full.
Use applies the viewer settings immediately; Save persists them in the same
preferences file. Restart client and Preferences after installing this build (the live settings protocol has changed).

Image previews use Rocket.Chat's `image_url`, while **Save attachment** uses the
original `title_link`. Enable server thumbnails under **Workspace Settings →
Message → Message Attachments**; 480×360 works within the client limits. Choose
160, 320 or 640 pixels in **AmiRocketPrefs → General → Preview width** (maximum
height is nine sixteenths of the width; images retain their aspect ratio and
are not enlarged). Low-memory mode caps previews at 160×90. Use applies the
size immediately and Save also persists it in the same preferences file.

Previews load on click by default and are cached in memory for reopening.
General preferences offers **Enable thumbnail previews** (off means text-only)
and **Load visible thumbnails automatically**. Automatic mode queues one visible,
server-provided thumbnail at a time, never an original, and does not automatically
retry failures. It is disabled in low-memory mode. Turning previews off does not
prevent Open or Save. The default 320-pixel preview fits within 320×180 while
retaining its proportions.
The 2 MiB encoded and one-million-pixel safety limits apply to the downloaded
preview, not to its larger original; low-memory mode has stricter limits. If the
server supplies only the original URL, the existing limits still apply. This
client change does not regenerate missing server thumbnails or install a local
image converter. Preview decoding still needs a suitable picture datatype.

Fenced code blocks (triple backticks, optionally followed by a language such as `yaml`) use a shaded fixed-width layout. Indentation, blank lines and literal Markdown are preserved; long code lines remain on a single row and are clipped to the available width. HTTP and HTTPS links are underlined and clickable, including wrapped URLs. Link opening uses **C:OpenURL** with your configured Amiga browser. Chat avatars reuse the authenticated, size-limited avatar cache used by the sidebar.

**New conversations.** Use **Create New** or the Chat menu to open a direct/group message or create a public/private channel. Enter one or more comma-separated usernames; channel membership is optional. The server's permission and validation errors are displayed. Teams and discussions are not implemented yet.

**Composer toolbar.** The buttons insert emoji shortcodes, bold, italic, strike-through, inline code, fenced code blocks and links. File opens the upload requester; Find searches the current conversation. Formatting wraps the selected text; Send remains available alongside Enter. Microphone/video controls are not shown because those features are not implemented.

**Separate preferences app.** Launch **AmiRocketPrefs** directly, or choose **Project → Preferences**. It needs no server connection. General controls polling, emoji graphics, avatars and inline images; Appearance controls themes and fonts; Plugins configures loadable modules; Helpers configures native browser/image-viewer integrations; Diagnostics and About provide application information. Use broadcasts temporary settings to running clients without a disk write. Save applies now and writes the single `ENVARC:AmiRocket.prefs` file. Cancel leaves settings unchanged. Updates reach running clients through native message ports without requiring a restart or focus change. Keep AmiRocketPrefs beside AmiRocket.

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
at the top of the sidebar, a resizable composer (four rows by default), and no composer hint or bottom
status strip. Enter sends and Shift+Enter inserts a newline as before.


Reliability and workspace
-------------------------
Read receipts are issued by the UI after a conversation is visible in the
active window; server-event views, background servers and inactive windows do
not mark cached conversations read. Network failures retry after 5, 10, 20,
40, 80, 160 and then 300 seconds. Authentication failures stop retries.
Right-click a server and choose Retry Now to try immediately. F10 also opens
the selected server's actions while its event view is displayed.

Attachment downloads have a progress window with Cancel and Retry.
Inline thumbnail loading is non-blocking and displays status and Retry on its card. Cancellation interrupts active transfer waits
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
Polling and remembered-token login use exponential reconnect delays with a
5-second to 5-minute base plus jitter, including temporary HTTP 5xx/429 failures;
a longer Retry-After takes precedence (up to 24 hours). Expired authentication
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

## Network load and proxy protection

**AmiRocketPrefs → Network** sets a minimum request gap for each configured server
(default **1000 ms**, one request per second, without a startup burst), the idle
polling interval (default **30 seconds**) and persistent avatar caching. All
requests, including login, read receipts and avatar fallback URLs, pass through
the same per-server governor. Multiple clients or profiles still add to your
proxy's total traffic; these limits do not replace HAProxy's aggregate policy.

The bounded 64-command queue prioritises sends and room actions over cosmetic
refreshes, and coalesces duplicate pending poll/presence requests. Failed or
unconfirmed sends are never automatically replayed. Disconnect cancels queued
operations for that server. Name suggestions and room searches use local data
and make no requests while typing.

Without realtime, room subscriptions refresh every 30 seconds, presence at most every
60 seconds while a conversation is foreground, and message polling slows to the
idle interval after six unchanged polls or while the conversation is hidden.
New activity or returning to the conversation restores active polling.

HTTP 429, server errors and transport failures trigger exponential cooldowns
with jitter. `Retry-After` delta-seconds and HTTP-date values are respected
(up to 24 hours); response `Date` is preferred over the local clock. During a
cooldown, queued requests wait and no background retries bypass the pause.
A stale keep-alive GET can reconnect once; mutating requests are never replayed.
Cooldown events appear in the server's event view.

**Diagnostics → Refresh network statistics** shows sampled per-server request
rates, queued operations, cooldowns, last HTTP status, error/429 counts and disk
cache hits. Values are sampled every five seconds; HTTP 0 means no HTTP response
was available. The request rate is a sliding estimate, not an HAProxy measurement.

Avatars are cached for 24 hours in the configured cache drawer’s `Avatars` directory: 64 fixed slots with
at most 128 KiB of image data each (8 MiB plus small headers). Keys include the
server URL, authenticated user ID and avatar username; a full-key check prevents
hash collisions from showing another account's cached image. Expired, mismatched or incomplete
entries are downloaded again. Caching failures fall back to normal downloads.
Disable caching in Network, or delete this cache drawer to clear it. Cache data
is separate from configuration; all settings still live in `ENVARC:AmiRocket.prefs`.
The editable/ARexx keys are `request_spacing_ms` (100–5000),
`background_interval` (15–600) and `avatar_disk_cache` (0/1). Use changes the
running session; Save also persists them.

## Connection health, recovery and conversation tools

Server roots and the chat header show **Connected**, **Cooling down**,
**Reconnecting** or **Offline**. Recovery states include a retry countdown; the
server event view keeps the detailed error. An authenticated session retains its
room tree during temporary recovery, so queued user actions remain accessible.

Draft text and cursor position are checkpointed every two seconds and on room
or server switches. Recovery is scoped to the server URL, authenticated account
and conversation. The selected room is saved when opened so restart can return
to it. `SYS:AmiRocket/AmiRocket.drafts` (or the selected shared-data drawer) is private user data, separate from
the single settings file; updates preserve it and release packaging refuses to
include it. Atomic replacement and backup recovery protect the previous snapshot.
The bounded store holds 64 private draft/delivery records, with up to 4000 composer characters per text draft. Failed
writes keep drafts in memory; a switch is blocked if the buffer cannot be retained
safely. The header flags unsaved recovery data and server events record the reason.
Confirmed sends clear only a matching draft. Close AmiRocket before
manually deleting its recovery file and `.bak` file.

Choose **Room → Search this conversation** (Amiga+F) or **Find** on the toolbar.
Search runs when submitted, with up to 25 results per page and Previous/Next controls.
Select a result to read and copy its full text in the read-only preview. Closing
the dialog safely discards late replies. Requests use the existing per-server
limits and cooldowns. See Rocket.Chat's [search API](https://developer.rocket.chat/apidocs/search-message).

Choose **File** on the toolbar or **Room → Upload attachment** to select a file
up to 8 MiB. Uploads stream through a 4 KiB buffer, show progress and support
cancellation, including while queued during a cooldown. Upload sends the selected
file; composer text stays as a draft. The client uses the current
[media upload](https://developer.rocket.chat/apidocs/upload-media-files-to-a-room)
and [confirmation](https://developer.rocket.chat/apidocs/check-uploaded-file)
endpoints. Neither POST is automatically repeated. After an unconfirmed response,
check the conversation before uploading again. Server permissions, API support
and configured file limits still apply. Dock status queries stay responsive
during transfers; commands that could alter the transfer context return busy.

**Preferences → Memory** configures retained chat messages (50–1000 per server)
and encoded image-cache size (256–8192 KiB). **Low-memory mode** caps retention
at 200 messages, disables avatar downloads and emoji drawing, limits previews to
two images and 512 KiB of encoded data, and caps buffered downloads at 2 MiB.
The message limit applies to each server's cached conversation; search results
are separately limited to 25 per page. **Room → Clear media caches** releases
in-memory previews/avatars and clears the bounded disk avatar cache where writable.
Use and Save apply these options dynamically like the other preferences.

Realtime protocol tests run with `make test`, including real TCP/TLS upgrades, masked frames, fragmented messages, heartbeats, room switching, authentication/subscription rejection and certificate failure. WebSocket messages are bounded to 128 KiB; unsupported extensions or oversized frames fall back to REST. For a reverse proxy, forward HTTP/1.1 WebSocket Upgrade/Connection headers and allow long-lived connections to the workspace `/websocket` path (including any configured path prefix). No proxy changes are needed for polling fallback.

### Realtime release checks

WebSocket TCP/TLS/upgrade negotiation shares a 10-second monotonic deadline;
stream reads are nonblocking. The default-port Host header matches REST so
virtual-host proxies receive the expected hostname.
See [Realtime validation](docs/REALTIME-VALIDATION.md) for automated disconnect
recovery tests, private live-server checks and the opt-in two-hour native soak.

The transfer window includes a native percentage bar alongside byte counts.
Cancel interrupts stalled transfer reads instead of waiting for a socket timeout.
Cancelled downloads do not replace existing destination files or launch the viewer.

The installer offers a default download drawer in every mode, including Novice;
the default is `SYS:Downloads`. Intermediate/Expert can also choose the media-cache
drawer (default: `SYS:AmiRocket/Cache` (or the selected shared-data drawer’s `Cache`)). Both paths are editable under
AmiRocketPrefs → Helpers, and Use/Save applies them through the shared preferences
file. Save requesters begin in the selected download drawer; each file can still
be saved elsewhere. The cache drawer contains avatar data and temporary thumbnail
decoding files; decoded thumbnails are also cached in memory.

Code blocks hide their Markdown fences in displayed messages; the original fences remain in the editor and copied text.
A lightweight colour pass distinguishes common keywords, quoted strings, numbers
and line comments. Indentation, blank lines and spaces are preserved; tabs display
at four-column stops. Long code lines are clipped rather than reflowed. Copying a
message retains its original text, including literal tabs and spaces.

Notification sounds are configured under **AmiRocketPrefs → Notifications**.
Leave the file blank for the Workbench system beep, or enter an installed sound
file and choose **Test sound**. Short 8SVX files work with the standard sound
datatype; other formats need an installed sound datatype. Files are limited to
256 KiB and cached for reuse. Enable sounds per server under Servers → Edit.
Quiet mode, mention-only filtering and the active-conversation rule still apply;
a burst is limited to one audible alert every two seconds.

File downloads and external-image originals are streamed through a bounded I/O
buffer to a temporary disk file. A 5 MiB download does not require a 5–8 MiB
contiguous response allocation. Failed or cancelled transfers discard the partial
file and preserve an existing destination. File-size limits still apply.
MultiView opens on Workbench and brings that screen forward; custom viewer
commands retain their normal launch behavior. If the configured executable is missing, the client reports its exact path; choose an installed viewer in Preferences / Helpers. Temporary originals are stored
under the configured cache drawer's `Originals` directory.

Click an existing reaction badge to add your reaction; click your own selected
badge again to remove it. Requests use Rocket.Chat's explicit `shouldReact`
setter, and repeated clicks are held while a request is pending.

`Sounds/Notification.8svx` is the included original 0.28-second notification
chime, generated by `scripts/make-notification-sound.py`. It is the default for
new settings. Installation also replaces the old blank/system-beep setting with the bundled chime and records its installed path; existing custom sound files are preserved. Choose **Use bundled sound**
in Preferences → Notifications to select it after an update.

## Release 0.2 interaction improvements

The composer keeps failed or unconfirmed sends as drafts. **Retry send** checks
whether the original message arrived, then reuses its delivery ID if sending is
still needed. Delivery records are saved beside private draft text, so they
survive restart. A changed draft asks before creating a new message. Nothing is
retried automatically. **Room > Forget unconfirmed delivery** removes the ID only
after confirmation and keeps the draft; check history first because a later new
send can duplicate a previously delivered message. These recovery records share
the bounded private draft store and never appear in diagnostic reports.

This uses Rocket.Chat's [caller-supplied message IDs](https://developer.rocket.chat/apidocs/send-message)
and [single-message confirmation endpoint](https://developer.rocket.chat/apidocs/get-message).
Thread and attachment actions remain explicit; uploads are not automatically retried.

**First unread** jumps to the read boundary. When requested explicitly it loads
older pages, one at a time, until that boundary or the configured message limit.
Opening a room does not trigger that history scan. Escape stops further paging.
The existing new-message divider remains visible in the conversation.

Keyboard shortcuts work in the composer: **Ctrl+Tab / Ctrl+Shift+Tab** switch
between visible conversations; **Ctrl+K** focuses a fresh sidebar search; Enter
opens its first match; **Ctrl+U** opens the next unread conversation. Escape
closes search/message windows, dismisses suggestions or cancels a transfer.
Existing Amiga menu shortcuts remain available.

Preferences > Memory offers **Use low-resource preset**: 100 retained messages,
256 KiB image cache, 160-pixel previews, no automatic thumbnails or avatar/emoji
drawing, a 60-second poll interval, 300-second background interval and 1.5-second
request spacing. Realtime and disk avatar caching stay enabled. Choose Use or
Save to apply the staged settings; server accounts and custom paths are retained.

Transfers show average speed and an ETA when the server supplies a total size.
Unknown totals are labelled explicitly. Opening a viewer and other short-lived
actions are reported temporarily in the chat header. Preferences > Diagnostics
shows fixed connection diagnoses plus HTTP/WebSocket status, queue and cooldown
statistics. **Copy diagnostic report** excludes credentials, URLs and message text.
