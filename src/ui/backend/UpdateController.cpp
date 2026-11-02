#include "UpdateController.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>

#include <algorithm>

#include "Logging.h"

namespace vindauga {

namespace {

constexpr qint64 kCheckIntervalSecs = 24 * 3600;
constexpr qint64 kManualCooldownSecs = 60;
constexpr qint64 kBaseBackoffSecs = 3600;
constexpr int kLauncherRestartDelayMs = 1500;
const char* kInstalledLauncher = "/usr/bin/vindauga";

QString currentVersion() {
    return QCoreApplication::applicationVersion();
}

} // namespace

UpdateController::UpdateController(QObject* parent)
    : QObject(parent),
      m_settings(std::make_unique<AppSettings>()),
      m_checker(new UpdateChecker(UpdateChecker::defaultApiUrl(), this)),
      m_downloader(new UpdateDownloader(this)),
      m_installer(new UpdateInstaller(this)),
      m_kind(UpdateChecker::detectPackageKind()),
      m_debug(qEnvironmentVariableIsSet("VINDAUGA_UPDATE_DEBUG")) {
    // Automatic install only from our own package: the binary lives under /usr or
    // /opt/vindauga and /usr/bin/vindauga (the launcher the package installs, and the
    // .deb's wrapper that sets up the bundled Qt) exists. A build run from a source tree
    // gets the notification but never touches the system's packages.
    const QString exe = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    m_managedInstall = (exe.startsWith(QStringLiteral("/usr/")) ||
                        exe.startsWith(QStringLiteral("/opt/vindauga/"))) &&
                       QFileInfo::exists(QString::fromLatin1(kInstalledLauncher));
    if (m_debug) {
        qCWarning(lcUpdate) << "VINDAUGA_UPDATE_DEBUG is set: the latest release is offered "
                               "even if it is not newer, and may be installed from a non-installed build";
        m_managedInstall = true;
    }
    // In debug mode m_managedInstall can be true without the launcher existing.
    const bool haveLauncher = QFileInfo::exists(QString::fromLatin1(kInstalledLauncher));
    // Captured now: once the package is replaced, /proc/self/exe reads "... (deleted)".
    m_launcher = m_managedInstall && haveLauncher ? QString::fromLatin1(kInstalledLauncher) : exe;

    connect(m_checker, &UpdateChecker::finished, this,
            [this](const UpdateChecker::Result& r) { onChecked(r, m_manualCheck); });
    // The changelog arrives after the banner is already up; "What's new" shows the
    // release body until then (or for good, if the fetch fails).
    connect(m_checker, &UpdateChecker::changelogFetched, this,
            [this](const QString& version, const QString& text) {
                if (version == m_release.version && !text.isEmpty() && text != m_release.notes) {
                    m_release.notes = text;
                    emit releaseChanged();
                }
            });
    connect(m_downloader, &UpdateDownloader::progress, this, [this](qint64 got, qint64 total) {
        const double p = total > 0 ? double(got) / double(total) : -1;
        if (p != m_progress) {
            m_progress = p;
            emit progressChanged();
        }
    });
    connect(m_downloader, &UpdateDownloader::finished, this, &UpdateController::onDownloaded);
    connect(m_installer, &UpdateInstaller::finished, this, &UpdateController::onInstalled);

    // A check shortly after start-up (not at once: the window, sign-in and feed come
    // first, and a random spread keeps everyone from hitting GitHub in the same second
    // after a release), then an hourly local test of "is it a day since the last one?".
    if (m_debug)
        QTimer::singleShot(2000, this, [this]() { startCheck(true); });
    else
        QTimer::singleShot(30000 + QRandomGenerator::global()->bounded(120000), this,
                           &UpdateController::maybeCheck);
    m_hourly.setInterval(3600 * 1000);
    connect(&m_hourly, &QTimer::timeout, this, &UpdateController::maybeCheck);
    m_hourly.start();
}

UpdateController::~UpdateController() = default;

bool UpdateController::checkForUpdates() const {
    return m_settings->checkForUpdates();
}

void UpdateController::setCheckForUpdates(bool value) {
    if (value == m_settings->checkForUpdates())
        return;
    m_settings->setCheckForUpdates(value);
    if (!value) {
        m_checker->abort();
        if (m_checking) {
            m_checking = false;
            emit checkingChanged();
        }
        if (m_state == Available || m_state == Failed)
            setState(Idle);
    }
    emit checkForUpdatesChanged();
}

bool UpdateController::canInstall() const {
    return m_managedInstall && m_kind != PackageKind::Unknown && m_release.installable();
}

QDateTime UpdateController::lastChecked() const {
    const qint64 t = m_settings->updateLastCheck();
    return t > 0 ? QDateTime::fromSecsSinceEpoch(t) : QDateTime();
}

void UpdateController::maybeCheck() {
    if (!m_settings->checkForUpdates() || m_checking || m_state == Downloading ||
        m_state == Installing || m_state == Restarting)
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (now < m_settings->updateNextAttempt())
        return; // backing off after a failure
    if (now - m_settings->updateLastCheck() < kCheckIntervalSecs)
        return;
    startCheck(false);
}

void UpdateController::checkNow() {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_checking || m_state == Downloading || m_state == Installing ||
        m_state == Restarting || now - m_lastManualCheck < kManualCooldownSecs)
        return;
    m_lastManualCheck = now;
    startCheck(true);
}

void UpdateController::startCheck(bool manual) {
    m_manualCheck = manual;
    m_checking = true;
    emit checkingChanged();
    if (manual)
        setStatusText(tr("Checking…"));
    qCInfo(lcUpdate) << "Checking for updates" << (manual ? "(manual)" : "");
    m_checker->check(m_kind);
}

void UpdateController::onChecked(const UpdateChecker::Result& result, bool manual) {
    m_checking = false;
    emit checkingChanged();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    if (!result.ok) {
        if (m_debug) {
            setStatusText(tr("Could not check for updates (%1)").arg(result.error));
            return;
        }
        // Exponential backoff (1 h, 2 h, 4 h ... up to a day), or what GitHub asked for.
        const int failures = m_settings->updateFailures() + 1;
        m_settings->setUpdateFailures(failures);
        const qint64 backoff =
            result.retryAfterSecs > 0
                ? result.retryAfterSecs
                : std::min<qint64>(kBaseBackoffSecs << std::min(failures - 1, 5), kCheckIntervalSecs);
        m_settings->setUpdateNextAttempt(now + backoff);
        setStatusText(manual ? tr("Could not check for updates (%1)").arg(result.error) : QString());
        return;
    }

    if (!m_debug) {
        m_settings->setUpdateFailures(0);
        m_settings->setUpdateNextAttempt(0);
        m_settings->setUpdateLastCheck(now);
    }

    const bool newer = m_debug || UpdateChecker::isNewer(result.release.version, currentVersion());
    qCInfo(lcUpdate) << "Latest release" << result.release.version << "running" << currentVersion()
                     << (newer ? "-> update available" : "-> up to date");
    if (!newer) {
        setStatusText(tr("Vindauga is up to date"));
        return;
    }
    // A skipped version stays hidden for the daily check, but a deliberate "Check now"
    // shows it again.
    if (!manual && result.release.version == m_settings->updateSkippedVersion()) {
        setStatusText(QString());
        return;
    }
    if (m_state != Idle && m_state != Available)
        return; // mid-install: leave it alone
    if (result.release.version != m_release.version) {
        removeDownload(); // a package from an older offer
        m_release = result.release;
        emit releaseChanged();
    }
    setStatusText(tr("Version %1 is available").arg(m_release.version));
    setState(Available);
    m_checker->fetchChangelog(m_release.version, currentVersion());
}

void UpdateController::install() {
    if (m_state != Available && m_state != Failed)
        return;
    if (!canInstall())
        return;
    setError(QString());
    if (!m_downloadedFile.isEmpty() && QFile::exists(m_downloadedFile)) {
        startInstall(); // e.g. the password prompt was closed; do not download 120 MB again
        return;
    }
    QDir dir(updatesDir());
    dir.removeRecursively(); // earlier, unfinished or already installed packages
    if (!QDir().mkpath(dir.path())) {
        setError(tr("Cannot create %1").arg(dir.path()));
        setState(Failed);
        return;
    }
    m_progress = -1;
    emit progressChanged();
    setState(Downloading);
    const QString dest = dir.filePath(m_release.assetName);
    m_downloadedFile = dest;
    m_downloader->start(m_release.assetUrl, dest, m_release.sha256, m_release.size);
}

void UpdateController::cancel() {
    if (m_state == Downloading)
        m_downloader->cancel();
}

void UpdateController::onDownloaded(UpdateDownloader::Outcome outcome, const QString& error) {
    if (outcome == UpdateDownloader::Outcome::Cancelled) {
        m_downloadedFile.clear();
        setState(Available);
        return;
    }
    if (outcome == UpdateDownloader::Outcome::Failed) {
        m_downloadedFile.clear();
        qCWarning(lcUpdate) << "Update download failed:" << error;
        setError(error);
        setState(Failed);
        return;
    }
    startInstall();
}

void UpdateController::startInstall() {
    setState(Installing);
    m_installer->start(m_kind, m_downloadedFile, m_release.sha256);
}

void UpdateController::onInstalled(UpdateInstaller::Outcome outcome, const QString& details) {
    switch (outcome) {
    case UpdateInstaller::Outcome::Ok:
        removeDownload();
        // The offer is spent: should the restart fail, "Try again" must not offer to
        // install the same package again (canInstall turns false).
        m_release = ReleaseInfo();
        emit releaseChanged();
        setState(Restarting);
        QTimer::singleShot(kLauncherRestartDelayMs, this, &UpdateController::restartApp);
        break;
    case UpdateInstaller::Outcome::Dismissed:
        // Not an error: the user closed the prompt. Back to the banner; the verified file
        // is kept so that "Update now" again goes straight to the prompt.
        setState(Available);
        break;
    case UpdateInstaller::Outcome::NoAgent:
        setError(tr("Vindauga could not ask for your password (no polkit authentication "
                    "agent is running). Install the update from a terminal instead:"),
                 UpdateInstaller::manualCommand(m_kind, m_downloadedFile));
        setState(Failed);
        break;
    case UpdateInstaller::Outcome::Failed:
        setError(details.isEmpty() ? tr("The installation failed.")
                                   : tr("The installation failed:\n%1").arg(details));
        setState(Failed);
        break;
    }
}

void UpdateController::restartApp() {
    QStringList args = QCoreApplication::arguments();
    args.removeFirst();
    // Through sh with a short sleep: this process still holds its window (and any
    // single-instance resources) for a moment, and the new one should start after it is
    // gone. The launcher is the one captured at start-up; see the constructor.
    const QStringList shArgs = QStringList{QStringLiteral("-c"),
                                           QStringLiteral("sleep 1; exec \"$0\" \"$@\""),
                                           m_launcher} + args;
    if (QProcess::startDetached(QStringLiteral("/bin/sh"), shArgs)) {
        qCInfo(lcUpdate) << "Update installed; restarting";
        QCoreApplication::quit();
    } else {
        qCWarning(lcUpdate) << "Update installed but the restart failed";
        setError(tr("The update was installed. Please start Vindauga again."));
        setState(Failed);
    }
}

void UpdateController::dismiss() {
    if (m_state == Available || m_state == Failed)
        setState(Idle);
}

void UpdateController::skipVersion() {
    m_settings->setUpdateSkippedVersion(m_release.version);
    removeDownload();
    setState(Idle);
}

void UpdateController::openReleasePage() {
    QUrl url = m_release.pageUrl;
    if (url.isEmpty())
        url = QUrl(QStringLiteral("https://github.com/ETroll/Vindauga/releases/latest"));
    QDesktopServices::openUrl(url);
}

void UpdateController::copyManualCommand() {
    if (!m_manualCommand.isEmpty())
        QGuiApplication::clipboard()->setText(m_manualCommand);
}

void UpdateController::setState(State state) {
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
}

void UpdateController::setError(const QString& error, const QString& manualCommand) {
    m_error = error;
    m_manualCommand = manualCommand;
    emit errorChanged();
}

void UpdateController::setStatusText(const QString& text) {
    m_statusText = text;
    emit statusTextChanged();
}

QString UpdateController::updatesDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
           QStringLiteral("/vindauga/updates");
}

void UpdateController::removeDownload() {
    if (!m_downloadedFile.isEmpty())
        QFile::remove(m_downloadedFile);
    m_downloadedFile.clear();
}

} // namespace vindauga
