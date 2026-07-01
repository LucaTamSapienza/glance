# Lessons

> Environment and tooling facts that cost real debugging time. Check here
> before chasing a "weird" hang or a missed event. One `##` per lesson —
> headings are glance's retrieval unit.

## AddressSanitizer deadlocks at init on macOS 26

Any `-fsanitize=address` binary can spin at 100% CPU before reaching `main`:
asan's shadow-memory setup re-enters its own malloc interceptor through the
dyld shared cache and spins on its init lock (`sample <pid>` signature:
`InitializeShadowMemory → … → __sanitizer_mz_malloc → AsanInitFromRtl →
StaticSpinMutex::LockSlow`). Reproduced on Apple clang 17 **and** Homebrew
clang 20; `MallocNanoZone=0` doesn't help; UBSan is unaffected (no shadow
memory). It looks exactly like an infinite build/test loop. Mitigation: the
Makefile probe ([[decisions]]).

## A backgrounded ./glance spins at 100% CPU

With no controlling tty (`&`, some sandboxes) the TUI event loop can't block
on input. The rule (run every glance invocation synchronously) lives in
AGENTS.md; this is the mechanism. Agent subcommands all finish in well under
a second.

## notcurses' NCKEY_RESIZE is unreliable behind an external poll loop

Its input thread only surfaces the resize event when it next wakes for
input, so an app that polls `notcurses_inputready_fd()` itself may never see
it. If you need resize, own SIGWINCH via a self-pipe — see [[decisions]].

## TUIs are testable headless with a PTY harness

Drive glance under a Python `pty` that *answers notcurses' init queries* —
DA1 (`\x1b[c`), CPR (`\x1b[6n`), the OSC color queries — because
`notcurses_init` blocks until they're answered; then resize with
`ioctl(TIOCSWINSZ)` + SIGWINCH and assert on the emitted frames. This is how
the 2026-07-01 resize fix was validated without a terminal.

## Terminals encode Ctrl-chords three different ways

The same Ctrl-V can arrive as the raw control code (0x16), or as `v`/`V`
with the ctrl flag set. glance's `ctrl_is()` (tui.c) accepts all of them —
route every new chord through it, and diagnose real keys with
`./glance --keys`.

Related, probe-confirmed 2026-07-01: **terminal profile key mappings fire
before any keyboard protocol.** On Luca's iTerm2, Option+arrow arrives as a
BARE `b`/`f` even with the kitty protocol active (`keyboard = enhanced`) —
an iTerm2 profile mapping (Natural-Text-Editing-style preset) sends its text
before protocol encoding, and no app can see through it. The tell: the lone
Option keydown arrives as `NCKEY_LALT` with `alt=1` (proof kitty is on), yet
the combo arrives as plain text. Fix is terminal-side: delete the ⌥←/⌥→
rows in iTerm2's Key Mappings, after which the arrows arrive as
alt+Left/Right (in enhanced mode via kitty; in legacy as CSI `1;3C/D`).
Cmd+Left/Right arrive as Ctrl-A/Ctrl-E and are already bound to line
start/end. Terminal.app doesn't speak the kitty protocol at all.

## The kitty keyboard stack can outlive the app (iTerm2)

Reproduced live 2026-07-01: after an enhanced-mode glance session on iTerm2,
CSI-u key reports — including release events (`…;1:3u`) — kept streaming
into the shell as literal text: the kitty stack held more pushes than our
two plain pops (resize/refresh cycles may re-push). Fix shipped: the
teardown pop carries a count (`CSI < 64 u`), clearing the whole stack in one
write; popping past the top is a no-op, so over-popping is safe. Un-wedge a
stuck tab with `printf '\x1b[<10u\x1b[=0u'` (or close it). This leak is why
legacy remains the default keyboard mode.

## NCBLIT_PIXEL needs an aspect-tight plane

Blank letterbox cells under a pixel sprixel "annihilate" the text around
them — size the image plane to the image's aspect ratio and stretch
(`NCSCALE_STRETCH`), leaving no margins. Also: reusing one `ncvisual`
across frames corrupts notcurses' sprite bookkeeping (raw sixel/OSC bytes
leak to the screen) — decode per frame; the proper future cache is
persistent planes moved on scroll, not a reused visual.

## The macOS pasteboard is lazy

Right after a screenshot or a "copy image", the first read can return empty
(a *promised* pasteboard): check `clipboard info` first (fail fast on plain
text) and retry ~10×150 ms — `clipboard.c` does exactly this.

## Stacked PRs: deleting a base branch closes its child PR

GitHub does not retarget children when a merged PR's branch is deleted —
that is how M2's original PR #10 died and had to be reopened as PR #14.
Retarget each child first (`gh pr edit N --base main`) and don't
`--delete-branch` mid-chain.
