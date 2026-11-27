# AmiRocket changelog

## 0.2 - 2026-11-27

### Features
- Add restart-safe delivery IDs and an explicit Retry send flow for unconfirmed composer messages, with server-side delivery checks before retrying.
- Add a one-click low-resource preset and copyable connection diagnostics covering DNS, TLS, authentication, throttling and WebSocket fallback.
- Add visible First unread and Retry send controls, Ctrl+Tab conversation switching, Ctrl+K conversation search and Ctrl+U next unread.

- Add clickable reaction badges and an original bundled 8SVX notification chime.
- Add cached notification sound files, a Notifications preferences page with playback testing, and burst limiting while respecting per-server alert rules.
- Add a native transfer progress bar, interruptible active download cancellation, configurable media-cache/download drawers and preformatted code fences with lightweight syntax colours.
- Open full-size images directly in a configured external viewer, with inline thumbnails and a bounded temporary-original cache.
- Add message quotes, paged thread viewing and replies, editing/deleting own messages and reaction add/remove actions.
- Add per-server mentions-only alerts, system beep/flash and quiet mode while preserving unread badges.
- Add authenticated realtime WebSocket event subscriptions, coalesced delta synchronization, heartbeat handling and automatic polling fallback.
- Show connection health and retry countdowns in server roots and the chat header.
- Recover per-account conversation drafts after restart, with periodic checkpoints and bounded, atomic private storage.
- Search conversation history with paginated results and a full-text preview.
- Stream attachment uploads with progress, cancellation and the media upload/confirmation API.
- Add per-server request pacing, adaptive polling, shared cooldowns, network diagnostics and bounded persistent avatar caching.
- Record custom installation locations in the shared configuration and detect them automatically on subsequent installer runs.

### Enhancements
- Show attachment transfer speed and estimated time remaining, external-viewer handoff feedback and temporary action messages in the chat header.
- Load older unread history only on explicit request, within configured retention and request pacing limits; Escape stops further paging or cancels transfers.


- Keep shared resources, plugins, sounds and the default cache in SYS:AmiRocket, allow an Expert shared-data destination, and preserve custom plugins and drafts when moving it.

- Installation shows its summary before a final Finish or Finish and start AmiRocket choice, then exits quietly without a completion popup behind the client.

- Fresh installations place the client directly in SYS:Utilities and the manual in HELP:, without an AmiRocket application subdrawer.
- Select the installed bundled notification chime during installation and migrate old blank sound settings while preserving custom sounds.
- Replace attachment links with compact cards, separate Preview/Open/Save actions, non-blocking loading and Retry, cached aspect-preserving thumbnails, and text-only/on-click/automatic preview settings.
- Show installed paths on completion and offer a one-time client launch without enabling autostart.
- Install the client in Utilities and AmiRocketPrefs in Prefs without copying the installer; allow separate destinations in Intermediate/Expert mode and a shared configuration drawer in Expert mode.
- Offer 160, 320 and 640-pixel image preview widths with a low-memory cap.
- Check the actual network worker idle wake budget and sustained request pacing during release testing.
- Add a draggable composer divider with a four-row default and remembered height.
- Add automated REST/WebSocket recovery checks and opt-in live-server and native multi-profile stability runs.
- Add a live realtime toggle in Network preferences, bounded stream buffers and proxy-aware upgrade/reconnect pacing.
- Keep read-only ARexx status and subscriptions responsive during transfers while guarding UI-changing commands.
- Add dynamic low-memory controls for message retention, image-cache size, avatar/emoji rendering and media-cache clearing.
- Prioritise user actions over cosmetic requests, coalesce pending refreshes and keep searches local without typing-triggered requests.
- Novice mode installs fresh copies to SYS:Utilities/AmiRocket automatically and offers Update or Uninstall for existing copies. Directory selection remains available in Intermediate and Expert modes.
- Use the current packaging date for LHA and ZIP members, share one timestamp across release archives and record it in build metadata.

### Bugs
- Detach list nodes before closing search and message-action windows to prevent cleanup stalls in the native GUI regression suite.
- Keep private recovery data and delivery IDs scoped to their server account and conversation; failed sends are never replayed automatically.


- Create icons and resolve client paths correctly for custom drawers, assign roots and trailing separators.

- Installer detection no longer mistakes a leftover AmiRocket drawer for the installed client after uninstall.
- Stream file downloads to disk to avoid file-sized memory allocations, bring MultiView forward on Workbench, and hide code fences outside editing.
- Keep file-transfer progress updating beyond the first three events by collecting GUI acknowledgements during downloads.
- Use server-generated image URLs for previews while retaining original attachment URLs for downloads, and apply byte limits to the preview itself.
- Prevent the network worker from spinning on cancelled timer replies and consuming a CPU while idle.
- Preserve a reconnect queued after a disconnect while cancelling older work, and retain same-origin rate-limit cooldowns across manual reconnects.
- Match default-port WebSocket Host headers to REST for hostname-based proxy routing.
- Bound TCP, TLS and WebSocket upgrade negotiation with one monotonic deadline, keep stream reads nonblocking and start retry backoff after an attempt finishes.
- Allow queued transfer cancellation during server cooldowns and keep draft buffers safe when recovery storage is full or unavailable.
- Respect Retry-After and back off with jitter after throttling, server errors or transport failures to reduce proxy overload.
- Prevent insert-volume requesters when probing missing assigns or stale saved installation paths.
- Fix GitLab publication failing with HTTP 302 during the final latest-release verification.
- Fix packaged files displaying 1 January 1980 after extraction.

## 0.1 - 2026-11-18

### Features
- Native AmigaOS 3.2 Rocket.Chat client with multiple servers, channel and direct-message trees, unread navigation, mentions and server event views.
- Multiline messages, code blocks, clipboard support, clickable links, attachment downloads and expandable image previews.
- Native plugin SDK with Timestamp and Quick Reply examples, plus ARexx commands, state snapshots and sequenced notifications.
- Workbench iconification and separate Preferences with session-only Use, persistent Save and one shared configuration file.

### Enhancements
- Themes, configurable fonts and polling, avatar and emoji controls, and a message-formatting toolbar.
- Download progress and cancellation, and reconnect recovery without automatic message replay.
- Installer maintenance choices for Update or Uninstall, with optional settings removal, preservation of custom files and no Workbench autostart registration.
- Verified runtime, SDK and source packages, checksums, build provenance and GitHub, GitLab and Aminet publication tooling.
- PHP website with native emulator screenshots and automatic GitHub release versions, asset links, sizes and download counts.

### Bugs
- Fixed text contrast with the Workbench theme and long group-message labels in the sidebar.
- Fixed arrow-key selection in user suggestions, Enter-to-send and Shift+Enter for new lines.
- Fixed Preferences Use/Save handling and attachment download and image expansion failures.
