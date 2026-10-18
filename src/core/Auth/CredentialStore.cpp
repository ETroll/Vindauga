#include "CredentialStore.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QThread>
#include <functional>
#include <qt6keychain/keychain.h>
#include <utility>

#include "../Logging.h"

namespace vindauga {

namespace {

// QtKeychain has one process-wide job queue (QKeychain::JobExecutor) that is created on
// whichever thread starts the first job and from then on lives there. A job started on
// any other thread is connected to that queue across threads, so the queue's
// "job finished" bookkeeping runs as a queued call after the job object has already been
// destroyed (the jobs below live on the stack): a use-after-free in
// JobExecutor::jobFinished, seen as a segfault on the feed worker thread, or a queue
// stuck "busy" so every later keychain call hangs. The only safe arrangement is to run
// every job on one thread. The main thread is that thread; callers on other threads are
// hopped over with a blocking call and get the result back synchronously.
//
// Hazard to keep in mind: the main thread must be processing events while another thread
// waits here. Everything that joins a worker thread from the main thread must therefore
// either pump events while waiting (AppController's destructor does) or make sure the
// worker cannot be inside a keychain call (AuthManager persists from non-GUI threads
// asynchronously for that reason, see AuthManager::adoptTokens).
void runOnMainThread(const std::function<void()>& fn) {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app || QThread::currentThread() == app->thread()) {
        fn();
        return;
    }
    QMetaObject::invokeMethod(app, fn, Qt::BlockingQueuedConnection);
}

// Runs `job` to completion on the current (main) thread.
void runJob(QKeychain::Job& job) {
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
}

} // namespace

CredentialStore::CredentialStore(QString service) : m_service(std::move(service)) {}

CredentialStore::Credentials CredentialStore::load(const QString& key) const {
    Credentials result;
    if (key.isEmpty())
        return result;

    runOnMainThread([&]() {
        QKeychain::ReadPasswordJob job(m_service);
        job.setKey(key);
        runJob(job);

        if (job.error() != QKeychain::NoError) {
            if (job.error() != QKeychain::EntryNotFound)
                qCWarning(lcAuth) << "Failed to read from keychain:" << job.errorString();
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(job.textData().toUtf8());
        if (!doc.isObject())
            return;
        const QJsonObject obj = doc.object();
        result.username = obj.value(QStringLiteral("username")).toString();
        result.password = obj.value(QStringLiteral("password")).toString();
        result.found = !result.username.isEmpty() && !result.password.isEmpty();
    });
    return result;
}

void CredentialStore::save(const QString& key, const QString& username,
                           const QString& password) const {
    if (key.isEmpty())
        return;

    QJsonObject obj;
    obj[QStringLiteral("username")] = username;
    obj[QStringLiteral("password")] = password;
    const QString text = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));

    runOnMainThread([&]() {
        QKeychain::WritePasswordJob job(m_service);
        job.setKey(key);
        job.setTextData(text);
        runJob(job);

        if (job.error() != QKeychain::NoError)
            qCWarning(lcAuth) << "Failed to write to keychain:" << job.errorString();
        else
            qCDebug(lcAuth) << "Saved credentials to keychain (username length"
                            << username.size() << ")";
    });
}

QString CredentialStore::loadText(const QString& key) const {
    if (key.isEmpty())
        return {};

    QString text;
    runOnMainThread([&]() {
        QKeychain::ReadPasswordJob job(m_service);
        job.setKey(key);
        runJob(job);

        if (job.error() != QKeychain::NoError) {
            if (job.error() != QKeychain::EntryNotFound)
                qCWarning(lcAuth) << "Failed to read from keychain:" << job.errorString();
            return;
        }
        text = job.textData();
    });
    return text;
}

void CredentialStore::saveText(const QString& key, const QString& text) const {
    if (key.isEmpty())
        return;

    runOnMainThread([&]() {
        QKeychain::WritePasswordJob job(m_service);
        job.setKey(key);
        job.setTextData(text);
        runJob(job);

        if (job.error() != QKeychain::NoError)
            qCWarning(lcAuth) << "Failed to write to keychain:" << job.errorString();
        else
            qCDebug(lcAuth) << "Saved secret to keychain (length" << text.size() << ")";
    });
}

void CredentialStore::remove(const QString& key) const {
    if (key.isEmpty())
        return;

    runOnMainThread([&]() {
        QKeychain::DeletePasswordJob job(m_service);
        job.setKey(key);
        runJob(job);

        if (job.error() != QKeychain::NoError && job.error() != QKeychain::EntryNotFound)
            qCWarning(lcAuth) << "Failed to remove from keychain:" << job.errorString();
    });
}

} // namespace vindauga
