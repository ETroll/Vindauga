# Changelog

Notable changes per release. The git history before 1.2.5 was squashed into the first
commit, so 1.2.5 is the earliest entry.

## Unreleased

- Update check: Vindauga looks for a newer release on GitHub at most once a day and shows a
  banner on the start page. **Update now** downloads the package for your system, verifies
  it against GitHub's SHA-256 digest, installs it through a polkit password prompt
  (`pkexec`; if no prompt can be shown, the command to run by hand is displayed) and
  restarts the app. **Later**, **Skip this version** and **What's new** (the release notes in a
  dialog; the release workflow now uses this changelog as the release text) are on the banner. The check can be turned off in Settings → Updates (which also has **Check now**).

## 1.5.2 — 2026-11-02

- Fix the Arch package requiring a `qt6-webengine` version newer than Omarchy has shipped
  yet (`unable to satisfy dependency`). The release build now builds against Omarchy's own
  package mirror instead of plain Arch, so the computed dependency floor stays installable.

## 1.5.1 — 2026-10-30

- Fix very high memory use in long sessions (4 GB after seven hours seen): screen updates
  queued up without limit while the window was not being drawn (another workspace,
  minimised). The queue is now capped at 32 MiB and collapsed into one frame.
- Fix a broken picture after resizing the window back and forth: the first full update for
  the new size could be discarded, leaving only scattered patches.
- Fix the mouse pointer becoming invisible after a resize (server rounds the width to an
  even number, which left the pointer scaled by a fractional ratio).
- Drive redirection (`drivestoredirect:s:*`) is now always off: FreeRDP re-registered the
  gvfs mount every second, and it exposed every host mount to the remote session.
- USB redirection (`usbdevicestoredirect:s:*`) is now always off: plugging in a USB device
  crashed the process inside FreeRDP's urbdrc channel. Camera, microphone and audio are
  unaffected.

## 1.5.0 — 2026-10-27

- Clipboard redirection is back (it had been off since 1.2.5): copy and paste of text,
  images and rich text (HTML) in both directions between host and remote session.
- Clipboard transfers run off the connection's channel thread, so they no longer
  take the session down.
- Redirection can still be turned off with `redirectclipboard:i:0` in the .rdp file.
- File copy is not supported.

## 1.4.2 — 2026-10-19

- Arch package: compute the Qt dependency versions when the PKGBUILD is sourced.

## 1.4.1 — 2026-10-19

- Arch package: pin the Qt runtime dependencies to the Qt version the package was built
  against, so a mismatched Qt is refused at install time instead of failing at startup.

## 1.4.0 — 2026-10-18

- Feed discovery: sign in with your Microsoft account and pick desktops from your
  Azure Virtual Desktop feed instead of importing an .rdp file. See
  `docs/feed-discovery.md`.
- Home page with a card per desktop, persistent sign-in, and a feed cache.
- Auto-reconnect after short network interruptions.
- Credential store fixes: keychain access always runs on the main thread.

## 1.3.1 — 2026-10-05

- Keys and mouse buttons that are held when the window loses focus are now released in
  the remote session, so Win/Ctrl/Shift no longer get stuck after the lock screen or a
  compositor shortcut.

## 1.3.0 — 2026-09-16

- The RDP gateway's TLS certificate is now validated, with a trust dialog for unknown
  certificates (issue #1).
- Fix Arch package version drift in the release workflow.

## 1.2.5 — 2026-09-10

- Real .rdp files are ignored by git, and the RDSTLS username is no longer written to the
  log.
- Clipboard redirection disabled, because FreeRDP 3.30 dropped the connection on
  clipboard format lists.
