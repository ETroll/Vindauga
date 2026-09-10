#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

#ifdef VINDAUGA_WITH_RDP
#include <winpr/wlog.h>
#endif

Q_LOGGING_CATEGORY(lcAuth, "vindauga.auth")
Q_LOGGING_CATEGORY(lcFeed, "vindauga.feed")
Q_LOGGING_CATEGORY(lcRdp, "vindauga.rdp")
Q_LOGGING_CATEGORY(lcConnections, "vindauga.connections")
Q_LOGGING_CATEGORY(lcX11, "vindauga.x11")

namespace vindauga {

namespace {

// See DecodeFailureCallback in Logging.h. Global (only one active RdpSession ever) and
// mutex-protected, since it is set from one thread while the call happens on another
// (vindaugaWLogCallback below). Always compiled, even without VINDAUGA_WITH_RDP, because
// setDecodeFailureCallback() is declared unconditionally; only the detection in
// vindaugaWLogCallback is RDP-conditional.
QMutex g_decodeFailureCallbackMutex;
DecodeFailureCallback g_decodeFailureCallback;

// Simple logrotate-style rotation without a third-party dependency. Thread-safe: called
// from Qt's message handler (any thread) and from FreeRDP's WLog callback (RdpSession's
// worker thread or FreeRDP channel threads such as cliprdr).
class RotatingLogFile {
public:
    bool open(const QString& path, qint64 maxBytes, int maxBackups) {
        QMutexLocker lock(&m_mutex);
        m_path = path;
        m_maxBytes = maxBytes;
        m_maxBackups = maxBackups;
        m_file = std::make_unique<QFile>(m_path);
        return m_file->open(QIODevice::Append | QIODevice::Text);
    }

    void write(const QString& line) {
        QMutexLocker lock(&m_mutex);
        if (!m_file || !m_file->isOpen())
            return;
        rotateIfNeeded();
        m_file->write(line.toUtf8());
        m_file->write("\n");
        m_file->flush();
    }

    QString path() const {
        QMutexLocker lock(&m_mutex);
        return m_path;
    }

private:
    // Called with m_mutex already held.
    void rotateIfNeeded() {
        if (m_file->size() < m_maxBytes)
            return;
        m_file->close();
        // Drop the oldest and shift the rest up one slot (.{N-1} -> .{N}, ..., .1 -> .2,
        // path -> .1), the same numbering as logrotate.
        QFile::remove(m_path + "." + QString::number(m_maxBackups));
        for (int i = m_maxBackups - 1; i >= 1; --i)
            QFile::rename(m_path + "." + QString::number(i), m_path + "." + QString::number(i + 1));
        QFile::rename(m_path, m_path + ".1");
        m_file->setFileName(m_path);
        // If reopening fails, write() silently drops everything from here on, so warn on
        // stderr even without --log-terminal: this is a failure condition.
        if (!m_file->open(QIODevice::Append | QIODevice::Text))
            std::fprintf(stderr, "vindauga: failed to reopen log file %s after rotation\n",
                         qPrintable(m_path));
    }

    mutable QMutex m_mutex;
    QString m_path;
    qint64 m_maxBytes = 0;
    int m_maxBackups = 0;
    std::unique_ptr<QFile> m_file;
};

// Logging is process-global (like Qt's own qInstallMessageHandler), and the free C
// callbacks below have no instance to call methods on.
RotatingLogFile g_logFile;
LogLevel g_level = LogLevel::Info;
bool g_logToTerminal = false;

QString levelLabel(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("?");
}

void writeLine(const QString& line) {
    g_logFile.write(line);
    if (g_logToTerminal)
        std::fprintf(stderr, "%s\n", qPrintable(line));
}

// Replaces Qt's default message handler entirely, including the abort() on QtFatalMsg:
// an installed handler must abort itself, or the process survives a fatal error.
void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    const QString category = context.category ? QString::fromUtf8(context.category)
                                              : QStringLiteral("default");
    const QString line = QStringLiteral("%1 [%2] %3: %4")
                             .arg(QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss.zzz"),
                                  levelLabel(type), category, msg);
    writeLine(line);
    if (type == QtFatalMsg)
        std::abort();
}

#ifdef VINDAUGA_WITH_RDP
// Content-based redaction applied to every FreeRDP WLog message regardless of category.
// FreeRDP logs the token-endpoint response (access/refresh/id_token) and the authorization
// code in request bodies at WLOG_DEBUG from several places (e.g. freerdp_http_request() in
// utils.http, shared by the ARM gateway and AAD token flows), so a per-category whitelist
// is inherently incomplete against vendored code.
QString redactSecrets(QString line) {
    // JSON token fields (token-endpoint and ARM gateway responses): keep the structure,
    // redact only the value. The custom raw-string delimiter (RX) is needed because the
    // pattern contains literal ')"' sequences that would end a plain R"(...)" early.
    static const QRegularExpression kTokenField(
        QStringLiteral(R"RX(("(?:access_token|refresh_token|id_token)"\s*:\s*")[^"]*("))RX"));
    // Known sensitive keys in a url-encoded request body or a redirect URL's query string
    // (grant_type=authorization_code&code=..., https://.../?code=...). refresh_token and
    // client_secret are not used today but are redacted defensively. Anchored to '?', '&'
    // or line start (MultilineOption: per physical line of a multi-line WLog message) to
    // avoid false positives such as "status code=200".
    static const QRegularExpression kSensitiveFormParam(
        QStringLiteral(
            R"RX(((?:^|[?&])(?:code|refresh_token|access_token|id_token|client_secret)=)[^&\s]+)RX"),
        QRegularExpression::MultilineOption);
    // Authorization/Cookie/Set-Cookie headers. Not anchored to line start: WLog's own
    // timestamp/category prefix often precedes the header name on the same line.
    static const QRegularExpression kSensitiveHeader(
        QStringLiteral(R"RX((Authorization|Set-Cookie|Cookie):\s*\S.*$)RX"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    // Generic safety net: a bare JWT (eyJ<header>.<payload>.<signature>) without any known
    // key/header around it, e.g. in an error message. Covers access_token/id_token; Entra
    // refresh tokens and authorization codes are opaque, not JWTs, and are handled by the
    // patterns above.
    static const QRegularExpression kBareJwt(QStringLiteral(R"RX(\beyJ[\w-]+\.[\w-]+\.[\w-]+\b)RX"));

    line.replace(kTokenField, QStringLiteral("\\1<redacted>\\2"));
    line.replace(kSensitiveFormParam, QStringLiteral("\\1<redacted>"));
    line.replace(kSensitiveHeader, QStringLiteral("\\1: <redacted>"));
    line.replace(kBareJwt, QStringLiteral("<redacted-jwt>"));
    return line;
}

// Bridge from FreeRDP's WLog to the same rotating file/terminal control as the Qt
// categories above (see the WLOG_APPENDER_CALLBACK setup in initLogging()).
// msg->PrefixString is a stack-allocated buffer in FreeRDP's
// WLog_CallbackAppender_WriteMessage and must be copied here, never stored. FreeRDP's
// prefix already carries timestamp/pid:tid/level/tag, so it is reused as is.
BOOL vindaugaWLogCallback(wLogAppender* /*appender*/, const wLogMessage* msg) {
    if (!msg)
        return FALSE;
    QString line;
    if (msg->PrefixString)
        line += QString::fromUtf8(msg->PrefixString);
    if (msg->TextString)
        line += QString::fromUtf8(msg->TextString);

    // "ignoring update" is unique to the failed-decode branch of
    // gdi_SurfaceCommand_AVC420/444 (no other WLog line in FreeRDP uses this phrase) and
    // marks the moment FreeRDP gives up on a frame. The same failure also logs "H264
    // decompress failed" and "Failed to transfer video frame" on the way there, so
    // triggering on all three would triple-count. Checked on the unredacted line (the
    // phrase never contains anything sensitive) with a plain substring test, keeping this
    // hot path cheap.
    if (line.contains(QStringLiteral("ignoring update"))) {
        // Invoke the callback while the lock is held. Copying the std::function under the
        // lock and calling it outside would let setDecodeFailureCallback(nullptr), called
        // from RdpSession's worker thread right before its teardown, return while a copied
        // callback (capturing a raw RdpSession*) is still running: a use-after-free race.
        // Holding the lock makes setDecodeFailureCallback(nullptr) a true drain. As a
        // deliberate side effect it also serialises the callback's own debounce state
        // across threads without a separate lock.
        QMutexLocker lock(&g_decodeFailureCallbackMutex);
        if (g_decodeFailureCallback)
            g_decodeFailureCallback();
    }

    writeLine(redactSecrets(line));
    return TRUE;
}
#endif

QString stateDirPath() {
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString base = env.value(QStringLiteral("XDG_STATE_HOME"));
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/state");
    return base + QStringLiteral("/vindauga");
}

} // namespace

QString logLevelToString(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return QStringLiteral("debug");
    case LogLevel::Info:
        return QStringLiteral("info");
    case LogLevel::Warning:
        return QStringLiteral("warning");
    case LogLevel::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

bool logLevelFromString(const QString& text, LogLevel& outLevel) {
    const QString t = text.trimmed().toLower();
    if (t == QStringLiteral("debug")) {
        outLevel = LogLevel::Debug;
    } else if (t == QStringLiteral("info")) {
        outLevel = LogLevel::Info;
    } else if (t == QStringLiteral("warning") || t == QStringLiteral("warn")) {
        outLevel = LogLevel::Warning;
    } else if (t == QStringLiteral("error")) {
        outLevel = LogLevel::Error;
    } else {
        return false;
    }
    return true;
}

void initLogging(LogLevel level, bool logToTerminal) {
    g_level = level;
    g_logToTerminal = logToTerminal;

    const QString dir = stateDirPath();
    QDir().mkpath(dir);
    // Active file + 5 rotated backups x 5 MiB = at most ~30 MiB in total: enough for
    // debugging without filling the disk on a long-running machine.
    const QString logPath = dir + QStringLiteral("/vindauga.log");
    if (!g_logFile.open(logPath, 5 * 1024 * 1024, 5)) {
        // Otherwise the app would run normally with nothing logged (e.g. when
        // $XDG_STATE_HOME is not writable). The log file itself is the problem, so stderr
        // is the only sensible place regardless of --log-terminal.
        std::fprintf(stderr, "vindauga: failed to open log file %s — continuing without file logging\n",
                     qPrintable(logPath));
    }

    // Qt side: filter out levels below the threshold before messages are even constructed,
    // for both the vindauga.* categories and Qt's internal qt.* categories.
    QString rules;
    switch (level) {
    case LogLevel::Debug:
        break; // no filtering; everything passes (Qt's default)
    case LogLevel::Info:
        rules = QStringLiteral("*.debug=false");
        break;
    case LogLevel::Warning:
        rules = QStringLiteral("*.debug=false\n*.info=false");
        break;
    case LogLevel::Error:
        rules = QStringLiteral("*.debug=false\n*.info=false\n*.warning=false");
        break;
    }
    QLoggingCategory::setFilterRules(rules);
    qInstallMessageHandler(qtMessageHandler);

#ifdef VINDAUGA_WITH_RDP
    // FreeRDP side: same file and terminal/level control via a WLOG_APPENDER_CALLBACK
    // appender on the root logger, replacing FreeRDP's default console appender.
    wLog* root = WLog_GetRoot();
    if (root && WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK)) {
        wLogAppender* appender = WLog_GetLogAppender(root);
        wLogCallbacksEx callbacks = {};
        callbacks.message = vindaugaWLogCallback;
        WLog_ConfigureAppender(appender, "callbacksEx", &callbacks);
        WLog_OpenAppender(root);
    }
    const char* wlogLevel = "INFO";
    switch (level) {
    case LogLevel::Debug:
        wlogLevel = "DEBUG";
        break;
    case LogLevel::Info:
        wlogLevel = "INFO";
        break;
    case LogLevel::Warning:
        wlogLevel = "WARN";
        break;
    case LogLevel::Error:
        wlogLevel = "ERROR";
        break;
    }
    WLog_SetStringLogLevel(root, wlogLevel);

    // Secrets in FreeRDP debug output are handled by redactSecrets() for every WLog
    // message; pinning individual WLog categories to INFO would be incomplete.
#endif
}

QString currentLogFilePath() { return g_logFile.path(); }

void setDecodeFailureCallback(DecodeFailureCallback callback) {
    QMutexLocker lock(&g_decodeFailureCallbackMutex);
    g_decodeFailureCallback = std::move(callback);
}

} // namespace vindauga
