#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMetaObject>
#include <QQuickWindow>
#include <QTimer>
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
    // Developer aid for checking the UI without a real session, e.g.
    //   QT_QPA_PLATFORM=offscreen vindauga --screenshot home.png --page settings --size 900x1800
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Save a screenshot of the window to <file> and exit."),
        QStringLiteral("file"));
    const QCommandLineOption pageOption(
        QStringLiteral("page"),
        QStringLiteral("Page to show before the screenshot: home or settings (default: home)."),
        QStringLiteral("page"), QStringLiteral("home"));
    const QCommandLineOption sizeOption(
        QStringLiteral("size"),
        QStringLiteral("Window size for the screenshot, e.g. 900x1800."),
        QStringLiteral("WxH"));
    parser.addOption(screenshotOption);
    parser.addOption(pageOption);
    parser.addOption(sizeOption);
    parser.addOption(logTerminalOption);
    parser.addOption(logLevelOption);
    parser.process(app);

    QSize screenshotSize;
    if (parser.isSet(sizeOption)) {
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'));
        const int w = parts.value(0).toInt();
        const int h = parts.value(1).toInt();
        if (parts.size() != 2 || w <= 0 || h <= 0) {
            std::fprintf(stderr, "Invalid --size %s (expected WxH, e.g. 900x1800)\n",
                         qPrintable(parser.value(sizeOption)));
            return 1;
        }
        screenshotSize = QSize(w, h);
    }
    const QString pageName = parser.value(pageOption);
    if (pageName != QLatin1String("home") && pageName != QLatin1String("settings")) {
        std::fprintf(stderr, "Unknown --page %s (valid: home, settings)\n", qPrintable(pageName));
        return 1;
    }

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

    if (parser.isSet(screenshotOption)) {
        // Give the cached feed and the first layout time to settle, switch page, resize,
        // let the layout settle again, then grab.
        const QString file = parser.value(screenshotOption);
        QTimer::singleShot(2500, &app, [&engine, pageName, screenshotSize, file]() {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
            if (!window) {
                QCoreApplication::exit(1);
                return;
            }
            QMetaObject::invokeMethod(window, "openPage", Q_ARG(QVariant, pageName));
            if (screenshotSize.isValid())
                window->resize(screenshotSize);
            QTimer::singleShot(1000, window, [window, file]() {
                const bool ok = window->grabWindow().save(file);
                if (!ok)
                    std::fprintf(stderr, "Could not save screenshot to %s\n", qPrintable(file));
                QCoreApplication::exit(ok ? 0 : 1);
            });
        });
    }

    return app.exec();
}
