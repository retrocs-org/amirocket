# AmiRocket SDK, ABI 1

This SDK provides two integration routes: loadable native m68k plugins and the public ARexx port. A NovaDock integration normally uses ARexx; it does not need to load code into AmiRocket.

## Build and try the native examples

From the repository, `make plugins` creates `build/amiga/Plugins/timestamp.arp`, `quickreply.arp` and `build/amiga/AmiRocketMonitor`. The complete Amiga build installs both plugins beside the applications. A standalone copy of this SDK can build them with `make -C sdk`, or `make` inside the installed SDK directory; set `AMIGA_CC` if your cross compiler lives elsewhere.

Start AmiRocketPrefs, open Plugins, enable Timestamp and/or Quick Reply, then press Use or Save. The client gains a Plugins menu immediately. Timestamp inserts `[HH:MM] ` using the client’s local clock. Quick Reply inserts its configurable text into the draft. Neither example sends messages. The examples are disabled by default.

For another plugin, create a lowercase identifier containing letters, digits or underscores, implement `ar_plugin_entry()` from [amirocket_plugin.h](include/amirocket_plugin.h), and link `plugin_header.s` first, without a C startup module. Use the supplied Makefile flags: the first loaded segment must start with `ArPluginFile`, not a normal Amiga executable startup. Keep the supplied header order and compile host and plugin using the m68k C stack calling convention. The `.arp` file uses normal AmigaDOS hunk relocation and is loaded with LoadSeg.

Register it in the same application configuration:

```text
plugin.myplugin.path=PROGDIR:Plugins/myplugin.arp
plugin.myplugin.enabled=1
plugin.myplugin.option=value
```

Use Preferences → Plugins → Advanced plugin key to set these keys, followed by Use/Save. `plugin.<id>.path` and `.enabled` control loading. Other keys belong to the plugin. No directory scanning or automatic execution of unregistered files occurs.

## Lifecycle and host API

The plugin returns an immutable `ArPlugin` descriptor containing ABI, structure size, id, name, action label and callbacks. The id must match its configuration prefix. ABI/header/id mismatches are rejected before callbacks execute. `start` is optional and returns nonzero for success. `action` handles the menu or ARexx invocation. `event` receives application events. `stop` runs before unload. Reloads are deferred until callbacks have returned.

All callbacks run synchronously on the GUI task. Return promptly; do not wait, block, create another UI event loop or call host functions from a different task. Native modules are trusted local code with the same memory access as the client; they are not sandboxed.

`ArPluginHost` provides these callbacks:

| Callback | Behavior |
|---|---|
| `config_get` | Read an option under this plugin’s own prefix into a supplied buffer; negative return means absent/error. |
| `config_set` | Change a plugin option for this running session. Explicit Save is required to persist it. |
| `insert_text` | Insert Latin-1 text at the draft cursor, respecting the 4000-character limit; never sends. |
| `get_selection` | Read the selected chat message’s plain text, or the current draft when no chat message is selected. |
| `local_time` | Format local time as `[HH:MM] `. |
| `status` | Show a brief status line. |

Host callbacks return zero on success, negative on error unless documented otherwise. Treat all passed pointers as borrowed; copy any data retained past the callback. Do not free host-owned strings. Event text and sender/room data come from Rocket.Chat in UTF-8; draft/config/helper UI strings are Amiga Latin-1. The public header reserves compatibility through ABI and structure-size checks. Append-only host additions can be gated by `host->size`; incompatible changes require a new ABI.

Limits: eight enabled plugins, 48 preserved extension/config keys, key length 63 bytes, value length 255 bytes, configuration file under 32 KiB. Unknown keys are preserved when reading and saving. An oversized configuration is rejected rather than silently truncated.

## Use, Save and the one configuration file

`ENVARC:AmiRocket.prefs` is the single persistent file for core and plugin settings. An old `ENV:AmiRocket.prefs` is read only as a migration fallback if no persistent file exists. No separate per-plugin settings files are used.

Use broadcasts an in-memory update to running clients and writes nothing to disk. It needs a running client. Closing/restarting the client restores the saved configuration. Save writes the persistent file through a temporary replacement, then broadcasts the update. Account/window autosaves merge only those fields into the saved settings, so they cannot accidentally persist a temporary Use override. Opening AmiRocketPrefs reads a running client’s effective settings, including temporary plugin options. Multiple clients receive the same update; the editor displays the first client’s effective settings.

Display changes apply immediately. A screen/font change received during a client modal dialog is deferred until that dialog closes. Downloaded images/avatars are released when their display option is disabled. Theme changes preserve the draft.

## ARexx commands

The first client owns `AMIROCKET`; additional clients use `AMIROCKET.2`, etc. Standard ARexx scripts require the system ARexx interpreter/RexxMast. Native applications may also exchange standard `CreateRexxMsg` packets directly, without running a script interpreter.

```rexx
/* Example NovaDock action; does not send chat messages. */
options results
address AMIROCKET 'SHOW'
address AMIROCKET 'STATUS'
if rc = 0 then say result
```

Commands are case-insensitive; keys, room names and plugin ids retain their case. Use `OPTIONS RESULTS` to obtain RESULT. Successful commands return RC 0; invalid/unavailable operations return RC 10; allocation or persistence failures return RC 20. A QUEUED result means the asynchronous network operation was accepted, not that the server succeeded. Server failures remain visible in the client.

| Command | Result/action |
|---|---|
| `HELP` | Supported command names. |
| `STATUS` | Aggregate connection/unread state, selected-server fields, epoch, sequence and port name. |
| `CAPABILITIES` | Protocol version, notification formats, limits and coverage. |
| `DOCKSTATUS` | Coherent aggregate header plus one state line per configured server. |
| `SERVERSTATUS n` | State, unread count, alerts, generation and error for stable server slot n. |
| `ROOMS n [offset]` | Read-only subscription snapshot, eight rooms per page; `next=-1` ends pagination. |
| `CONNECT [n or ALL]`, `DISCONNECT [n or ALL]` | Queue connection control without changing selection or drafts. |
| `GETCONFIG key` | Read an editable UI/plugin setting. Account credentials are not exposed. |
| `SETCONFIG key value` | Validate and apply a session-only setting. Quote a value with spaces if desired; double embedded quotes. |
| `SAVE` | Persist the current effective configuration and broadcast it. |
| `RELOAD` | Discard temporary overrides and reload the saved configuration. |
| `DRAFT` | Read the current draft without changing it. |
| `INSERT text` | Insert text into the editable draft. |
| `SEND [text]` | Send supplied text to the current room, or send the current draft when text is omitted. Supplied text does not overwrite the draft. |
| `OPENROOM [n:]id-or-name` | Open an existing subscribed room. |
| `NEWDIRECT user1,user2` | Open/create a single or group DM. |
| `NEWCHANNEL public-or-private name [user1,user2]` | Create a channel, subject to server permissions. |
| `PLUGINS` | Comma-separated loaded plugin ids. |
| `PLUGIN id` | Invoke that plugin’s action. |
| `SUBSCRIBE port [2]` | Subscribe a public port; opt into reliable-state metadata with version 2. |
| `UNSUBSCRIBE port` | Remove that subscription. |
| `SHOW`, `HIDE` | Bring the client forward or move its window/screen behind others. |
| `PREFERENCES`, `QUIT` | Launch the separate editor or close the client. |

Editable core keys: `realtime` (0/1; apply WebSocket subscriptions or polling fallback immediately), `low_memory` (0/1), `message_limit` (50–1000), `image_cache_kib` (256–8192), `request_spacing_ms` (100–5000), `background_interval` (15–600), `avatar_disk_cache` (0/1), `interval`, `theme`, `ownscreen`, `chat_font`, `chat_font_size`, `preview_images`, `show_emoji`, `show_avatars`, `inline_images`, `open_links`, `browser`, `image_viewer`. Plugin keys begin with `plugin.`. Numeric and Boolean ranges are validated. Theme values use ids such as `workbench`, `mocha` or `nord`.

## Notifications for NovaDock and other apps

Create a public message port, then send `SUBSCRIBE yourport` to AMIROCKET. Events arrive as command-level RexxMsg packets whose argument 0 is:

```text
AMIROCKET_EVENT KIND "room-id" "username" "text"
```

Quotes inside fields are doubled and line breaks/control characters become spaces. Events include CONNECTED, DISCONNECTED, ROOM, ROOMCREATED, MESSAGE, SENT, UNREAD and CONFIG. MESSAGE covers newly received messages in the active room; the client subscribes to the active room and does not receive all workspace messages. UNREAD text is the current total unread count from subscriptions; CONFIG text is `session` or `saved`. Payloads are bounded and long text can be truncated. Notifications describe data, never instructions to execute.

A receiver must reply promptly with `rm_Result1=0`, `rm_Result2=0`, and `ReplyMsg`; the sender owns/frees the packet and argstrings. Do not free incoming notifications. Native packets may have no interpreter task block, so do not reject them solely because `IsRexxMsg()` returns false: validate their standard structure length and command action as well. See the complete [native notification receiver](examples/rexx_monitor.c). Run the compiled AmiRocketMonitor from Shell with `Stack 65536`; Ctrl-C exits. Configure NovaDock to run scripts in [rexx](rexx/), or adapt the receiver to update a badge/widget.

There are eight subscriber slots and at most 16 outstanding notification packets. Missing ports are removed; notifications may be dropped when receivers do not reply. Shutdown waits briefly for replies; bounded public packet storage is retained for a broken receiver so late replies cannot signal a terminated task. This avoids hanging the client on another app’s failure.

Plugin metadata
---------------
Install an optional `<module>.arp.meta` text file beside the module with
`name=`, `version=`, `author=` and `description=` lines. Preferences reads this
metadata without loading or running the module. Settings remain in the shared
AmiRocket.prefs file; the metadata file is descriptive, not configuration.
The Plugins page discovers PROGDIR:Plugins and configured plugin paths, shows
loaded status from the running client, and stages enable/disable and options
until Use or Save. Unknown versions are shown as unknown.


Multi-server control
--------------------
`SERVERS` returns tab-separated lines: server number (1–8), display name, URL,
and connection state. It never returns credentials. `SERVER <number>`
selects a server; subsequent DRAFT, SEND, OPENROOM and creation commands operate
on that selected server. Numbers are stable configuration slots, so removing an
entry can leave gaps. CONNECT uses remembered credentials and returns RC 10 when interactive login is needed. Already healthy/connecting servers are unchanged. ALL is best effort if an allocation fails; inspect DOCKSTATUS for final results.

`UNREAD` and STATUS unread both total cached subscription counts across all configured server trees. `SERVERMESSAGE` events carry
`<server-number>:<room-id>` in the room field, with the sender and message in
the usual user/text fields. They cover newly received messages in each server's
last opened room; subscription polling supplies unread counts for other rooms.
The older MESSAGE event remains available for the currently selected server.
All client and plugin configuration remains in `ENVARC:AmiRocket.prefs`.

### Window visibility
`ICONIFY` keeps the client connected and creates a Workbench AppIcon.
`SHOW` restores an iconified window and brings it forward. `HIDE` sends an
open window/screen behind others. `STATUS` reports `iconified=0` or `1`.
Subscribers receive `WINDOW` events with `iconified` or `restored` text.

## Dock protocol version 2

Use `SUBSCRIBE myport 2`. The legacy envelope remains available without `2`.
Version 2 packets contain:

```text
AMIROCKET_EVENT2 epoch sequence KIND "room" "user" "text" dropped=N truncated=0
```

Epoch changes when the ARexx host restarts. Sequence advances for every application
event, including dropped deliveries; RESYNC reuses the current sequence. Counters
are unsigned 32-bit values on Amiga. `dropped` counts failed deliveries across all
subscribers. Quoted fields double quotes and replace control characters with spaces;
room/user/text budgets are 128/96/384 bytes. Oversized fields set `truncated=1`.

Reply promptly, then refresh DOCKSTATUS on events. Refresh after a changed epoch,
sequence gap, RESYNC, or truncation, and poll every ten seconds even without events.
Resubscribe after restart. Notifications are hints for reconciling state, not a durable
message log. A slow receiver has at most two packets outstanding (16 globally,
eight subscribers); it receives RESYNC when delivery resumes. This keeps another
receiver from consuming the whole notification queue.

DOCKSTATUS's first line contains `version`, `epoch`, `seq`, `dropped`, `event_pending`,
`connected`, `connected_servers`, `servers`, `unread`, `alerts`, `selected`,
`selected_connected`, `selected_unread`, `iconified`, `temporary`, `pending`, and `port`.
`connected` means any healthy server; `alerts` counts rooms with an alert, not mentions.
Each following line includes `server`, `state`, `unread`, `alerts`, `generation`,
`login_required`, `room`, and `error`. States are connected, connecting, reconnecting,
offline or failed. Cached counts may be stale while offline. These queries never mark
messages read or change server selection. Credentials are never included.

ROOMS returns epoch/sequence/generation/next in its header, then tab-separated rows:
`room`, quoted room ID, unread count, alert flag, room type, quoted display name.
IDs and display names are capped at 128 bytes. Restart pagination if the epoch,
sequence or generation changes between pages.

New event kinds:

- SERVERSTATUS: room is the server slot, user is the numeric error code, text is the connection reason.
- SERVERSTATE: room is the server slot, text contains the current server state fields.
- ROOMUNREAD: room is `slot:room-id`, text contains unread/alert counters; removal adds `removed=1`.
- SERVERS: configuration changed; rebuild the server list and snapshot.
- RESYNC: refresh snapshots after subscription or missed delivery.

Unread deltas cover every subscribed room. SERVERMESSAGE previews still cover each
server's last opened room; fetching every room's message bodies is outside this
REST client's notification coverage.

[`include/amirocket_rexx.h`](include/amirocket_rexx.h) supplies bounded field and
event parsers. [`examples/rexx_monitor.c`](examples/rexx_monitor.c) demonstrates
coalesced snapshots, periodic polling, resubscription and bounded timeout handling.
Run `AmiRocketMonitor [AMIROCKET.2]` with `Stack 65536`; Ctrl-C exits. A dock can
use the same approach for unread badges, connection indicators, SHOW and ICONIFY.

### Repeatable dock smoke test

`make qa-gui` also starts a separately compiled monitor from the same source as AmiRocketMonitor,
with test-only closed observation files,
checks aggregate badges from two simulated server states and polling without an
event, then starts the actual AmiRocket executable with two offline test profiles.
It checks ICONIFY/SHOW through real ARexx packets, quits/restarts the client while
the monitor stays running, and restarts the monitor while the client stays running.
All of this runs in a disposable emulator profile, with no credentials or real
servers. The production monitor prints `available=0` and zero badge values when the client
is missing or unresponsive; a valid snapshot restores `available=1`.

This validates the example receiver and client together. A NovaDock plugin still
needs its own widget/badge rendering test once that plugin is implemented.

SENDFAILED events identify the target as `server-slot:room-id`; text explains the
failed or unconfirmed send. They do not trigger automatic message replay. Refresh
the conversation before manually resending an unconfirmed message. Failure events
also appear in that server's event history, without changing its connection state
merely because a send was rejected.

## Distribution licence

The SDK archive includes AmiRocket's LICENSE (GPL-3.0-or-later) and COPYRIGHT.
Beam68-derived release tooling retains its separate MIT notice in the main source distribution.

Server snapshots may report `state=cooldown` while requests are paused, and
`state=reconnecting` during recovery. These indicate temporary recovery; connected counters count healthy connections. Draft recovery data
lives in `AmiRocket.drafts`, outside configuration and plugin settings.

`STATUS` and `DOCKSTATUS` include `transfer=1` during a file transfer. Read-only
queries and subscriptions remain responsive; UI-mutating commands return 10
until completion. `SHOW` raises the transfer window and `QUIT` cancels/ends the
session through the normal shutdown path.
