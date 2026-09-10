#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <cstdio>

#include "Logging.h"

#ifdef VINDAUGA_WITH_RDP
#include <QtWebEngineQuick/QtWebEngineQuick>

#include "X11KeymapGuard.h"
#endif

int main(int argc, char* argv[]) {
#ifdef VINDAUGA_WITH_RDP
    // Must be called before QGuiApplication is constructed.
    QtWebEngineQuick::initialize();
#endif
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Vindauga"));
    // setDesktopFileName gives the Wayland app_id "vindauga" so the compositor associates
    // the window with vindauga.desktop (and its hicolor icon); setWindowIcon covers X11 and
    // every window the app opens. The PNG is embedded via qrc (src/app/CMakeLists.txt).
    QGuiApplication::setDesktopFileName(QStringLiteral("vindauga"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/vindauga/vindauga.png")));
#ifdef VINDAUGA_VERSION
    QGuiApplication::setApplicationVersion(QStringLiteral(VINDAUGA_VERSION));
#endif

    // Log level and terminal output are controlled from the command line; see initLogging()
    // in Logging.h (the rolling log file is always active, terminal output only with
    // --log-terminal).
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Vindauga — native AVD/Windows 365 client"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption logTerminalOption(
        QStringLiteral("log-terminal"),
        QStringLiteral("Mirror the log to the terminal (stderr) in addition to the log file."));
    const QCommandLineOption logLevelOption(
        QStringLiteral("log-level"),
        QStringLiteral("Minimum log level: debug, info, warning or error (default: info)."),
        QStringLiteral("level"), QStringLiteral("info"));
    parser.addOption(logTerminalOption);
    parser.addOption(logLevelOption);
    parser.process(app);

    vindauga::LogLevel level = vindauga::LogLevel::Info;
    if (!vindauga::logLevelFromString(parser.value(logLevelOption), level)) {
        std::fprintf(stderr, "Unknown --log-level %s (valid: debug, info, warning, error)\n",
                     qPrintable(parser.value(logLevelOption)));
        return 1;
    }
    vindauga::initLogging(level, parser.isSet(logTerminalOption));

#ifdef VINDAUGA_VERSION
    std::printf("Vindauga %s — log: %s\n", VINDAUGA_VERSION,
                qPrintable(vindauga::currentLogFilePath()));
#else
    std::printf("Vindauga (unknown version) — log: %s\n",
                qPrintable(vindauga::currentLogFilePath()));
#endif
    // stdout is fully buffered (not line-buffered) when not attached to a tty; flush
    // explicitly so the startup banner is visible when redirected or piped.
    std::fflush(stdout);

#ifdef VINDAUGA_WITH_RDP
    // Must run before the first QtWebEngine object is created (Chromium reads
    // _XKB_RULES_NAMES when it initialises ozone, i.e. at the first WebEngineView/profile,
    // not in QtWebEngineQuick::initialize()) and after initLogging() so the warning ends
    // up in the log.
    vindauga::X11KeymapGuard::apply();
#endif

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Vindauga", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
