#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

#include "Connections/ConnectionStore.h"

namespace vindauga {

// List model over ConnectionStore for the connections ListView. Created and owned by
// AppController; QML_UNCREATABLE since instantiating it directly from QML makes no sense.
class ConnectionListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by AppController")
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        DisplayNameRole,
        LastUsedAtRole,
    };

    // The store is not owned by the model; the caller must keep it alive at least as
    // long as the model.
    explicit ConnectionListModel(ConnectionStore* store, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private slots:
    void refresh(); // connected to ConnectionStore::connectionsChanged

private:
    ConnectionStore* m_store;
    QList<SavedConnection> m_cache;
};

} // namespace vindauga
