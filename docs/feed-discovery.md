# Feed discovery and silent sign-in

This document describes the sign-in and feed-discovery work added in October 2026: what
changed, why, and what each change buys. It is written for someone who knows the code
base and the AVD protocol flow but did not follow the work itself.

The short version: Vindauga no longer needs an imported `.rdp` file. The user signs in
once with their work account, the app lists every desktop and app the account is entitled
to, and later connections need no browser sign-in at all. The `.rdp` import is still there
as an alternative.

## Background

Until now the only way to add a connection was to download an `.rdp` file from Microsoft's
web client and import it. That was the main onboarding hurdle, and every connection then
opened the embedded Entra ID sign-in page (with an autofill hack to make it bearable).

The earlier attempt at feed discovery in `src/core` was never wired up, for two reasons
that only became clear while testing against a real tenant:

1. It used the OAuth device-code flow, which most Conditional Access policies block.
2. The feed service rejects any client that does not send the `X-MS-User-Agent` header
   Microsoft's own web client sends (`INCOMPATIBLE_CLIENT_VERSION`).

The Python project [avd_feed_connect](https://github.com/shakeelosmani/avd_feed_connect)
proved that authorization code + PKCE with the public "Microsoft Remote Desktop" client ID
works, and which headers the feed wants. Those findings were verified against a real
tenant before anything was built on them.

## What the user gets

| Before | After |
|---|---|
| Download an `.rdp` file from the web client, import it | Click **Sign in**, pick the desktop from the list |
| Entra sign-in page (web view) on every connect | No sign-in page on connect; tokens are minted silently from a stored refresh token |
| Sign-in page shown immediately, autofill hack types the password | Page shown only if input is needed; SSO cookies kept between sign-ins, so re-auth is usually one click or nothing |
| Empty list after a network hiccup | List and icons cached locally; a failed refresh keeps the last list and says why |
| Network drop ends the session with an error card | Session reconnects automatically (resumed, not logged on again) |
| Cryptic `AADSTS70043` in the log | A sentence explaining that the organisation requires a new sign-in |

## Changes, benefit, and the issue each one solves

### Sign-in and tokens (`src/core/Auth`)

**AuthManager rewritten around auth-code + PKCE.** The interactive sign-in reuses the
existing `AadInteractiveAuth` web-view capture (the same mechanism FreeRDP's AAD step
used), exchanges the code for a feed access token, an id_token (UPN) and a refresh token,
and stores the refresh token in the OS keychain.
*Benefit:* works with Conditional Access; one sign-in gives everything.
*Solves:* device-code flow blocked; `QtNetworkAuth` dependency gone.

**AuthManager as `AadTokenProvider` for FreeRDP.** At connect time `RdpSession` asks the
provider first and only falls back to the web view when it returns nothing. The AVD
gateway token (`ACCESS_TOKEN_TYPE_AVD`) is a refresh grant with FreeRDP's own scope; the
session-host token (`ACCESS_TOKEN_TYPE_AAD`, host pools with `enablerdsaadauth`) is a
refresh grant with FreeRDP's proof-of-possession key as `req_cnf`.
*Benefit:* no browser on connect.
*Solves:* the sign-in page on every connection.
*Verified:* gateway token on a real host pool. The `req_cnf` path is implemented but
unverified, because the test host pool does not have `enablerdsaadauth`; it falls back to
the web view if the grant fails.

**Access tokens cached per scope.** Tokens are kept in memory until a minute before expiry,
keyed by scope plus `req_cnf` (a proof-of-possession token is bound to one key).
*Benefit:* a reconnect or a second connection costs no round trip and no keychain write.
*Solves:* every grant rotates the refresh token, so each one was a network call plus a
keychain write; FreeRDP's auto-reconnect would have doubled that.

**Sign-out cannot be undone by a grant in flight.** A generation counter is captured
before each network call and re-checked after it and after every keychain write; an
operation lock serialises keychain transitions against sign-out.
*Solves:* two races found in review: a refresh completing after sign-out resurrected the
token, and a sign-in that returned no refresh token kept the previous account's.

**Readable error explanations** (`AuthManager::explainAuthError`). Maps the common
`AADSTS` codes (sign-in frequency, inactivity expiry, Conditional Access, MFA required)
and transport errors to one actionable sentence, with the code appended for support.
*Solves:* "token refresh failed" with no indication of what to do.

**Configurable User-Agent for sign-in.** One string applied to both the sign-in web view
and the token requests (Settings, empty by default).
*Solves:* tenants whose Conditional Access only admits certain platforms or browsers,
which they recognise from the User-Agent.

### Feed (`src/core/Feed`)

**FeedClient.** Discovery and per-workspace feed with the required `X-MS-User-Agent`,
bearer downloads of `.rdp` files and icons. Refuses to send the token to any host that is
not under `microsoft.com` or `microsoftonline.com` (the feed XML supplies the URLs).
*Solves:* `INCOMPATIBLE_CLIENT_VERSION`; token leakage to a tampered feed.

**FeedCache.** The last enumeration, each resource's 32x32 PNG icon and the last
downloaded `.rdp` text are stored under `~/.cache/vindauga/`. Loaded at startup, refreshed
in the background; a failed refresh keeps the list. The cached `.rdp` is used when the
download fails but the gateway is reachable.
*Benefit:* the list is on screen instantly; the app is usable through feed outages.
*Solves:* empty list at every start and after every hiccup.

**Configurable discovery URL** (Settings, empty = public cloud) and the trust list extended
to the sovereign-cloud hosts (`azure.us`, `microsoftonline.us`, `azure.cn`,
`chinacloudapi.cn`).
*Solves:* sovereign clouds (Azure Government, Azure China) serve the feed from another host.
*Limitation:* those clouds also have their own sign-in authority; it can be set with
`VINDAUGA_LOGIN_HOST` but there is no Settings entry for it, and none of this has been
verified against a sovereign tenant.

### Session (`src/core/Rdp/RdpSession`)

**Auto-reconnect after a network drop.** `AutoReconnectionEnabled` is forced on (the
feed's `.rdp` files do not set it), so the server issues a reconnect cookie and the Windows
session is resumed rather than logged on again. The retry loop is Vindauga's own rather
than `client_auto_reconnect_ex()`: that helper sleeps through its full retry delay, which
would make "End session" block for seconds; ours waits on FreeRDP's abort event and stops
immediately. The gating (only transport failures; never credential errors, server logoffs
or cancels) mirrors FreeRDP's helper. Twenty attempts, five seconds apart.
*Benefit:* a Wi-Fi roam, VPN re-key or laptop resume no longer ends the session.
*Note:* exercised only by code review and a normal session, not by an induced network
drop; the "Try again" card remains as the fallback.

**Keep session awake** (`FakeMouseMotionInterval`, Settings, 0 = off). Synthetic mouse
motion after N idle seconds, FreeRDP's `/prevent-session-lock`.
*Solves:* the gateway's idle timeout and the Windows lock screen during long reads.
Off by default because the lock-screen policy belongs to the organisation.

**Compression.** Was listed as a candidate improvement; libfreerdp 3 already enables RDP
6.1 bulk compression by default, so no change was needed. Documented here so the question
is not reopened.

### Sign-in web view (`src/ui/backend/AadWebAuthenticator`)

**Persistent profile.** All sign-in views share one on-disk `QQuickWebEngineProfile`
(`entra-signin`) with forced persistent cookies. QtWebEngine's default profile is
off-the-record and forgot Microsoft's SSO cookies as soon as the view was destroyed, which
is why the autofill hack existed.
*Benefit:* with `login_hint` (the stored UPN) and a valid Microsoft session, re-auth
completes with no input; otherwise it is one click on the account tile. Password and MFA
only reappear when the tenant demands them.

**Delayed display.** The view starts hidden and is shown after three seconds only if the
sign-in has not completed by then.
*Benefit:* a sign-in that Microsoft completes on its own never flashes a browser.

**Popups blocked.** `newWindowRequested` is ignored.
*Solves:* orphaned popup windows outside the overlay that could never report a code.

**Autofill demoted to fallback.** Still runs when the stored credentials exist, now also
clicking the stored account's tile on the picker, but it is no longer what makes
sign-in bearable.

**Sign-out clears the profile's cookies**, so the next sign-in starts from a clean picker
and can pick another account.

### UI (`src/ui`)

**Home page** (`ConnectionsPage.qml`): account header with Sign in / Refresh / Sign out,
a status banner for sign-in and feed problems, a grid of desktop/app cards with icons and
workspace names, and the imported `.rdp` files below. **Settings** gained keep-awake,
discovery URL and browser identity. **SessionPage** hands `AppController.auth` to
`RdpItem.aadTokenProvider` and shows the reconnect overlay for mid-session reconnects.

**AppController** owns `AuthManager`, `FeedCache` and a single worker thread: silent
sign-in at startup, interactive sign-in, feed refresh, icon downloads and `.rdp`
downloads all run there, one after another, and report back to the GUI thread. Results
carry the job generation they started under; sign-out bumps it, so a refresh in flight
cannot repopulate the list afterwards.

**Feed resources use `feed:<resource id>` as connection id**, the keychain key for RDSTLS
credentials. Resource ids are stable across refreshes, so the credentials survive.

### Build and packaging

`Qt6::NetworkAuth` is no longer linked or required. CMake, README, the Arch `PKGBUILD`,
the `.deb` script and the CI workflow were updated accordingly.

## Verification

Run on 2026-10-17 against a real tenant (one workspace, one host pool, no
`enablerdsaadauth`), using `vindauga_cli`:

| Step | Result |
|---|---|
| `login` (auth-code + PKCE in the web view) | Signed in; feed token and refresh token received; refresh token in the keychain |
| `list` (discovery + feed with `X-MS-User-Agent`) | 1 workspace, 1 resource |
| `refresh-test` (refresh grant for FreeRDP's AVD gateway scope) | Token obtained without a browser |
| `connect 0 <id>` | Gateway token obtained silently, RDSTLS credentials from the keychain, Windows desktop shown, no browser window |

GUI run the same day, after the integration: the app started with a silent sign-in from
the stored refresh token, showed the cached list immediately and refreshed it in the
background; clicking the desktop card downloaded the `.rdp` file, obtained the gateway
token silently, took the RDSTLS credentials from the keychain and reached the Windows
logon screen without a browser window or a dialog. The reconnect loop and the persistent
sign-in profile were verified by build, unit tests and an independent code review, not by
an induced network drop or a forced re-authentication.

## Environment variables (unchanged, for completeness)

| Variable | Purpose |
|---|---|
| `VINDAUGA_TENANT` | Pin the Entra tenant (default `organizations`) |
| `VINDAUGA_CLIENT_ID` | Override the public client ID |
| `VINDAUGA_LOGIN_HOST` | Sign-in authority, e.g. `https://login.microsoftonline.us` (default public cloud) |
| `VINDAUGA_FEED_DISCOVERY_URL` | Discovery endpoint (the Settings value takes precedence in the GUI) |
| `VINDAUGA_FEED_USER_AGENT` | The `X-MS-User-Agent` value, should Microsoft retire the built-in string |

## Review follow-ups

The independent review of this round found three issues, all fixed before hand-over:
application exit could deadlock while the sign-in page was open (the worker thread was
blocked on a hop to the GUI thread that the destructor no longer served; the authenticator
now has `shutdown()` and the destructor keeps delivering events while joining), the trust
list rejected the sovereign-cloud hosts the discovery URL setting was meant for, and a
resource whose icon URL changed left its old icon file behind.

## Fixes after the first run on a second machine

Running the branch on a second machine (Qt 6.12, QtKeychain 0.17, FreeRDP 3.32) crashed
when the desktop card was clicked. The core dump pointed at
`QKeychain::JobExecutor::jobFinished` on the feed worker thread.

### Keychain access pinned to the main thread (`CredentialStore`, `AuthManager`)

QtKeychain has one process-wide job queue that is created on the thread that starts the
first job and connects every later job to it with an automatic connection. The code
started jobs from three threads (GUI thread for the credential dialog, feed worker for
the silent sign-in, FreeRDP thread for the connect-time token grant). When the queue
lived on the worker and the GUI thread ran a job, the queue's "finished" bookkeeping
arrived as a queued call after the stack-allocated job had been destroyed: a
use-after-free that showed up as a segfault, or as a queue stuck busy so that every later
keychain call hung. Whether it crashed depended on timing, which is why one machine was
fine and the other was not.

- `CredentialStore` now runs every job on the main thread. A call from another thread
  blocks until the main thread has run it (`QMetaObject::invokeMethod` with
  `Qt::BlockingQueuedConnection`).
- `AuthManager::adoptTokens` no longer blocks a worker on the keychain write. A rotated
  refresh token is recorded as pending and flushed from the main thread's event loop;
  `~AuthManager` flushes synchronously so a rotation right before quit is not lost (a
  lost rotation means the next start needs a fresh interactive sign-in).
- `refreshGrant` and `signInInteractive` no longer hold `m_opMutex` across a keychain
  call. A worker waiting for the main thread while holding a lock that `signOut()` (main
  thread) takes would deadlock a Sign out click; the generation counter already covers
  the race that lock was guarding.

Benefit: connecting from the card no longer crashes or hangs on the keychain, on every
machine. Solves: use-after-free in QtKeychain's job queue, and two deadlock windows the
first fix would have introduced.

### Session loop no longer spins after the first input (`RdpSession`)

`wakeEvent`, the WinPR event the GUI thread signals to wake the session loop for queued
input, was created as auto-reset. WinPR has no auto-reset events: `CreateEvent` logs
"auto-reset events not yet implemented" and the handle's wait cleanup is a no-op. After
the first key press or mouse move the event stayed signalled, so `WaitForMultipleObjects`
returned immediately on every iteration and the loop ran at full CPU for the rest of the
session (present since 1.2.5). The event is now manual-reset and the loop resets it right
after each wake-up, before draining the event queue. Benefit: idle session CPU is back to
a few percent, and the WinPR error line is gone from the log.

### Clearer status while Azure starts a session host (`RdpSession`)

FreeRDP reports a deallocated session host that Azure is booting through its retry
callback with the internal tag `arm-transport`, which the UI showed verbatim as
"arm-transport (3/20)". It now reads "Waiting for the session host to start (3/20)".

## Open points

- The `req_cnf` (RDSTLS proof-of-possession) refresh grant needs a host pool with
  `enablerdsaadauth` to be verified.
- Auto-reconnect has not been exercised with an induced network drop.
- Windows 365 uses the same feed and file format but has not been tested.
