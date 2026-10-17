#include "ResourceListModel.h"

#include <QUrl>

namespace vindauga {

ResourceListModel::ResourceListModel(QObject* parent) : QAbstractListModel(parent) {}

int ResourceListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_items.size());
}

QVariant ResourceListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};

    const FeedCache::CachedResource& item = m_items.at(index.row());
    switch (role) {
    case IdRole:
        return item.resource.id;
    case TitleRole:
        return item.resource.title;
    case TypeRole:
        return item.resource.type;
    case WorkspaceNameRole:
        return item.workspaceName;
    case IconSourceRole:
        return item.iconFile.isEmpty() ? QUrl() : QUrl::fromLocalFile(item.iconFile);
    case DeviceStateRole:
        return item.resource.deviceState;
    default:
        return {};
    }
}

QHash<int, QByteArray> ResourceListModel::roleNames() const {
    return {
        { IdRole, "id" },
        { TitleRole, "title" },
        { TypeRole, "type" },
        { WorkspaceNameRole, "workspaceName" },
        { IconSourceRole, "iconSource" },
        { DeviceStateRole, "deviceState" },
    };
}

void ResourceListModel::setItems(const QList<FeedCache::CachedResource>& items) {
    beginResetModel();
    m_items = items;
    endResetModel();
}

} // namespace vindauga
