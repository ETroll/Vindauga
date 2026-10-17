#include "FeedClient.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "../Logging.h"
#include "FeedParser.h"

namespace vindauga {

namespace {

constexpr auto kDiscoveryUrl = "https://rdweb.wvd.microsoft.com/api/arm/feeddiscovery";

// The feed service rejects clients it does not recognise ("INCOMPATIBLE_CLIENT_VERSION:
// Client did not send any User Agent approved header"). This is the X-MS-User-Agent
// Microsoft's web client sends (clientType/clientVersion sdkType/sdkVersion); it is also
// sent as the plain User-Agent. VINDAUGA_FEED_USER_AGENT overrides it without a rebuild
// should Microsoft retire this version string.
constexpr auto kDefaultUserAgent = "com.microsoft.rdc.html/2.0.79.2 rdhtml-sdk/2.0.4";

constexpr auto kAcceptDiscovery = "application/x-msts-radc-discovery+xml,text/xml";
constexpr auto kAcceptFeed = "application/x-msts-radc+xml;radc_schema_version=2.0,text/xml";

constexpr int kHttpTimeoutMs = 30000;

} // namespace

FeedClient::FeedClient(QUrl discoveryUrl, QByteArray userAgent)
    : m_discoveryUrl(std::move(discoveryUrl)), m_userAgent(std::move(userAgent)) {
    if (m_discoveryUrl.isEmpty())
        m_discoveryUrl = defaultDiscoveryUrl();
    if (m_userAgent.isEmpty())
        m_userAgent = defaultUserAgent();
}

QByteArray FeedClient::defaultUserAgent() {
    const QByteArray override = qgetenv("VINDAUGA_FEED_USER_AGENT");
    return override.isEmpty() ? QByteArray(kDefaultUserAgent) : override;
}

QUrl FeedClient::defaultDiscoveryUrl() {
    const QByteArray override = qgetenv("VINDAUGA_FEED_DISCOVERY_URL");
    return QUrl(override.isEmpty() ? QString::fromLatin1(kDiscoveryUrl) : QString::fromUtf8(override));
}

bool FeedClient::isTrustedUrl(const QUrl& url) {
    if (!url.isValid() || url.scheme() != QLatin1String("https"))
        return false;
    const QString host = url.host().toLower();
    // Public cloud, Azure Government (rdweb.wvd.azure.us, login.microsoftonline.us) and
    // Azure China (rdweb.wvd.azure.cn, login.chinacloudapi.cn).
    for (const auto* suffix : {".microsoft.com", ".microsoftonline.com", ".azure.us",
                               ".microsoftonline.us", ".azure.cn", ".chinacloudapi.cn"}) {
        const QString s = QString::fromLatin1(suffix);
        if (host == s.mid(1) || host.endsWith(s))
            return true;
    }
    return false;
}

QByteArray FeedClient::get(const QUrl& url, const QString& accessToken, const QByteArray& accept,
                           QString* error) const {
    if (!isTrustedUrl(url)) {
        if (error)
            *error = QStringLiteral("refusing to send the token to an untrusted URL (host %1)")
                         .arg(url.host());
        return {};
    }

    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    request.setRawHeader("Accept", accept);
    request.setRawHeader("User-Agent", m_userAgent);
    request.setRawHeader("X-MS-User-Agent", m_userAgent);
    request.setTransferTimeout(kHttpTimeoutMs);

    QEventLoop loop;
    QNetworkReply* reply = nam.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (status == 0) {
        if (error)
            *error = QStringLiteral("network error: %1").arg(reply->errorString());
        return {};
    }
    if (status != 200) {
        if (error)
            *error = QStringLiteral("HTTP %1: %2").arg(status).arg(QString::fromUtf8(body.left(300)));
        return {};
    }
    return body;
}

FeedClient::Enumeration FeedClient::enumerate(const QString& accessToken) const {
    Enumeration result;
    QString error;
    const QByteArray discovery = get(m_discoveryUrl, accessToken, kAcceptDiscovery, &error);
    if (discovery.isEmpty()) {
        result.error = QStringLiteral("feed discovery failed: %1").arg(error);
        qCWarning(lcFeed) << result.error;
        return result;
    }
    result.workspaces = FeedParser::parseDiscovery(discovery);
    qCDebug(lcFeed) << "Discovery returned" << result.workspaces.size() << "workspace(s)";

    for (const Workspace& workspace : result.workspaces) {
        const QByteArray feed = get(workspace.feedUrl, accessToken, kAcceptFeed, &error);
        if (feed.isEmpty()) {
            qCWarning(lcFeed) << "Workspace" << workspace.displayName << "feed failed:" << error;
            continue;
        }
        const QList<RdpResource> resources = FeedParser::parseWebFeed(feed, workspace.tenantId);
        qCDebug(lcFeed) << "Workspace" << workspace.displayName << "has" << resources.size()
                        << "resource(s)";
        result.resources.append(resources);
    }
    result.ok = true;
    return result;
}

QByteArray FeedClient::download(const QUrl& url, const QString& accessToken, QString* error) const {
    return get(url, accessToken, "*/*", error);
}

} // namespace vindauga
