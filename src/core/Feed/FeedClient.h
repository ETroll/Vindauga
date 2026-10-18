#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

#include "../Models.h"

// Feed discovery against the AVD/Windows 365 feed service with a bearer token from
// AuthManager: discovery -> one FeedURL per workspace -> resources (desktops, apps), and
// bearer downloads of the per-resource .rdp file and icons. Synchronous (nested
// QEventLoop around QNetworkAccessManager), usable from any thread with an event
// dispatcher. XML parsing is FeedParser's.
namespace vindauga {

class FeedClient {
public:
    struct Enumeration {
        bool ok = false;
        QString error;             // set when !ok; per-workspace failures are only logged
        QList<Workspace> workspaces;
        QList<RdpResource> resources; // across all workspaces, in feed order
    };

    // discoveryUrl defaults to the public cloud; sovereign clouds use another host.
    // userAgent is sent as both User-Agent and X-MS-User-Agent (see FeedClient.cpp); the
    // feed service rejects clients it does not recognise.
    explicit FeedClient(QUrl discoveryUrl = defaultDiscoveryUrl(),
                        QByteArray userAgent = defaultUserAgent());

    static QUrl defaultDiscoveryUrl();     // VINDAUGA_FEED_DISCOVERY_URL or the public cloud
    static QByteArray defaultUserAgent();  // VINDAUGA_FEED_USER_AGENT or the built-in string

    // A bearer token is only ever attached to https URLs on Microsoft hosts, public or
    // sovereign cloud (the feed XML supplies the feed, .rdp and icon URLs, so they are
    // checked first). Dot-boundary match: "evilmicrosoft.com" and "microsoft.com.evil.io"
    // are rejected.
    static bool isTrustedUrl(const QUrl& url);

    Enumeration enumerate(const QString& accessToken) const;

    // Bearer GET of url (the .rdp file or an icon). Empty with `error` set on failure.
    QByteArray download(const QUrl& url, const QString& accessToken, QString* error) const;

private:
    QByteArray get(const QUrl& url, const QString& accessToken, const QByteArray& accept,
                   QString* error) const;

    QUrl m_discoveryUrl;
    QByteArray m_userAgent;
};

} // namespace vindauga
