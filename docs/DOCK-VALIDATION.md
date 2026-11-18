# Dock integration smoke validation

Validated 18 November 2026 in a disposable Workbench 3.2.1/68060 emulator profile.
The full native suite passed **320 checks, zero failures**; its embedded core
suite passed **183 checks, zero failures**.

## Covered

- A separately launched monitor receives real native ARexx notifications and queries
  coherent snapshots from the controlled GUI fixture.
- Two synthetic server subscriptions aggregate to a badge of 12, then 16 after an
  event. An unannounced change to 10 is recovered by the periodic poll.
- The actual AmiRocket executable starts separately with two offline profiles,
  no credentials and auto-connect disabled.
- A different process sends ICONIFY and SHOW over the public ARexx port; the
  monitor observes the resulting visibility state.
- The client quits, the still-running monitor reports unavailable with cleared
  badge state, and a restarted client supplies a new epoch and fresh snapshot.
- The monitor quits/restarts while the client remains running, rebuilds its
  subscription, and continues receiving visibility updates.

The test receiver is built from `sdk/examples/rexx_monitor.c` with
`AR_MONITOR_QA`, which only adds immediately closed observation files in T:.
The protocol, timer, parsing and recovery code are shared with the installed
AmiRocketMonitor. Neither observer files nor the QA executable are release
configuration or installed application settings. Shell launches explicitly set
Stack 131072. The harness does not pump the synthetic GUI fixture's activation
messages after stopping that fixture's worker; the real client services its own
window normally.

## Repeat

Close FS-UAE, then run `make qa-gui` from the repository. The test launches and
stops its own client/monitor processes inside the disposable profile. Read the
resulting `launch.log` under the run's profile for the per-phase trace and final
check counts. No real server or account is used.

## Scope

This validates the client/receiver integration and restart recovery. Badge
rendering and actions inside an actual NovaDock plugin need a further smoke test
when that plugin is implemented. Live-server notification delivery, and a
complete application crash instead of a clean QUIT/restart, are separate checks.
