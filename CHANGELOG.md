# Changelog

Notable changes per release. The git history before 1.2.5 was squashed into the first
commit, so 1.2.5 is the earliest entry.

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
