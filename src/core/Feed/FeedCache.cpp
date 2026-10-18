#include "FeedCache.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include "../Logging.h"

namespace vindauga {

namespace {

constexpr auto kFeedFileName = "feed.json";
constexpr auto kIconDirName = "icons";
constexpr int kFormatVersion = 1;

// Resource IDs come from the feed ("<workspace>:<app>" style strings); keep file names
// to a safe character set.
QString safeFileName(const QString& id) {
    QString out;
    out.reserve(id.size());
    for (const QChar c : id)
        out.append(c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_') ? c
                                                                                         : QLatin1Char('_'));
    return out;
}

QJsonObject workspaceToJson(const Workspace& w) {
    QJsonObject o;
    o[QStringLiteral("tenantId")] = w.tenantId;
    o[QStringLiteral("displayName")] = w.displayName;
    o[QStringLiteral("geo")] = w.geo;
    o[QStringLiteral("feedUrl")] = w.feedUrl.toString();
    o[QStringLiteral("conditionalAccessClaims")] = w.conditionalAccessClaims;
    return o;
}

Workspace workspaceFromJson(const QJsonObject& o) {
    Workspace w;
    w.tenantId = o.value(QStringLiteral("tenantId")).toString();
    w.displayName = o.value(QStringLiteral("displayName")).toString();
    w.geo = o.value(QStringLiteral("geo")).toString();
    w.feedUrl = QUrl(o.value(QStringLiteral("feedUrl")).toString());
    w.conditionalAccessClaims = o.value(QStringLiteral("conditionalAccessClaims")).toString();
    return w;
}

QJsonObject resourceToJson(const FeedCache::CachedResource& c) {
    const RdpResource& r = c.resource;
    QJsonObject o;
    o[QStringLiteral("id")] = r.id;
    o[QStringLiteral("title")] = r.title;
    o[QStringLiteral("type")] = r.type;
    o[QStringLiteral("deviceState")] = r.deviceState;
    o[QStringLiteral("rdpFileUrl")] = r.rdpFileUrl.toString();
    o[QStringLiteral("iconUrl")] = r.iconUrl.toString();
    o[QStringLiteral("iconPngUrl")] = r.iconPngUrl.toString();
    o[QStringLiteral("workspaceTenantId")] = r.workspaceTenantId;
    o[QStringLiteral("workspaceName")] = c.workspaceName;
    o[QStringLiteral("iconFile")] = c.iconFile;
    o[QStringLiteral("rdpText")] = c.rdpText;
    return o;
}

FeedCache::CachedResource resourceFromJson(const QJsonObject& o) {
    FeedCache::CachedResource c;
    RdpResource& r = c.resource;
    r.id = o.value(QStringLiteral("id")).toString();
    r.title = o.value(QStringLiteral("title")).toString();
    r.type = o.value(QStringLiteral("type")).toString();
    r.deviceState = o.value(QStringLiteral("deviceState")).toString();
    r.rdpFileUrl = QUrl(o.value(QStringLiteral("rdpFileUrl")).toString());
    r.iconUrl = QUrl(o.value(QStringLiteral("iconUrl")).toString());
    r.iconPngUrl = QUrl(o.value(QStringLiteral("iconPngUrl")).toString());
    r.workspaceTenantId = o.value(QStringLiteral("workspaceTenantId")).toString();
    c.workspaceName = o.value(QStringLiteral("workspaceName")).toString();
    c.iconFile = o.value(QStringLiteral("iconFile")).toString();
    c.rdpText = o.value(QStringLiteral("rdpText")).toString();
    return c;
}

} // namespace

FeedCache::FeedCache(QString cacheDir)
    : m_dir(cacheDir.isEmpty()
                ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                      QStringLiteral("/vindauga")
                : std::move(cacheDir)) {
    load();
}

QString FeedCache::feedPath() const {
    return m_dir + QLatin1Char('/') + QLatin1String(kFeedFileName);
}

QString FeedCache::iconPath(const QString& resourceId) const {
    return m_dir + QLatin1Char('/') + QLatin1String(kIconDirName) + QLatin1Char('/') +
           safeFileName(resourceId) + QStringLiteral(".png");
}

void FeedCache::load() {
    QFile file(feedPath());
    if (!file.open(QIODevice::ReadOnly)) {
        qCDebug(lcFeed) << "No feed cache at" << feedPath();
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcFeed) << "Ignoring unparsable feed cache" << feedPath() << parseError.errorString();
        return;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt() != kFormatVersion) {
        qCWarning(lcFeed) << "Ignoring feed cache with unknown version";
        return;
    }
    m_refreshedAt = QDateTime::fromString(root.value(QStringLiteral("refreshedAt")).toString(),
                                          Qt::ISODate);
    for (const QJsonValue& v : root.value(QStringLiteral("workspaces")).toArray())
        m_workspaces.append(workspaceFromJson(v.toObject()));
    for (const QJsonValue& v : root.value(QStringLiteral("resources")).toArray()) {
        CachedResource c = resourceFromJson(v.toObject());
        if (c.resource.id.isEmpty())
            continue;
        // The icon file may have been cleaned away by the user; treat it as not fetched.
        if (!c.iconFile.isEmpty() && !QFile::exists(c.iconFile))
            c.iconFile.clear();
        m_resources.append(c);
    }
    m_valid = true;
    qCDebug(lcFeed) << "Loaded feed cache:" << m_workspaces.size() << "workspace(s),"
                    << m_resources.size() << "resource(s), refreshed" << m_refreshedAt;
}

void FeedCache::save() const {
    QDir().mkpath(m_dir);
    QJsonObject root;
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("refreshedAt")] = m_refreshedAt.toString(Qt::ISODate);
    QJsonArray workspaces;
    for (const Workspace& w : m_workspaces)
        workspaces.append(workspaceToJson(w));
    root[QStringLiteral("workspaces")] = workspaces;
    QJsonArray resources;
    for (const CachedResource& c : m_resources)
        resources.append(resourceToJson(c));
    root[QStringLiteral("resources")] = resources;

    // QSaveFile: the previous cache survives a crash or a full disk mid-write.
    QSaveFile file(feedPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcFeed) << "Failed to write" << feedPath();
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        qCWarning(lcFeed) << "Failed to commit" << feedPath();
}

FeedCache::CachedResource FeedCache::resourceById(const QString& id) const {
    for (const CachedResource& c : m_resources) {
        if (c.resource.id == id)
            return c;
    }
    return {};
}

void FeedCache::update(const QList<Workspace>& workspaces, const QList<RdpResource>& resources) {
    QList<CachedResource> previous = m_resources;
    m_workspaces = workspaces;
    m_resources.clear();
    for (const RdpResource& r : resources) {
        CachedResource c;
        c.resource = r;
        for (const Workspace& w : workspaces) {
            if (w.tenantId == r.workspaceTenantId) {
                c.workspaceName = w.displayName;
                break;
            }
        }
        for (qsizetype i = 0; i < previous.size(); ++i) {
            if (previous.at(i).resource.id != r.id)
                continue;
            // Keep the icon unless the feed now points at a different image, in which
            // case the stale file goes (clear() only knows about referenced files).
            if (previous.at(i).resource.iconPngUrl == r.iconPngUrl)
                c.iconFile = previous.at(i).iconFile;
            else if (!previous.at(i).iconFile.isEmpty())
                QFile::remove(previous.at(i).iconFile);
            c.rdpText = previous.at(i).rdpText;
            previous.removeAt(i);
            break;
        }
        m_resources.append(c);
    }
    // Whatever is left in `previous` vanished from the feed: drop its icon file.
    for (const CachedResource& gone : previous) {
        if (!gone.iconFile.isEmpty())
            QFile::remove(gone.iconFile);
    }
    m_refreshedAt = QDateTime::currentDateTimeUtc();
    m_valid = true;
    save();
}

QList<RdpResource> FeedCache::resourcesMissingIcons() const {
    QList<RdpResource> missing;
    for (const CachedResource& c : m_resources) {
        if (!c.resource.iconPngUrl.isEmpty() && c.iconFile.isEmpty())
            missing.append(c.resource);
    }
    return missing;
}

void FeedCache::storeIcon(const QString& resourceId, const QByteArray& png) {
    if (png.isEmpty())
        return;
    for (CachedResource& c : m_resources) {
        if (c.resource.id != resourceId)
            continue;
        const QString path = iconPath(resourceId);
        QDir().mkpath(m_dir + QLatin1Char('/') + QLatin1String(kIconDirName));
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            file.write(png) != png.size() || !file.commit()) {
            qCWarning(lcFeed) << "Failed to write icon" << path;
            return;
        }
        c.iconFile = path;
        save();
        return;
    }
}

void FeedCache::storeRdpText(const QString& resourceId, const QString& rdpText) {
    for (CachedResource& c : m_resources) {
        if (c.resource.id != resourceId)
            continue;
        if (c.rdpText == rdpText)
            return;
        c.rdpText = rdpText;
        save();
        return;
    }
}

void FeedCache::clear() {
    for (const CachedResource& c : m_resources) {
        if (!c.iconFile.isEmpty())
            QFile::remove(c.iconFile);
    }
    m_workspaces.clear();
    m_resources.clear();
    m_refreshedAt = QDateTime();
    m_valid = false;
    QFile::remove(feedPath());
}

} // namespace vindauga
