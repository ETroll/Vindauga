#include "UpdateDownloader.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "Logging.h"

namespace vindauga {

UpdateDownloader::UpdateDownloader(QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)) {}

UpdateDownloader::~UpdateDownloader() {
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    if (m_file.isOpen()) {
        m_file.close();
        m_file.remove(); // partial file
    }
}

void UpdateDownloader::start(const QUrl& url, const QString& destPath,
                             const QString& expectedSha256, qint64 expectedSize) {
    if (running())
        return;
    m_file.setFileName(destPath);
    // 0600: the file is only ever read back by us (and, after being copied, by root).
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit finished(Outcome::Failed, tr("Cannot write %1: %2").arg(destPath, m_file.errorString()));
        return;
    }
    m_file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_hash.reset();
    m_expectedSha256 = expectedSha256.toLower();
    m_expectedSize = expectedSize;
    m_received = 0;
    m_cancelled = false;
    m_writeError.clear();

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Vindauga/%1").arg(QCoreApplication::applicationVersion()));
    // GitHub redirects release assets to its CDN. Never follow a redirect to plain http.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    // Inactivity timeout, not a total one: a slow connection may need minutes for 120 MB.
    req.setTransferTimeout(30000);
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, &UpdateDownloader::onReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &UpdateDownloader::progress);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateDownloader::onReplyFinished);
}

void UpdateDownloader::cancel() {
    if (!m_reply)
        return;
    m_cancelled = true;
    m_reply->abort(); // emits finished()
}

void UpdateDownloader::onReadyRead() {
    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty() || !m_writeError.isEmpty())
        return;
    m_received += chunk.size();
    // Stop early on a response bigger than announced (also bounds disk use).
    if (m_expectedSize > 0 && m_received > m_expectedSize) {
        m_writeError = tr("The download is larger than expected");
        m_reply->abort();
        return;
    }
    if (m_file.write(chunk) != chunk.size()) {
        m_writeError = tr("Cannot write the update to disk: %1").arg(m_file.errorString());
        m_reply->abort();
        return;
    }
    m_hash.addData(chunk);
}

void UpdateDownloader::onReplyFinished() {
    QNetworkReply* reply = m_reply;
    m_reply.clear();
    reply->deleteLater();
    // Whatever is left in the buffer (readyRead may not have fired for the tail).
    if (m_writeError.isEmpty() && !m_cancelled && reply->error() == QNetworkReply::NoError) {
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty()) {
            m_received += tail.size();
            if (m_file.write(tail) != tail.size())
                m_writeError = tr("Cannot write the update to disk: %1").arg(m_file.errorString());
            else
                m_hash.addData(tail);
        }
    }

    if (m_cancelled) {
        finish(Outcome::Cancelled, QString());
    } else if (!m_writeError.isEmpty()) {
        finish(Outcome::Failed, m_writeError);
    } else if (reply->error() != QNetworkReply::NoError) {
        finish(Outcome::Failed, tr("Download failed: %1").arg(reply->errorString()));
    } else if (m_expectedSize > 0 && m_received != m_expectedSize) {
        finish(Outcome::Failed, tr("The download is incomplete"));
    } else if (QString::fromLatin1(m_hash.result().toHex()) != m_expectedSha256) {
        qCWarning(lcUpdate) << "Checksum mismatch for the downloaded update";
        finish(Outcome::Failed, tr("The downloaded file does not match its checksum"));
    } else {
        finish(Outcome::Ok, QString());
    }
}

void UpdateDownloader::finish(Outcome outcome, const QString& error) {
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
    if (outcome != Outcome::Ok)
        m_file.remove();
    emit finished(outcome, error);
}

} // namespace vindauga
