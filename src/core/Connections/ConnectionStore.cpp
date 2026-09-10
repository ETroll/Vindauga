#include "ConnectionStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

#include "../Logging.h"

namespace vindauga {

namespace {

constexpr auto kStorageFileName = "connections.json";

// .rdp text is line-based "key:type:value". This only derives a display name (FreeRDP's
// own parser handles the actual connection); it is not a full .rdp parser. Keys are
// case-insensitive, like the rest of the format.
QString extractRemoteDesktopName(const QString& rdpText) {
    const QStringList lines = rdpText.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (!line.startsWith(QStringLiteral("remotedesktopname:"), Qt::CaseInsensitive))
            continue;
        const int lastColon = line.lastIndexOf(QLatin1Char(':'));
        if (lastColon < 0 || lastColon + 1 >= line.size())
            continue;
        const QString value = line.mid(lastColon + 1).trimmed();
        if (!value.isEmpty())
            return value;
    }
    return {};
}

} // namespace

ConnectionStore::ConnectionStore(QString storageDir, QObject* parent)
    : QObject(parent),
      m_storageDir(storageDir.isEmpty()
                       ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                             QStringLiteral("/vindauga")
                       : std::move(storageDir)) {
    load();
}

QString ConnectionStore::storagePath() const {
    return m_storageDir + QLatin1Char('/') + QLatin1String(kStorageFileName);
}

void ConnectionStore::load() {
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly)) {
        qCDebug(lcConnections) << "No saved connections found (first run?)";
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcConnections) << "Failed to parse" << storagePath()
                                 << "— starting with an empty list:" << parseError.errorString();
        return;
    }

    for (const QJsonValue& v : doc.array()) {
        const QJsonObject obj = v.toObject();
        SavedConnection conn;
        conn.id = obj.value(QStringLiteral("id")).toString();
        conn.displayName = obj.value(QStringLiteral("displayName")).toString();
        conn.rdpText = obj.value(QStringLiteral("rdpText")).toString();
        conn.addedAt = QDateTime::fromString(obj.value(QStringLiteral("addedAt")).toString(),
                                             Qt::ISODate);
        conn.lastUsedAt = QDateTime::fromString(
            obj.value(QStringLiteral("lastUsedAt")).toString(), Qt::ISODate);
        if (conn.id.isEmpty() || conn.rdpText.isEmpty()) {
            qCWarning(lcConnections) << "Ignoring invalid entry in" << storagePath();
            continue;
        }
        m_connections.append(conn);
    }
    qCDebug(lcConnections) << "Loaded" << m_connections.size() << "saved connections";
}

void ConnectionStore::save() const {
    QDir().mkpath(m_storageDir);
    QJsonArray arr;
    for (const SavedConnection& conn : m_connections) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = conn.id;
        obj[QStringLiteral("displayName")] = conn.displayName;
        obj[QStringLiteral("rdpText")] = conn.rdpText;
        obj[QStringLiteral("addedAt")] = conn.addedAt.toString(Qt::ISODate);
        obj[QStringLiteral("lastUsedAt")] = conn.lastUsedAt.toString(Qt::ISODate);
        arr.append(obj);
    }

    QFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcConnections) << "Failed to write" << storagePath();
        return;
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

QList<SavedConnection> ConnectionStore::connections() const {
    QList<SavedConnection> sorted = m_connections;
    std::sort(sorted.begin(), sorted.end(), [](const SavedConnection& a, const SavedConnection& b) {
        return a.lastUsedAt > b.lastUsedAt;
    });
    return sorted;
}

SavedConnection ConnectionStore::connectionById(const QString& id) const {
    for (const SavedConnection& conn : m_connections) {
        if (conn.id == id)
            return conn;
    }
    return {};
}

QString ConnectionStore::importFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcConnections) << "Failed to open" << filePath;
        return {};
    }
    const QString rdpText = QString::fromUtf8(file.readAll());
    if (rdpText.trimmed().isEmpty()) {
        qCWarning(lcConnections) << "Empty .rdp file:" << filePath;
        return {};
    }

    SavedConnection conn;
    conn.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    conn.displayName = extractRemoteDesktopName(rdpText);
    if (conn.displayName.isEmpty())
        conn.displayName = QFileInfo(filePath).completeBaseName();
    conn.rdpText = rdpText;
    conn.addedAt = QDateTime::currentDateTimeUtc();
    conn.lastUsedAt = conn.addedAt;

    m_connections.append(conn);
    save();
    emit connectionsChanged();
    qCDebug(lcConnections) << "Imported connection" << conn.displayName << "(length"
                           << rdpText.size() << "chars)";
    return conn.id;
}

void ConnectionStore::remove(const QString& id) {
    const qsizetype before = m_connections.size();
    m_connections.removeIf([&id](const SavedConnection& c) { return c.id == id; });
    if (m_connections.size() == before)
        return;
    save();
    emit connectionsChanged();
}

void ConnectionStore::touchLastUsed(const QString& id) {
    for (SavedConnection& conn : m_connections) {
        if (conn.id != id)
            continue;
        conn.lastUsedAt = QDateTime::currentDateTimeUtc();
        save();
        emit connectionsChanged();
        return;
    }
}

} // namespace vindauga
