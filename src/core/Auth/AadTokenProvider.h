#pragma once
#include <QString>

// Abstract source of Entra ID access tokens that can be obtained without user
// interaction (typically a refresh-token grant, see AuthManager). RdpSession consults a
// registered provider from its GetCommonAccessToken callback before falling back to the
// interactive AadInteractiveAuth web view, so a connection whose sign-in is still valid
// never shows a browser.
namespace vindauga {

class AadTokenProvider {
public:
    virtual ~AadTokenProvider() = default;

    // scopeEncoded is the scope exactly as FreeRDP hands it to the callback: already
    // percent-encoded (e.g. "https%3A%2F%2Fwww.wvd.microsoft.com%2F.default%20openid"),
    // ready to be placed in a form body verbatim. reqCnf is the base64url-encoded
    // proof-of-possession key FreeRDP generates for the RDSTLS (session host) token, or
    // empty for the plain gateway token. Returns the bare access token, or an empty
    // string when the token could not be obtained silently (the caller then falls back
    // to the interactive flow).
    //
    // Called from RdpSession's worker thread; implementations must be thread-safe and
    // may block (the connection sequence is waiting on the result anyway).
    virtual QString acquireAccessToken(const QString& scopeEncoded, const QString& reqCnf) = 0;
};

} // namespace vindauga
