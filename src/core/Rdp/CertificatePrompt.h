#pragma once
#include <QString>

// Abstract interface; no Qt Quick/GUI dependency in core/. The concrete Qt Quick
// implementation is ui/backend/CertificateTrustDialog.
namespace vindauga {

// Details about a server certificate that FreeRDP's own automatic verification could not
// accept outright: it first does a real X.509 chain check against the system trust store
// (OpenSSL, same CA bundle a browser uses) plus its own certs directory, and only when
// that fails does it fall back to a known_hosts-style local cache keyed by host:port. This
// struct describes the two cases that fall through both of those: the host has never been
// seen before (isChanged == false) or it has, but the fingerprint no longer matches what
// was previously accepted (isChanged == true). oldSubject/oldIssuer/oldFingerprint are
// only populated when isChanged is true.
struct CertificateInfo {
    QString host;
    quint16 port = 0;
    QString commonName;
    QString subject;
    QString issuer;
    QString fingerprint;
    bool isChanged = false;    // true: this replaces a previously trusted certificate
    bool hostMismatch = false; // certificate subject does not match the host connected to
    QString oldSubject;
    QString oldIssuer;
    QString oldFingerprint;
};

// Prompts the user to accept or reject a server certificate FreeRDP could not verify
// automatically. Returning without user confirmation must never accept the certificate:
// a Vindauga session talks to an Azure RDP gateway over the open internet, and blind
// acceptance here is exactly the man-in-the-middle exposure this interface exists to
// close.
class CertificatePrompt {
public:
    virtual ~CertificatePrompt() = default;

    enum class Decision {
        Reject,      // do not connect (FreeRDP return value 0)
        AcceptOnce,  // trust for this session only, do not persist (return value 2)
        AcceptAlways // trust and store in FreeRDP's certificate cache (return value 1)
    };

    // Called synchronously from RdpSession's worker thread. Implementations must hop to
    // the GUI thread themselves (e.g. QMetaObject::invokeMethod with
    // Qt::BlockingQueuedConnection) and block until the result is available.
    virtual Decision promptForCertificate(const CertificateInfo& info) = 0;
};

} // namespace vindauga
