#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include "../Models.h"

// Local copy of the last feed enumeration so the desktop list is on screen the moment
// the app starts, before (or without) a round trip to the feed service: workspaces,
// resources, each resource's 32x32 icon as a PNG file and the last .rdp text that was
// downloaded for it. The feed service is slow (two or more sequential HTTPS calls behind
// a token refresh) and occasionally unavailable; without this cache the list would be
// empty every time the app opens and would disappear whenever a refresh failed.
//
// Lives under $XDG_CACHE_HOME/vindauga/ (QStandardPaths::CacheLocation): feed.json plus
// icons/<id>.png. Pure core/, no Qt Quick dependency. Not thread-safe; the owner (the
// GUI thread in AppController) does all reads and writes, and worker threads only hand
// over fetched data.
namespace vindauga {

class FeedCache {
public:
    struct CachedResource {
        RdpResource resource;
        QString workspaceName; // Workspace::displayName for resource.workspaceTenantId
        QString iconFile;      // absolute path of the PNG, empty if never fetched
        QString rdpText;       // last downloaded .rdp content, empty if never connected
    };

    // cacheDir empty (default) means QStandardPaths::CacheLocation. Loads feed.json if
    // present; a missing or unparsable file simply means an empty cache.
    explicit FeedCache(QString cacheDir = QString());

    bool isValid() const { return m_valid; } // true once a feed has been stored or loaded
    QDateTime refreshedAt() const { return m_refreshedAt; }
    QList<Workspace> workspaces() const { return m_workspaces; }
    QList<CachedResource> resources() const { return m_resources; }
    CachedResource resourceById(const QString& id) const; // resource.id empty if unknown

    // Replaces the list with a fresh enumeration and persists it. Icon files and .rdp
    // text are kept for resources whose id is still present; those of vanished
    // resources are removed.
    void update(const QList<Workspace>& workspaces, const QList<RdpResource>& resources);

    // Which resources still need their icon fetched (have an iconPngUrl but no file).
    QList<RdpResource> resourcesMissingIcons() const;
    void storeIcon(const QString& resourceId, const QByteArray& png);
    void storeRdpText(const QString& resourceId, const QString& rdpText);

    void clear(); // forgets everything, including files on disk (sign-out)

    QString cacheDir() const { return m_dir; }

private:
    QString feedPath() const;
    QString iconPath(const QString& resourceId) const;
    void load();
    void save() const;

    QString m_dir;
    bool m_valid = false;
    QDateTime m_refreshedAt;
    QList<Workspace> m_workspaces;
    QList<CachedResource> m_resources;
};

} // namespace vindauga
