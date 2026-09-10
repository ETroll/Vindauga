#pragma once
#include <QString>

namespace vindauga {

// Secure storage of RDSTLS username/password per saved connection
// (ConnectionStore::SavedConnection::id) in the OS keychain via QtKeychain
// (libsecret/kwallet/gnome-keyring depending on the system).
//
// Never log the credentials themselves; log only lengths or presence.
class CredentialStore {
public:
    struct Credentials {
        QString username;
        QString password;
        bool found = false;
    };

    explicit CredentialStore(QString service = QStringLiteral("vindauga"));

    // Synchronous API (a nested QEventLoop waits for the QtKeychain job to finish). Safe
    // to call from the GUI thread. key = SavedConnection::id.
    Credentials load(const QString& key) const;
    void save(const QString& key, const QString& username, const QString& password) const;
    void remove(const QString& key) const;

private:
    QString m_service;
};

} // namespace vindauga
