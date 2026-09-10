#pragma once
#include <QLoggingCategory>
#include <QString>
#include <functional>

// One logging category per module; use qCDebug(lcAuth) etc.
// Never log tokens, codes or passwords; log lengths or "<redacted>".
Q_DECLARE_LOGGING_CATEGORY(lcAuth)
Q_DECLARE_LOGGING_CATEGORY(lcFeed)
Q_DECLARE_LOGGING_CATEGORY(lcRdp)
Q_DECLARE_LOGGING_CATEGORY(lcConnections)
Q_DECLARE_LOGGING_CATEGORY(lcX11)

namespace vindauga {

// Levels in increasing severity; selecting one shows it and everything more severe.
enum class LogLevel { Debug, Info, Warning, Error };

QString logLevelToString(LogLevel level);
// Returns false for an unknown string (case-insensitive: debug/info/warning/error)
// without modifying `outLevel`.
bool logLevelFromString(const QString& text, LogLevel& outLevel);

// Initialises logging for the rest of the process. Must be called before anything else
// logs (typically right after QGuiApplication/QCoreApplication is created and before
// QQmlApplicationEngine). Sets up:
//  - A rotating log file under $XDG_STATE_HOME/vindauga/ (or ~/.local/state/vindauga/ if
//    XDG_STATE_HOME is unset), always active regardless of `logToTerminal` so a log exists
//    after the fact even when terminal output was off. Rotates at a fixed maximum size and
//    keeps a fixed number of old files (see Logging.cpp); never grows unbounded.
//  - Qt's qInstallMessageHandler + QLoggingCategory::setFilterRules for the vindauga.*
//    categories and Qt's own internal categories.
//  - FreeRDP's WLog (only with VINDAUGA_WITH_RDP): a WLOG_APPENDER_CALLBACK appender on
//    the root logger so all FreeRDP "[HH:MM:SS][pid:tid][LEVEL][com.freerdp.xxx]" lines
//    land in the same rotating file with the same terminal/level control, replacing
//    FreeRDP's default console appender.
// `logToTerminal` additionally mirrors every formatted line to stderr; the file log is
// written regardless.
void initLogging(LogLevel level, bool logToTerminal);

// Absolute path of the active log file (valid after initLogging()); shown to the user
// in the startup banner.
QString currentLogFilePath();

// FreeRDP's RDPGFX client API has no notification for a failed H264/VAAPI decode; the
// only signal is the WLog debug line gdi_SurfaceCommand_AVC420/444 logs
// ("<codec>_decompress failure: %d, ignoring update.") right before silently dropping the
// update. This hook lets RdpSession listen for exactly that line without Logging.cpp
// knowing about RdpSession (no circular dependency inside core/). Only one listener at a
// time (last call wins), which suffices because there is never more than one active
// RdpSession. The callback runs on whichever thread triggered the WLog line (usually
// RdpSession's worker thread, but do not assume that) and must be safe to call from there.
//
// Lifetime contract: the callback is invoked while the internal registration lock is
// held, so setDecodeFailureCallback(nullptr) is a true drain: it cannot return until an
// in-flight invocation of the previous callback has finished. Any code registering a
// listener must therefore call setDecodeFailureCallback(nullptr) before the object the
// listener captures (typically [this]) starts being destroyed; see
// RdpSession::runConnectionLoop() for the established pattern. The callback must not do
// heavy work synchronously (it holds the global lock) and must never call
// setDecodeFailureCallback() itself, directly or indirectly, or it deadlocks.
// nullptr removes a previously registered listener.
using DecodeFailureCallback = std::function<void()>;
void setDecodeFailureCallback(DecodeFailureCallback callback);

} // namespace vindauga
