#include "AppController.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QQuickWindow>

#include "Feed/FeedClient.h"
#include "Logging.h"

#ifdef VINDAUGA_WITH_RDP
#include "Rdp/VaapiDeviceSelector.h"
#endif

namespace vindauga {

namespace {

// Called at startup and whenever either VA-API setting changes. Without
// VINDAUGA_WITH_RDP there is no FreeRDP and no VaapiDeviceSelector, so this is a no-op.
void applyVaapiSettings(const AppSettings& settings) {
#ifdef VINDAUGA_WITH_RDP
    applyVaapiEnvironment(settings.hardwareVideoDecoding(), settings.preferredVaapiDevice());
#else
    Q_UNUSED(settings);
#endif
}

// Connection id for a feed resource: the keychain key under which its RDSTLS
// credentials are stored (see RdpItem::connectToSession). Resource ids are stable
// across refreshes, so the credentials survive a re-enumeration.
QString feedConnectionId(const QString& resourceId) {
    return QStringLiteral("feed:") + resourceId;
}

} // namespace

AppController::AppController(QObject* parent)
    : QObject(parent),
      m_store(std::make_unique<ConnectionStore>()),
      m_model(std::make_unique<ConnectionListModel>(m_store.get())),
      m_settings(std::make_unique<AppSettings>()),
      m_auth(std::make_unique<AuthManager>()),
      m_feedCache(std::make_unique<FeedCache>()),
      m_resources(std::make_unique<ResourceListModel>()) {
#ifdef VINDAUGA_WITH_RDP
    m_webAuth = std::make_unique<AadWebAuthenticator>();
#endif
    connect(m_settings.get(), &AppSettings::useDisplayScaleFactorChanged, this,
            &AppController::useDisplayScaleFactorChanged);
    connect(m_settings.get(), &AppSettings::frameBufferMsChanged, this,
            &AppController::frameBufferMsChanged);
    connect(m_settings.get(), &AppSettings::preventSessionLockSecondsChanged, this,
            &AppController::preventSessionLockSecondsChanged);
    connect(m_settings.get(), &AppSettings::feedDiscoveryUrlChanged, this,
            &AppController::feedDiscoveryUrlChanged);
    connect(m_settings.get(), &AppSettings::signInUserAgentChanged, this, [this]() {
        applyFeedSettings();
        emit signInUserAgentChanged();
    });
    // Whenever either setting changes, re-apply both: applyVaapiEnvironment takes both
    // values, and re-applying unchanged environment variables is harmless.
    connect(m_settings.get(), &AppSettings::hardwareVideoDecodingChanged, this, [this]() {
        applyVaapiSettings(*m_settings);
        emit hardwareVideoDecodingChanged();
    });
    connect(m_settings.get(), &AppSettings::preferredVaapiDeviceChanged, this, [this]() {
        applyVaapiSettings(*m_settings);
        emit preferredVaapiDeviceChanged();
    });
    // Apply at startup as well, so the stored choice (or the auto heuristic when nothing
    // is stored) is in effect for the first connection, not only after a settings change.
    applyVaapiSettings(*m_settings);
    applyFeedSettings();

    m_worker = new QObject;
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.setObjectName(QStringLiteral("vindauga-feed"));
    m_workerThread.start();

    // The cached list is on screen immediately; the feed is refreshed behind it.
    publishCache();
    startupSignIn();
}

AppController::~AppController() {
    ++m_jobGeneration; // drop any result still queued for the GUI thread
#ifdef VINDAUGA_WITH_RDP
    // Ends an open sign-in page and makes any capture that is still on its way to this
    // thread return empty at once (see AadWebAuthenticator::shutdown).
    m_webAuth->shutdown();
#endif
    m_workerThread.quit();
    // The worker may be inside a job that is blocked on a Qt::BlockingQueuedConnection
    // hop to this thread (the sign-in capture). A plain wait() would then deadlock: the
    // hop can only complete if this thread keeps delivering events. So keep serving them
    // while waiting; the hop returns immediately after shutdown(), and queued job results
    // are dropped by the generation bump above.
    while (!m_workerThread.wait(50))
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    delete m_worker;
    qCDebug(lcFeed) << "AppController shut down (worker thread joined)";
}

QString AppController::version() const {
#ifdef VINDAUGA_VERSION
    return QStringLiteral(VINDAUGA_VERSION);
#else
    return QStringLiteral("unknown");
#endif
}

ConnectionListModel* AppController::connectionsModel() const {
    return m_model.get();
}

bool AppController::useDisplayScaleFactor() const {
    return m_settings->useDisplayScaleFactor();
}

void AppController::setUseDisplayScaleFactor(bool value) {
    m_settings->setUseDisplayScaleFactor(value);
}

bool AppController::hardwareVideoDecoding() const {
    return m_settings->hardwareVideoDecoding();
}

void AppController::setHardwareVideoDecoding(bool value) {
    m_settings->setHardwareVideoDecoding(value);
}

QString AppController::preferredVaapiDevice() const {
    return m_settings->preferredVaapiDevice();
}

void AppController::setPreferredVaapiDevice(QString value) {
    m_settings->setPreferredVaapiDevice(std::move(value));
}

QVariantList AppController::availableGpuDevices() const {
    QVariantList result;
#ifdef VINDAUGA_WITH_RDP
    for (const VaapiDeviceInfo& device : listVaapiDevices()) {
        QVariantMap entry;
        entry[QStringLiteral("label")] = device.label;
        entry[QStringLiteral("path")] = device.path;
        result.append(entry);
    }
#endif
    return result;
}

int AppController::frameBufferMs() const {
    return m_settings->frameBufferMs();
}

void AppController::setFrameBufferMs(int value) {
    m_settings->setFrameBufferMs(value);
}

int AppController::preventSessionLockSeconds() const {
    return m_settings->preventSessionLockSeconds();
}

void AppController::setPreventSessionLockSeconds(int value) {
    m_settings->setPreventSessionLockSeconds(value);
}

QString AppController::feedDiscoveryUrl() const {
    return m_settings->feedDiscoveryUrl();
}

void AppController::setFeedDiscoveryUrl(QString value) {
    m_settings->setFeedDiscoveryUrl(std::move(value));
}

QString AppController::defaultFeedDiscoveryUrl() const {
    return FeedClient::defaultDiscoveryUrl().toString();
}

QString AppController::signInUserAgent() const {
    return m_settings->signInUserAgent();
}

void AppController::setSignInUserAgent(QString value) {
    m_settings->setSignInUserAgent(std::move(value));
}

QObject* AppController::authObject() const {
    return m_auth.get();
}

bool AppController::feedAvailable() const {
#ifdef VINDAUGA_WITH_RDP
    return true;
#else
    return false;
#endif
}

bool AppController::signedIn() const {
    return m_signedIn;
}

QString AppController::accountName() const {
    return m_accountName;
}

bool AppController::authBusy() const {
    return m_authBusy;
}

bool AppController::feedBusy() const {
    return m_feedBusy;
}

QString AppController::feedStatus() const {
    return m_feedStatus;
}

ResourceListModel* AppController::resourcesModel() const {
    return m_resources.get();
}

QDateTime AppController::feedRefreshedAt() const {
    return m_feedCache->refreshedAt();
}

QString AppController::importConnection(const QUrl& fileUrl) {
    return m_store->importFromFile(fileUrl.toLocalFile());
}

void AppController::removeConnection(const QString& id) {
    m_store->remove(id);
}

void AppController::openConnection(const QString& id) {
    const SavedConnection conn = m_store->connectionById(id);
    if (conn.id.isEmpty())
        return;
    m_store->touchLastUsed(id);
    emit openSessionRequested(conn.rdpText, conn.id);
}

void AppController::setHostWindow(QObject* window) {
#ifdef VINDAUGA_WITH_RDP
    m_webAuth->setHostWindow(qobject_cast<QQuickWindow*>(window));
#else
    Q_UNUSED(window);
#endif
}

// ---- worker plumbing --------------------------------------------------------------

void AppController::runOnWorker(std::function<void()> job) {
    QMetaObject::invokeMethod(m_worker, std::move(job), Qt::QueuedConnection);
}

void AppController::runOnGui(quint64 generation, std::function<void()> fn) {
    QMetaObject::invokeMethod(
        this,
        [this, generation, fn = std::move(fn)]() {
            if (generation != m_jobGeneration) {
                qCDebug(lcFeed) << "Dropping a stale feed job result";
                return;
            }
            fn();
        },
        Qt::QueuedConnection);
}

void AppController::setAuthBusy(bool busy) {
    if (m_authBusy == busy)
        return;
    m_authBusy = busy;
    emit authBusyChanged();
}

void AppController::setFeedBusy(bool busy) {
    if (m_feedBusy == busy)
        return;
    m_feedBusy = busy;
    emit feedBusyChanged();
}

void AppController::setFeedStatus(const QString& status) {
    if (m_feedStatus == status)
        return;
    m_feedStatus = status;
    emit feedStatusChanged();
}

void AppController::setAccount(bool signedIn, const QString& name) {
    if (m_signedIn == signedIn && m_accountName == name)
        return;
    m_signedIn = signedIn;
    m_accountName = name;
    emit accountChanged();
}

void AppController::applyFeedSettings() {
    const QString userAgent = m_settings->signInUserAgent();
    m_auth->setUserAgent(userAgent);
#ifdef VINDAUGA_WITH_RDP
    AadWebAuthenticator::setSignInUserAgent(userAgent);
#endif
}

QUrl AppController::effectiveDiscoveryUrl() const {
    const QString configured = m_settings->feedDiscoveryUrl();
    return configured.isEmpty() ? FeedClient::defaultDiscoveryUrl() : QUrl(configured);
}

void AppController::publishCache() {
    m_resources->setItems(m_feedCache->resources());
    emit feedChanged();
}

// ---- account ------------------------------------------------------------------------

void AppController::startupSignIn() {
    if (!feedAvailable())
        return;
    setAuthBusy(true);
    const quint64 generation = m_jobGeneration;
    runOnWorker([this, generation]() {
        if (!m_auth->hasStoredRefreshToken()) {
            runOnGui(generation, [this]() { setAuthBusy(false); });
            return;
        }
        QString error;
        const bool ok = m_auth->signInSilently(&error);
        const QString upn = m_auth->userPrincipalName();
        runOnGui(generation, [this, ok, error, upn]() {
            setAuthBusy(false);
            if (ok) {
                setAccount(true, upn);
                setFeedStatus(QString());
                refreshFeed();
            } else {
                // The list from last time stays on screen; the status explains why it is
                // not being refreshed and the sign-in button is back.
                setAccount(false, QString());
                setFeedStatus(AuthManager::explainAuthError(error));
            }
        });
    });
}

void AppController::signIn() {
#ifdef VINDAUGA_WITH_RDP
    if (m_authBusy)
        return;
    setAuthBusy(true);
    setFeedStatus(QString());
    const quint64 generation = m_jobGeneration;
    runOnWorker([this, generation]() {
        QString error;
        const bool ok = m_auth->signInInteractive(*m_webAuth, &error);
        const QString upn = m_auth->userPrincipalName();
        runOnGui(generation, [this, ok, error, upn]() {
            setAuthBusy(false);
            if (ok) {
                setAccount(true, upn);
                refreshFeed();
            } else {
                setFeedStatus(error.contains(QLatin1String("cancelled"))
                                  ? tr("Sign-in was cancelled.")
                                  : AuthManager::explainAuthError(error));
            }
        });
    });
#else
    setFeedStatus(tr("Sign-in is not available in this build (no RDP support)."));
#endif
}

void AppController::signOut() {
    ++m_jobGeneration; // results of jobs started before this point are ignored
    setAuthBusy(false);
    setFeedBusy(false);
    m_auth->signOut();
    m_feedCache->clear();
#ifdef VINDAUGA_WITH_RDP
    AadWebAuthenticator::clearSignInState();
#endif
    setAccount(false, QString());
    setFeedStatus(QString());
    publishCache();
}

// ---- feed -----------------------------------------------------------------------------

void AppController::refreshFeed() {
    if (!m_signedIn || m_feedBusy)
        return;
    setFeedBusy(true);
    const quint64 generation = m_jobGeneration;
    const QUrl discoveryUrl = effectiveDiscoveryUrl();
    runOnWorker([this, generation, discoveryUrl]() {
        const QString token = m_auth->feedAccessToken();
        if (token.isEmpty()) {
            qCWarning(lcFeed) << "Feed refresh: no access token (silent sign-in failed)";
            runOnGui(generation, [this]() {
                setFeedBusy(false);
                setFeedStatus(tr("Could not refresh the list: the saved sign-in is no longer "
                                 "valid. Sign in again."));
                setAccount(false, QString());
            });
            return;
        }
        const FeedClient client(discoveryUrl);
        const FeedClient::Enumeration result = client.enumerate(token);
        runOnGui(generation, [this, result]() {
            setFeedBusy(false);
            if (!result.ok) {
                setFeedStatus(tr("Could not refresh the list (showing the last known one): %1")
                                  .arg(result.error));
                return;
            }
            setFeedStatus(QString());
            m_feedCache->update(result.workspaces, result.resources);
            publishCache();
            fetchMissingIcons();
        });
    });
}

void AppController::fetchMissingIcons() {
    const QList<RdpResource> missing = m_feedCache->resourcesMissingIcons();
    if (missing.isEmpty())
        return;
    const quint64 generation = m_jobGeneration;
    runOnWorker([this, generation, missing]() {
        const QString token = m_auth->feedAccessToken();
        if (token.isEmpty())
            return;
        const FeedClient client;
        for (const RdpResource& resource : missing) {
            QString error;
            const QByteArray png = client.download(resource.iconPngUrl, token, &error);
            if (png.isEmpty()) {
                qCDebug(lcFeed) << "Icon for" << resource.title << "not fetched:" << error;
                continue;
            }
            const QString id = resource.id;
            runOnGui(generation, [this, id, png]() {
                m_feedCache->storeIcon(id, png);
                publishCache();
            });
        }
    });
}

void AppController::openResource(const QString& resourceId) {
    const FeedCache::CachedResource cached = m_feedCache->resourceById(resourceId);
    if (cached.resource.id.isEmpty()) {
        qCWarning(lcFeed) << "openResource: unknown resource id";
        return;
    }
    if (cached.resource.rdpFileUrl.isEmpty()) {
        setFeedStatus(tr("This resource has no connection file in the feed."));
        return;
    }
    setFeedStatus(QString());
    const quint64 generation = m_jobGeneration;
    const QUrl rdpUrl = cached.resource.rdpFileUrl;
    const QString fallback = cached.rdpText;
    runOnWorker([this, generation, resourceId, rdpUrl, fallback]() {
        QString error;
        QString rdpText;
        const QString token = m_auth->feedAccessToken();
        if (token.isEmpty()) {
            error = tr("the saved sign-in is no longer valid");
        } else {
            const FeedClient client;
            rdpText = QString::fromUtf8(client.download(rdpUrl, token, &error));
        }
        runOnGui(generation, [this, resourceId, rdpText, fallback, error]() {
            if (!rdpText.isEmpty()) {
                m_feedCache->storeRdpText(resourceId, rdpText);
                emit openSessionRequested(rdpText, feedConnectionId(resourceId));
                return;
            }
            if (!fallback.isEmpty()) {
                // The gateway address and session-host routing in the .rdp file are
                // stable, so the previous copy is a reasonable stand-in when the feed is
                // unreachable; the connection itself still needs a token.
                qCWarning(lcFeed) << "Using the cached .rdp file for" << resourceId << ":"
                                  << error;
                emit openSessionRequested(fallback, feedConnectionId(resourceId));
                return;
            }
            setFeedStatus(tr("Could not download the connection file: %1").arg(error));
        });
    });
}

} // namespace vindauga
