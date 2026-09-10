#include "CredentialStore.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <qt6keychain/keychain.h>
#include <utility>

#include "../Logging.h"

namespace vindauga {

CredentialStore::CredentialStore(QString service) : m_service(std::move(service)) {}

CredentialStore::Credentials CredentialStore::load(const QString& key) const {
    Credentials result;
    if (key.isEmpty())
        return result;

    QKeychain::ReadPasswordJob job(m_service);
    job.setKey(key);

    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();

    if (job.error() != QKeychain::NoError) {
        if (job.error() != QKeychain::EntryNotFound)
            qCWarning(lcAuth) << "Failed to read from keychain:" << job.errorString();
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(job.textData().toUtf8());
    if (!doc.isObject())
        return result;
    const QJsonObject obj = doc.object();
    result.username = obj.value(QStringLiteral("username")).toString();
    result.password = obj.value(QStringLiteral("password")).toString();
    result.found = !result.username.isEmpty() && !result.password.isEmpty();
    return result;
}

void CredentialStore::save(const QString& key, const QString& username,
                           const QString& password) const {
    if (key.isEmpty())
        return;

    QJsonObject obj;
    obj[QStringLiteral("username")] = username;
    obj[QStringLiteral("password")] = password;

    QKeychain::WritePasswordJob job(m_service);
    job.setKey(key);
    job.setTextData(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();

    if (job.error() != QKeychain::NoError)
        qCWarning(lcAuth) << "Failed to write to keychain:" << job.errorString();
    else
        qCDebug(lcAuth) << "Saved credentials to keychain (username length"
                        << username.size() << ")";
}

void CredentialStore::remove(const QString& key) const {
    if (key.isEmpty())
        return;

    QKeychain::DeletePasswordJob job(m_service);
    job.setKey(key);

    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();

    if (job.error() != QKeychain::NoError && job.error() != QKeychain::EntryNotFound)
        qCWarning(lcAuth) << "Failed to remove from keychain:" << job.errorString();
}

} // namespace vindauga
