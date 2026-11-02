#pragma once
#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Downloads one release package to a file and verifies its SHA-256 (and size, when
// known) before reporting success; a file that fails verification is deleted. Streams to
// disk as it arrives, since the .deb is ~120 MB.
namespace vindauga {

class UpdateDownloader : public QObject {
    Q_OBJECT
public:
    enum class Outcome { Ok, Failed, Cancelled };
    Q_ENUM(Outcome)

    explicit UpdateDownloader(QObject* parent = nullptr);
    ~UpdateDownloader() override;

    // destPath's directory must exist. expectedSize <= 0 skips the size check.
    // finished() is emitted exactly once per start(). No-op while running.
    void start(const QUrl& url, const QString& destPath, const QString& expectedSha256,
               qint64 expectedSize);
    void cancel();
    bool running() const { return !m_reply.isNull(); }

signals:
    void progress(qint64 received, qint64 total);
    void finished(vindauga::UpdateDownloader::Outcome outcome, const QString& error);

private:
    void onReadyRead();
    void onReplyFinished();
    void finish(Outcome outcome, const QString& error);

    QNetworkAccessManager* m_nam;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QString m_expectedSha256;
    qint64 m_expectedSize = 0;
    qint64 m_received = 0;
    bool m_cancelled = false;
    QString m_writeError;
};

} // namespace vindauga
