# Realtime release validation

Run `make test` for the portable checks, socket/TLS WebSocket scenarios and
REST+DDP recovery integration. Recovery tests force transport drops, restore
all four subscriptions, recover messages/edits sent during disconnection,
apply deletions, reject duplicate cache entries, check socket cleanup and
verify that background synchronization does not mark messages read.

The WebSocket handshake has one **10-second monotonic deadline** shared by
TCP connection, TLS negotiation and the HTTP upgrade. A peer sending an
occasional header byte cannot reset that budget. WebSockets reuse an address
selected by REST; they never start an additional synchronous DNS lookup.
REST remains responsible for normal DNS resolution and refreshing that address
when its connection changes. Streaming reads are nonblocking, including
partial TLS records; each outgoing frame has a two-second write budget.
Reconnect backoff begins after a failed attempt finishes, rather than before
its timeout. Host headers omit default ports, matching REST routing through
virtual-host proxies.

## Live server and proxy checks

Use a dedicated private credentials file outside the checkout, mode 600:

```json
{
  "url": "https://chat.example.org",
  "user_id": "YOUR_USER_ID",
  "token": "YOUR_TOKEN",
  "room_id": "ROOM_ID_FOR_READ_ONLY_CHECKS"
}
```

Credentials are read from the file, not command arguments. Never commit it.
The check client uses verified TLS and the same session, framing, subscription,
REST synchronization, cache merging and traffic-governor code as AmiRocket.
Reports contain counters, timings and watched test-message IDs, never tokens
or message contents. Output directories are private.

```sh
make host
python3 -B scripts/check-live-realtime.py \
  --credentials /private/path/session.json \
  --output /private/path/amirocket-validation \
  --duration 7200
```

This mode is read-only. It samples resident memory and socket counts, checks
cache uniqueness and periodically drops/reconnects the stream. It verifies
the requested duration and a live connection at completion. An RSS increase
over 8 MiB after the warm-up or unexpected socket accumulation fails the run.
The live-server check is opt-in and is never part of an ordinary build.

With explicit permission to write temporary messages, add `--self-test`.
This verifies the authenticated user's **one-person self-chat** before writing;
it refuses any conversation containing another user. It sends two labelled
messages, checks delivery, editing, deletion and disconnected-message recovery,
and deletes its messages. Cleanup also runs after ordinary failures or
interruption. `summary.json` reports any cleanup that still needs attention.
No public channel or group is used. Do not use credentials with messaging
permissions you are unwilling to exercise.

## Native GUI and multi-profile soak

The opt-in `qa/release-soak.json` runs normal native checks, then launches the
production client with two profiles against the same workspace/account. It
checks both streams, alternates profiles, iconifies/restores the client,
checks draft preservation, periodically disconnects/reconnects a profile and
samples guest free memory. Sustained memory loss over 2 MiB after warm-up
fails the run. The test never sends its composer draft.

Use a **disposable** emulator profile. Prepare a private directory containing
`session.json` with the keys above, including the room ID. Mount it as
`Secrets:` separately from the build. Keep the entire output parent directory
mode 700 because the disposable guest configuration temporarily holds login
tokens. The harness restores the guest configuration after a normal finish;
remove the private profile after reviewing results.

```sh
mkdir -m 700 /tmp/amirocket-release-check
"/Applications/Amiga Workbench.app/Contents/MacOS/AmigaWorkbench" \
  --workbench latest \
  --mount "AmiRocket:$PWD/build/amiga" \
  --mount "Secrets:/private/path/credentials-directory" \
  --test qa/release-soak.json \
  --output /tmp/amirocket-release-check/results
```

Wait for other emulator jobs to finish before starting this check. The native
run requires working guest networking, AmiSSL and trusted CA certificates.
Mock-server results alone do not establish compatibility with a production
Rocket.Chat workspace or its proxy; retain the live and native results with
the release verification record.

## Idle CPU and request-rate release guards

Run `make amiga build/amiga/AmiRocket-GUIQA` and the isolated `qa/gui.json`
emulator profile for release candidates. The native suite starts the actual
separate-process network worker, sends eight commands and checks that it returns
to sleep: no more than twenty wakes over the command burst plus three idle
seconds. It also checks three real timer-device waits after unrelated signals.
This catches the cancelled-timer reply loop without relying on host CPU load.
A passing wake budget is not a measured hardware CPU percentage.

`make test` also runs a simulated ten-minute saturated request workload through
the production request scheduler, requiring at least one second between default
requests and retaining a 120-second Retry-After pause. Both checks are offline;
continue retaining the live-server compatibility and stability evidence separately.
