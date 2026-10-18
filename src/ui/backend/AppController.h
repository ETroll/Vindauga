#pragma once
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QThread>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

#include "Auth/AuthManager.h"
#include "Connections/ConnectionStore.h"
#include "ConnectionListModel.h"
#include "Feed/FeedCache.h"
#include "ResourceListModel.h"
#include "Settings/AppSettings.h"

#ifdef VINDAUGA_WITH_RDP
#include "AadWebAuthenticator.h"
#endif

namespace vindauga {

// Glue between QML and core. Owns the ConnectionStore, the list models, the settings,
// the AuthManager and the feed cache.
//
// Feed discovery runs on one worker thread (m_workerThread): every job (silent sign-in
// at startup, interactive sign-in, feed refresh, icon downloads, .rdp download) is queued
// there with runOnWorker() and reports back to the GUI thread with runOnGui(). Jobs run
// strictly one after another, so AuthManager (thread-safe in itself) never sees two
// grants at once from here, and the GUI stays responsive while the feed service takes
// its time. Results carry the job generation they were started under: signOut() bumps
// it, so a refresh that was in flight cannot repopulate the list after a sign-out.
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(vindauga::ConnectionListModel* connections READ connectionsModel CONSTANT)
    // Exposed as a property (not a Q_INVOKABLE get/set pair) so QML can bind a CheckBox
    // two-way to it and the session page can pass it on to RdpItem.
    Q_PROPERTY(bool useDisplayScaleFactor READ useDisplayScaleFactor WRITE
                   setUseDisplayScaleFactor NOTIFY useDisplayScaleFactorChanged)
    // availableGpuDevices is CONSTANT (the GPUs in the machine do not change during a
    // running process): a list of {label, path} maps for the settings ComboBox. An empty
    // list (with or without VINDAUGA_WITH_RDP) is safe; the dropdown then only offers
    // the automatic choice.
    Q_PROPERTY(bool hardwareVideoDecoding READ hardwareVideoDecoding WRITE
                   setHardwareVideoDecoding NOTIFY hardwareVideoDecodingChanged)
    Q_PROPERTY(QString preferredVaapiDevice READ preferredVaapiDevice WRITE
                   setPreferredVaapiDevice NOTIFY preferredVaapiDeviceChanged)
    Q_PROPERTY(QVariantList availableGpuDevices READ availableGpuDevices CONSTANT)
    // Same pattern as useDisplayScaleFactor: bound two-way from the settings page and
    // passed on to RdpItem by the session page.
    Q_PROPERTY(int frameBufferMs READ frameBufferMs WRITE setFrameBufferMs NOTIFY
                   frameBufferMsChanged)
    Q_PROPERTY(int preventSessionLockSeconds READ preventSessionLockSeconds WRITE
                   setPreventSessionLockSeconds NOTIFY preventSessionLockSecondsChanged)
    Q_PROPERTY(QString feedDiscoveryUrl READ feedDiscoveryUrl WRITE setFeedDiscoveryUrl NOTIFY
                   feedDiscoveryUrlChanged)
    Q_PROPERTY(QString defaultFeedDiscoveryUrl READ defaultFeedDiscoveryUrl CONSTANT)
    Q_PROPERTY(QString signInUserAgent READ signInUserAgent WRITE setSignInUserAgent NOTIFY
                   signInUserAgentChanged)

    // Feed discovery / account state, see the class comment.
    // `auth` is the AuthManager as a QObject, for SessionPage to hand to
    // RdpItem.aadTokenProvider (silent tokens at connect time).
    Q_PROPERTY(QObject* auth READ authObject CONSTANT)
    // False in a build without RDP support (no web view, so no interactive sign-in).
    Q_PROPERTY(bool feedAvailable READ feedAvailable CONSTANT)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY accountChanged)
    Q_PROPERTY(QString accountName READ accountName NOTIFY accountChanged)
    // True while a sign-in (silent at startup, or interactive) is running.
    Q_PROPERTY(bool authBusy READ authBusy NOTIFY authBusyChanged)
    // True while the feed is being fetched.
    Q_PROPERTY(bool feedBusy READ feedBusy NOTIFY feedBusyChanged)
    // User-facing explanation of the last sign-in/feed problem; empty when all is well.
    Q_PROPERTY(QString feedStatus READ feedStatus NOTIFY feedStatusChanged)
    Q_PROPERTY(vindauga::ResourceListModel* resources READ resourcesModel CONSTANT)
    // When the list was last fetched from the feed (invalid QDateTime if never).
    Q_PROPERTY(QDateTime feedRefreshedAt READ feedRefreshedAt NOTIFY feedChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    QString version() const;
    ConnectionListModel* connectionsModel() const;
    bool useDisplayScaleFactor() const;
    void setUseDisplayScaleFactor(bool value);
    bool hardwareVideoDecoding() const;
    void setHardwareVideoDecoding(bool value);
    QString preferredVaapiDevice() const;
    void setPreferredVaapiDevice(QString value);
    QVariantList availableGpuDevices() const;
    int frameBufferMs() const;
    void setFrameBufferMs(int value);
    int preventSessionLockSeconds() const;
    void setPreventSessionLockSeconds(int value);
    QString feedDiscoveryUrl() const;
    void setFeedDiscoveryUrl(QString value);
    QString defaultFeedDiscoveryUrl() const;
    QString signInUserAgent() const;
    void setSignInUserAgent(QString value);

    QObject* authObject() const;
    bool feedAvailable() const;
    bool signedIn() const;
    QString accountName() const;
    bool authBusy() const;
    bool feedBusy() const;
    QString feedStatus() const;
    ResourceListModel* resourcesModel() const;
    QDateTime feedRefreshedAt() const;

    // fileUrl typically comes from a QML FileDialog (selectedFile). Returns the new
    // connection's id, or an empty string on failure.
    Q_INVOKABLE QString importConnection(const QUrl& fileUrl);
    Q_INVOKABLE void removeConnection(const QString& id);
    // Looks up the stored rdpText for id, updates "last used" and emits
    // openSessionRequested. No-op (no emit) if id does not exist.
    Q_INVOKABLE void openConnection(const QString& id);

    // The window the sign-in web view is overlaid on (the main ApplicationWindow).
    // Without it the view opens as a separate top-level window.
    Q_INVOKABLE void setHostWindow(QObject* window);
    // Interactive Entra sign-in in the web view, then a feed refresh. No-op while a
    // sign-in is already running.
    Q_INVOKABLE void signIn();
    // Forgets the refresh token, the cached feed and the web view's cookies.
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void refreshFeed();
    // Downloads the resource's .rdp file (falling back to the last downloaded copy if the
    // feed is unreachable) and emits openSessionRequested with "feed:<id>" as the
    // connection id, the keychain key for its RDSTLS credentials.
    Q_INVOKABLE void openResource(const QString& resourceId);

signals:
    // connectionId is passed on to RdpItem::connectToSession as the key for
    // CredentialStore lookups.
    void openSessionRequested(const QString& rdpText, const QString& connectionId);
    void useDisplayScaleFactorChanged();
    void hardwareVideoDecodingChanged();
    void preferredVaapiDeviceChanged();
    void frameBufferMsChanged();
    void preventSessionLockSecondsChanged();
    void feedDiscoveryUrlChanged();
    void signInUserAgentChanged();
    void accountChanged();
    void authBusyChanged();
    void feedBusyChanged();
    void feedStatusChanged();
    void feedChanged();

private:
    // Queues `job` on the worker thread (jobs run one at a time, in order).
    void runOnWorker(std::function<void()> job);
    // Posts `fn` back to the GUI thread; it is dropped if `generation` is no longer the
    // current job generation (a sign-out happened meanwhile).
    void runOnGui(quint64 generation, std::function<void()> fn);
    void setAuthBusy(bool busy);
    void setFeedBusy(bool busy);
    void setFeedStatus(const QString& status);
    void setAccount(bool signedIn, const QString& name);
    void startupSignIn();
    void fetchMissingIcons();
    void applyFeedSettings();
    void publishCache(); // cache -> model, feedChanged
    QUrl effectiveDiscoveryUrl() const;

    std::unique_ptr<ConnectionStore> m_store;
    std::unique_ptr<ConnectionListModel> m_model;
    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<AuthManager> m_auth;
    std::unique_ptr<FeedCache> m_feedCache;
    std::unique_ptr<ResourceListModel> m_resources;
#ifdef VINDAUGA_WITH_RDP
    std::unique_ptr<AadWebAuthenticator> m_webAuth;
#endif

    QThread m_workerThread;
    QObject* m_worker = nullptr; // lives on m_workerThread; the context for runOnWorker
    quint64 m_jobGeneration = 0;

    bool m_signedIn = false;
    QString m_accountName;
    bool m_authBusy = false;
    bool m_feedBusy = false;
    QString m_feedStatus;
};

} // namespace vindauga
