#pragma once
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "Settings/AppSettings.h"
#include "Update/UpdateChecker.h"
#include "Update/UpdateDownloader.h"
#include "Update/UpdateInstaller.h"

namespace vindauga {

// Glue between the update classes in core/ and the QML update banner/settings.
//
// Policy: at most one check a day (persisted in AppSettings, so restarting the app does
// not re-check), a check shortly after start-up and then once an hour as a cheap local
// "is a day up?" test, exponential backoff after failures (and GitHub's own Retry-After /
// rate-limit reset), and nothing at all when the user has turned the check off. A found
// update is only ever offered; nothing is downloaded or installed until the user clicks.
//
// "Update now" downloads the package for this system, verifies it against GitHub's
// SHA-256 digest, installs it through pkexec (a polkit password prompt) and restarts the
// app. Only installs that come from our own package (/usr/bin/vindauga) can do that; a
// build run from a source tree is only told that a newer version exists.
//
// Testing the whole flow without publishing a release: start the app with
// VINDAUGA_UPDATE_DEBUG=1. The check then runs 2 s after start-up, the latest release is
// treated as newer even if it is the same version (installing it over yourself), the
// once-a-day bookkeeping and "skipped version" are ignored and not written, and a build
// that is not an installed package may install too. The package is still downloaded,
// verified and installed for real (pkexec), so only use it when that is what you want.
// VINDAUGA_UPDATE_API_URL can point the check at a local fake release instead.
class UpdateController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool checkForUpdates READ checkForUpdates WRITE setCheckForUpdates NOTIFY
                   checkForUpdatesChanged)
    // The version on offer (while state is not Idle) and its release notes (markdown).
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY releaseChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY releaseChanged)
    // False for a build that is not our installed package: the banner then offers the
    // release page instead of "Update now".
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY releaseChanged)
    // 0..1 while Downloading; -1 when the size is not known.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    // Set with errorText when installing through pkexec is not possible; for the user
    // to run in a terminal.
    Q_PROPERTY(QString manualCommand READ manualCommand NOTIFY errorChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    // Result of the last manual "Check now" ("Vindauga is up to date", ...).
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QDateTime lastChecked READ lastChecked NOTIFY statusTextChanged)
public:
    enum State { Idle, Available, Downloading, Installing, Restarting, Failed };
    Q_ENUM(State)

    explicit UpdateController(QObject* parent = nullptr);
    ~UpdateController() override;

    State state() const { return m_state; }
    bool checkForUpdates() const;
    void setCheckForUpdates(bool value);
    QString availableVersion() const { return m_release.version; }
    QString releaseNotes() const { return m_release.notes; }
    bool canInstall() const;
    double progress() const { return m_progress; }
    QString errorText() const { return m_error; }
    QString manualCommand() const { return m_manualCommand; }
    bool checking() const { return m_checking; }
    QString statusText() const { return m_statusText; }
    QDateTime lastChecked() const;

    // Manual check (Settings → "Check now"): ignores the once-a-day limit, but not more
    // often than once a minute.
    Q_INVOKABLE void checkNow();
    // Downloads (unless already downloaded), installs and restarts.
    Q_INVOKABLE void install();
    // Cancels a running download.
    Q_INVOKABLE void cancel();
    // "Later": hides the banner until the next check finds it again.
    Q_INVOKABLE void dismiss();
    // "Skip this version": hides the banner until a still newer version is released.
    Q_INVOKABLE void skipVersion();
    Q_INVOKABLE void openReleasePage();
    Q_INVOKABLE void copyManualCommand();

signals:
    void stateChanged();
    void checkForUpdatesChanged();
    void releaseChanged();
    void progressChanged();
    void errorChanged();
    void checkingChanged();
    void statusTextChanged();

private:
    void maybeCheck();
    void startCheck(bool manual);
    void onChecked(const UpdateChecker::Result& result, bool manual);
    void setState(State state);
    void setError(const QString& error, const QString& manualCommand = QString());
    void setStatusText(const QString& text);
    void startInstall();
    void onDownloaded(UpdateDownloader::Outcome outcome, const QString& error);
    void onInstalled(UpdateInstaller::Outcome outcome, const QString& details);
    void restartApp();
    QString updatesDir() const;
    void removeDownload();

    std::unique_ptr<AppSettings> m_settings;
    UpdateChecker* m_checker;
    UpdateDownloader* m_downloader;
    UpdateInstaller* m_installer;
    QTimer m_hourly;

    PackageKind m_kind;
    bool m_debug;          // VINDAUGA_UPDATE_DEBUG, see the class comment
    bool m_managedInstall; // running from our own installed package
    QString m_launcher;    // what to start again after the update
    ReleaseInfo m_release;
    QString m_downloadedFile; // verified package kept for a retry after a dismissed prompt
    State m_state = Idle;
    double m_progress = -1;
    QString m_error;
    QString m_manualCommand;
    bool m_checking = false;
    bool m_manualCheck = false;
    QString m_statusText;
    qint64 m_lastManualCheck = 0;
};

} // namespace vindauga
