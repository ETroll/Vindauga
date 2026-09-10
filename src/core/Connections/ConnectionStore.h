#pragma once
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

// Locally saved connections imported from .rdp files. Pure core/, no Qt Quick/GUI
// dependency.
namespace vindauga {

struct SavedConnection {
    QString id;
    QString displayName;
    QString rdpText;
    QDateTime addedAt;
    QDateTime lastUsedAt;
};

class ConnectionStore : public QObject {
    Q_OBJECT
public:
    // storageDir: directory where connections.json is read/written. Empty (default) means
    // $XDG_CONFIG_HOME/vindauga/ (QStandardPaths::GenericConfigLocation). Exposed so tests
    // can point at a temporary directory.
    explicit ConnectionStore(QString storageDir = QString(), QObject* parent = nullptr);

    // Sorted by lastUsedAt, newest first.
    QList<SavedConnection> connections() const;
    SavedConnection connectionById(const QString& id) const; // empty object (id.isEmpty()) if not found

    // Reads the file at filePath, creates a new SavedConnection, persists it and returns
    // its id. Empty string on failure (unreadable or empty file). displayName is derived
    // from the "remotedesktopname:s:" field, falling back to the file's base name.
    QString importFromFile(const QString& filePath);
    void remove(const QString& id);
    void touchLastUsed(const QString& id); // updates lastUsedAt, persists, emits connectionsChanged

signals:
    void connectionsChanged();

private:
    QString m_storageDir;
    QList<SavedConnection> m_connections;

    QString storagePath() const;
    void load();
    void save() const;
};

} // namespace vindauga
