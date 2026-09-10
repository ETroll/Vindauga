#include "ConnectionListModel.h"

namespace vindauga {

ConnectionListModel::ConnectionListModel(ConnectionStore* store, QObject* parent)
    : QAbstractListModel(parent), m_store(store) {
    connect(m_store, &ConnectionStore::connectionsChanged, this, &ConnectionListModel::refresh);
    m_cache = m_store->connections();
}

int ConnectionListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_cache.size());
}

QVariant ConnectionListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_cache.size())
        return {};

    const SavedConnection& conn = m_cache.at(index.row());
    switch (role) {
    case IdRole:
        return conn.id;
    case DisplayNameRole:
        return conn.displayName;
    case LastUsedAtRole:
        return conn.lastUsedAt;
    default:
        return {};
    }
}

QHash<int, QByteArray> ConnectionListModel::roleNames() const {
    return {
        { IdRole, "id" },
        { DisplayNameRole, "displayName" },
        { LastUsedAtRole, "lastUsedAt" },
    };
}

void ConnectionListModel::refresh() {
    beginResetModel();
    m_cache = m_store->connections();
    endResetModel();
}

} // namespace vindauga
