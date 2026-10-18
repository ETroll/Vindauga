#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

#include "Feed/FeedCache.h"

namespace vindauga {

// List model over the feed's desktops and apps (from FeedCache) for the home page's
// grid. Created and owned by AppController, which replaces the contents with setItems()
// after every refresh; QML_UNCREATABLE since instantiating it from QML makes no sense.
class ResourceListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by AppController")
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        TypeRole,          // "Desktop" | "RemoteApp"
        WorkspaceNameRole,
        IconSourceRole,    // file:// URL of the cached PNG, or an empty URL
        DeviceStateRole,
    };

    explicit ResourceListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QList<FeedCache::CachedResource>& items);

private:
    QList<FeedCache::CachedResource> m_items;
};

} // namespace vindauga
