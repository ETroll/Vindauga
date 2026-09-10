#include "RdpSession.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <freerdp/channels/disp.h>
#include <freerdp/client.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/disp.h>
#include <freerdp/client/file.h>
#include <freerdp/codec/color.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/graphics.h>
#include <freerdp/input.h>
#include <freerdp/locale/keyboard.h>
#include <freerdp/settings.h>
#include <winpr/string.h>
#include <winpr/synch.h>
#include <winpr/user.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include "../Logging.h"

namespace vindauga {

namespace {

// The context struct must begin with rdpClientContext (not just rdpContext): the FreeRDP
// helpers reused for AAD (client_cli_get_access_token, freerdp_client_get_aad_url) cast
// instance->context to rdpClientContext* internally. `self` gives the free C callbacks
// access to the owning RdpSession.
struct VindaugaRdpContext {
    rdpClientContext client;
    void* self = nullptr;
    // Set by vindaugaOnChannelConnected when the "disp" (Display Control) channel
    // connects; used by RdpSession::requestResize. Stays nullptr (and requestResize is a
    // no-op) if the server does not negotiate the channel.
    DispClientContext* disp = nullptr;
    // "Channel connected" only means the DVC tunnel is open, not that the server will
    // honour a DISPLAY_CONTROL_MONITOR_LAYOUT yet: MS-RDPEDISP requires the server's
    // DISPLAYCONTROL_CAPS_PDU (DispClientContext::DisplayControlCaps) to arrive first,
    // and a layout sent before that is silently ignored (SendMonitorLayout still returns
    // CHANNEL_RC_OK). Equivalent to the `activated` flag in FreeRDP's X11 client
    // (xf_disp.c).
    bool dispCapsReceived = false;
    // Set by vindaugaOnChannelConnected when the "cliprdr" (clipboard) channel connects.
    // Stays nullptr (and clipboard sync is a no-op) if the server does not negotiate the
    // channel (e.g. redirectclipboard:i:0 in the .rdp file).
    CliprdrClientContext* cliprdr = nullptr;
};

BOOL vindaugaGlobalInit() {
    return TRUE;
}

void vindaugaGlobalUninit() {}

// Builds https://<AzureActiveDirectory-host>/<tenantid>/oauth2/nativeclient, the same
// string FreeRDP's non-exported get_redirect_uri() builds internally when
// FreeRDP_UseCommonStdioCallbacks is TRUE (without it FreeRDP builds a Windows-specific
// ms-appx-web:// broker URI). Reconstructed here because the function is not exported.
QString vindaugaBuildRedirectPrefix(const rdpSettings* settings) {
    const char* adHost = freerdp_settings_get_string(settings, FreeRDP_GatewayAzureActiveDirectory);
    QString tenantId = QStringLiteral("common");
    if (freerdp_settings_get_bool(settings, FreeRDP_GatewayAvdUseTenantid)) {
        const char* t = freerdp_settings_get_string(settings, FreeRDP_GatewayAvdAadtenantid);
        if (t && *t)
            tenantId = QString::fromUtf8(t);
    }
    return QStringLiteral("https://%1/%2/oauth2/nativeclient")
        .arg(QString::fromUtf8(adHost && *adHost ? adHost : "login.microsoftonline.com"),
             tenantId);
}

// Replaces FreeRDP's manual "print URL / read stdin" step with a registered
// AadInteractiveAuth. URL construction and the token exchange (including PoP/req_cnf)
// remain FreeRDP's own logic via the exported freerdp_client_get_aad_url() /
// client_common_get_access_token(); nothing is generated or signed here.
//
// Without a registered AadInteractiveAuth (e.g. headless CLI testing) this falls back
// to FreeRDP's client_cli_get_access_token() (manual URL paste via stdin).
BOOL vindaugaGetCommonAccessToken(rdpContext* context, AccessTokenType tokenType, char** token,
                                  size_t count, ...) {
    auto* self = static_cast<RdpSession*>(reinterpret_cast<VindaugaRdpContext*>(context)->self);
    AadInteractiveAuth* auth = self ? self->aadInteractiveAuth() : nullptr;

    va_list ap;
    va_start(ap, count);
    QByteArray scope, reqCnf;
    if (tokenType == ACCESS_TOKEN_TYPE_AAD) {
        if (count < 2) {
            va_end(ap);
            qCWarning(lcRdp) << "ACCESS_TOKEN_TYPE_AAD with too few arguments:" << count;
            return FALSE;
        }
        scope = QByteArray(va_arg(ap, const char*));
        reqCnf = QByteArray(va_arg(ap, const char*));
    } else if (tokenType != ACCESS_TOKEN_TYPE_AVD) {
        va_end(ap);
        qCWarning(lcRdp) << "Unknown AccessTokenType:" << static_cast<int>(tokenType);
        return FALSE;
    }
    va_end(ap);

    if (!auth) {
        qCDebug(lcRdp) << "No AadInteractiveAuth registered — using FreeRDP's manual "
                          "URL paste (fallback)";
        if (tokenType == ACCESS_TOKEN_TYPE_AAD)
            return client_cli_get_access_token(context->instance, tokenType, token, 2,
                                               scope.constData(), reqCnf.constData());
        return client_cli_get_access_token(context->instance, tokenType, token, 0);
    }

    // Force FreeRDP's internal redirect construction to the real "nativeclient" HTTPS
    // page (not the Windows broker URI); see vindaugaBuildRedirectPrefix.
    freerdp_settings_set_bool(context->settings, FreeRDP_UseCommonStdioCallbacks, TRUE);

    auto* cctx = reinterpret_cast<rdpClientContext*>(context);
    const freerdp_client_aad_type authReqType =
        tokenType == ACCESS_TOKEN_TYPE_AAD ? FREERDP_CLIENT_AAD_AUTH_REQUEST
                                            : FREERDP_CLIENT_AAD_AVD_AUTH_REQUEST;
    char* authUrlRaw = tokenType == ACCESS_TOKEN_TYPE_AAD
                            ? freerdp_client_get_aad_url(cctx, authReqType, scope.constData())
                            : freerdp_client_get_aad_url(cctx, authReqType);
    if (!authUrlRaw) {
        qCWarning(lcRdp) << "freerdp_client_get_aad_url (auth request) failed";
        return FALSE;
    }
    const QUrl authUrl(QString::fromUtf8(authUrlRaw));
    free(authUrlRaw);

    const QString redirectPrefix = vindaugaBuildRedirectPrefix(context->settings);
    qCDebug(lcRdp) << "Opening AAD capture view, redirectPrefix=" << redirectPrefix;
    const QString code = auth->captureAuthorizationCode(authUrl, redirectPrefix);
    if (code.isEmpty()) {
        qCWarning(lcRdp) << "No authorization code captured (cancelled/failed/timed out)";
        return FALSE;
    }
    const QByteArray codeUtf8 = code.toUtf8();

    const freerdp_client_aad_type tokenReqType =
        tokenType == ACCESS_TOKEN_TYPE_AAD ? FREERDP_CLIENT_AAD_TOKEN_REQUEST
                                            : FREERDP_CLIENT_AAD_AVD_TOKEN_REQUEST;
    char* tokenRequestRaw =
        tokenType == ACCESS_TOKEN_TYPE_AAD
            ? freerdp_client_get_aad_url(cctx, tokenReqType, scope.constData(),
                                         codeUtf8.constData(), reqCnf.constData())
            : freerdp_client_get_aad_url(cctx, tokenReqType, codeUtf8.constData());
    if (!tokenRequestRaw) {
        qCWarning(lcRdp) << "freerdp_client_get_aad_url (token request) failed";
        return FALSE;
    }

    char* tokenRaw = nullptr;
    const BOOL rc = client_common_get_access_token(context->instance, tokenRequestRaw, &tokenRaw);
    free(tokenRequestRaw);
    if (rc && tokenRaw) {
        *token = tokenRaw; // ownership passes to aad.c, which frees it
    } else {
        qCWarning(lcRdp) << "client_common_get_access_token failed";
    }
    return rc;
}

BOOL vindaugaAuthenticateEx(freerdp* instance, char** username, char** password, char** domain,
                            rdp_auth_reason reason) {
    // The session is Entra-joined (targetisaadjoined:i:1), so RDP-level authentication is
    // handled by CredSSP/AAD token rather than username/password. pAuthenticateEx is
    // documented as "return TRUE and empty (as in empty string) credentials", but "on
    // input the CURRENT value" means FreeRDP may already have set a sensible default
    // (arm.c sets Domain to "AzureAD" if null before the AUTH_RDSTLS call). Only fill in
    // empty strings where the input is actually null; keep existing values otherwise.
    qCDebug(lcRdp) << "AuthenticateEx called, reason=" << static_cast<int>(reason)
                    << "username(in)=" << (*username ? *username : "(null)")
                    << "domain(in)=" << (*domain ? *domain : "(null)");

    // Neither an empty password nor the AAD access token used as password is accepted
    // for RDSTLS by an Entra-joined gateway. Without a WAM broker this client cannot do
    // the passwordless SSO Windows clients get, so like FreeRDP's own fallback we ask for
    // real credentials, via the registered RdstlsCredentialPrompt (a Qt Quick dialog,
    // not the terminal).
    if (reason == AUTH_RDSTLS && !*password) {
        auto* self = static_cast<RdpSession*>(
            reinterpret_cast<VindaugaRdpContext*>(instance->context)->self);
        RdstlsCredentialPrompt* prompt = self ? self->rdstlsCredentialPrompt() : nullptr;
        if (prompt) {
            const QString suggestedUsername = *username ? QString::fromUtf8(*username) : QString();
            const QString suggestedDomain = *domain ? QString::fromUtf8(*domain) : QString();
            const QStringList creds = prompt->requestCredentials(suggestedUsername, suggestedDomain);
            if (creds.size() != 3) {
                qCWarning(lcRdp) << "RDSTLS login cancelled by user";
                return FALSE;
            }
            free(*username);
            free(*domain);
            free(*password);
            *username = _strdup(creds.at(0).toUtf8().constData());
            *domain = _strdup(creds.at(1).toUtf8().constData());
            *password = _strdup(creds.at(2).toUtf8().constData());
        } else {
            qCDebug(lcRdp) << "No RdstlsCredentialPrompt registered — falling back to "
                              "empty credentials (confirmed insufficient)";
        }
    }

    if (!*username)
        *username = static_cast<char*>(calloc(1, 1));
    if (!*password)
        *password = static_cast<char*>(calloc(1, 1));
    if (!*domain)
        *domain = static_cast<char*>(calloc(1, 1));
    return *username && *password && *domain;
}

DWORD vindaugaVerifyCertificateEx(freerdp* /*instance*/, const char* host, UINT16 /*port*/,
                                  const char* /*commonName*/, const char* /*subject*/,
                                  const char* /*issuer*/, const char* /*fingerprint*/,
                                  DWORD /*flags*/) {
    // TODO: real certificate validation / user prompt. Accepted for this session only
    // for now.
    qCWarning(lcRdp) << "Accepting TLS certificate without verification (temporary):" << host;
    return 2; // accept for this session only
}

// cliprdr (clipboard) channel callbacks. All run on cliprdr's own dedicated channel
// thread (channel_client_thread_proc), not on the session's worker thread, unlike
// disp/RDPGFX; see RdpSession.h at requestRemoteClipboardText /
// respondWithLocalClipboardText for why that matters. QClipboard access (a GUI-thread
// API) happens inside RdpSession::setLocalClipboardText/localClipboardTextUtf16, not
// here.
UINT vindaugaCliprdrMonitorReady(CliprdrClientContext* context,
                                 const CLIPRDR_MONITOR_READY* /*monitorReady*/) {
    // Channel is ready: announce any existing local clipboard content right away so
    // pasting into the session works even if the text was copied before connecting
    // (same behaviour as mstsc/xfreerdp).
    auto* self = static_cast<RdpSession*>(
        reinterpret_cast<VindaugaRdpContext*>(context->rdpcontext)->self);
    if (self)
        self->announceLocalClipboardText();
    return CHANNEL_RC_OK;
}

UINT vindaugaCliprdrServerFormatList(CliprdrClientContext* context,
                                     const CLIPRDR_FORMAT_LIST* formatList) {
    // The protocol requires a response regardless of whether we use any of the offered
    // formats. Safe synchronously: it is only a reply to what we just received, not a new
    // outgoing request (matches xfreerdp's reference implementation).
    CLIPRDR_FORMAT_LIST_RESPONSE response = {};
    response.common.msgType = CB_FORMAT_LIST_RESPONSE;
    response.common.msgFlags = CB_RESPONSE_OK;
    context->ClientFormatListResponse(context, &response);

    bool hasText = false;
    for (UINT32 i = 0; i < formatList->numFormats; ++i) {
        if (formatList->formats[i].formatId == CF_UNICODETEXT) {
            hasText = true;
            break;
        }
    }
    if (!hasText)
        return CHANNEL_RC_OK;

    // Lazy fetch: the protocol only announces formats here; the content is requested
    // separately (the reply arrives in vindaugaCliprdrServerFormatDataResponse). Never
    // call ClientFormatDataRequest synchronously from here; see RdpSession.h at
    // requestRemoteClipboardText.
    auto* self = static_cast<RdpSession*>(
        reinterpret_cast<VindaugaRdpContext*>(context->rdpcontext)->self);
    if (self)
        QMetaObject::invokeMethod(self, "requestRemoteClipboardText", Qt::QueuedConnection);
    return CHANNEL_RC_OK;
}

UINT vindaugaCliprdrServerFormatDataResponse(CliprdrClientContext* context,
                                             const CLIPRDR_FORMAT_DATA_RESPONSE* response) {
    if (!(response->common.msgFlags & CB_RESPONSE_OK) || !response->requestedFormatData ||
        response->common.dataLen == 0)
        return CHANNEL_RC_OK;

    // CF_UNICODETEXT: null-terminated UTF-16 little-endian on the wire, which matches the
    // host's native byte order on x86/x86_64, so no conversion is needed.
    const auto* utf16Data = reinterpret_cast<const char16_t*>(response->requestedFormatData);
    size_t len = response->common.dataLen / 2;
    while (len > 0 && utf16Data[len - 1] == 0)
        --len; // strip trailing null terminator(s)
    const QString text = QString::fromUtf16(utf16Data, static_cast<int>(len));

    auto* self = static_cast<RdpSession*>(
        reinterpret_cast<VindaugaRdpContext*>(context->rdpcontext)->self);
    if (self)
        self->setLocalClipboardText(text);
    return CHANNEL_RC_OK;
}

UINT vindaugaCliprdrServerFormatDataRequest(CliprdrClientContext* context,
                                            const CLIPRDR_FORMAT_DATA_REQUEST* request) {
    if (request->requestedFormatId != CF_UNICODETEXT) {
        // Fast rejection: nothing to fetch for an unsupported format. Safe synchronously
        // (no GUI-thread access involved; matches xfreerdp's "format not found" fast path
        // in xf_cliprdr_server_format_data_request).
        CLIPRDR_FORMAT_DATA_RESPONSE response = {};
        response.common.msgType = CB_FORMAT_DATA_RESPONSE;
        response.common.msgFlags = CB_RESPONSE_FAIL;
        return context->ClientFormatDataResponse(context, &response);
    }

    // Do not call ClientFormatDataResponse synchronously from here when real data must
    // be fetched (that also needs a blocking GUI-thread hop for QClipboard); see
    // RdpSession.h at respondWithLocalClipboardText.
    auto* self = static_cast<RdpSession*>(
        reinterpret_cast<VindaugaRdpContext*>(context->rdpcontext)->self);
    if (!self) {
        // RdpSession is gone (session shutting down): answer FAIL synchronously rather
        // than nothing, otherwise the server waits for a reply that never comes.
        CLIPRDR_FORMAT_DATA_RESPONSE response = {};
        response.common.msgType = CB_FORMAT_DATA_RESPONSE;
        response.common.msgFlags = CB_RESPONSE_FAIL;
        return context->ClientFormatDataResponse(context, &response);
    }
    QMetaObject::invokeMethod(self, "respondWithLocalClipboardText", Qt::QueuedConnection);
    return CHANNEL_RC_OK;
}

// Called by FreeRDP when the server's DISPLAYCONTROL_CAPS_PDU arrives; this, not
// "channel connected", is the real "you may now send DISPLAY_CONTROL_MONITOR_LAYOUT"
// signal (see dispCapsReceived). Registered on vctx->disp->DisplayControlCaps in
// vindaugaOnChannelConnected, where vctx->disp->custom is pointed back at the
// rdpContext so `self` is reachable from here.
UINT vindaugaDispCaps(DispClientContext* context, UINT32 /*maxNumMonitors*/,
                      UINT32 /*maxMonitorAreaFactorA*/, UINT32 /*maxMonitorAreaFactorB*/) {
    auto* vctx = reinterpret_cast<VindaugaRdpContext*>(context->custom);
    if (!vctx)
        return CHANNEL_RC_OK;
    vctx->dispCapsReceived = true;
    qCInfo(lcRdp) << "Display Control channel reported ready (DisplayControlCaps)";

    auto* self = static_cast<RdpSession*>(vctx->self);
    if (self && self->lastRequestedResize().isValid()) {
        qCInfo(lcRdp) << "Re-sending stored resize (disp channel now ACTUALLY ready):"
                      << self->lastRequestedResize() << "scale=" << self->lastRequestedScale();
        self->requestResize(self->lastRequestedResize(), self->lastRequestedScale());
    }
    return CHANNEL_RC_OK;
}

// Thin wrapper around freerdp_client_OnChannelConnectedEventHandler (still needed
// unchanged for RDPGFX, see vindaugaPreConnect) that additionally captures the "disp"
// channel's DispClientContext* for requestResize() and the "cliprdr" channel's
// CliprdrClientContext*, registering the callbacks above on the latter. e->pInterface is
// the client context pointer for the named channel (ChannelConnectedEventArgs,
// freerdp/event.h).
void vindaugaOnChannelConnected(void* context, const ChannelConnectedEventArgs* e) {
    freerdp_client_OnChannelConnectedEventHandler(context, e);
    if (!e->name)
        return;
    if (std::strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0) {
        auto* disp = reinterpret_cast<DispClientContext*>(e->pInterface);
        reinterpret_cast<VindaugaRdpContext*>(context)->disp = disp;
        // Do not send a resize from here: the channel is only "connected" (DVC tunnel
        // open), not necessarily ready to accept a layout yet; see vindaugaDispCaps.
        disp->custom = context; // lets vindaugaDispCaps reach vctx/self
        disp->DisplayControlCaps = vindaugaDispCaps;
        qCInfo(lcRdp) << "Display Control channel connected (dynamic resolution available, "
                         "waiting for DisplayControlCaps before sending resize)";
    } else if (std::strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) == 0) {
        auto* cliprdr = reinterpret_cast<CliprdrClientContext*>(e->pInterface);
        reinterpret_cast<VindaugaRdpContext*>(context)->cliprdr = cliprdr;
        cliprdr->MonitorReady = vindaugaCliprdrMonitorReady;
        cliprdr->ServerFormatList = vindaugaCliprdrServerFormatList;
        cliprdr->ServerFormatDataRequest = vindaugaCliprdrServerFormatDataRequest;
        cliprdr->ServerFormatDataResponse = vindaugaCliprdrServerFormatDataResponse;
        qCDebug(lcRdp) << "Clipboard channel (cliprdr) connected";
    }
}

// Thin wrapper around freerdp_client_OnChannelDisconnectedEventHandler that also clears
// VindaugaRdpContext::cliprdr/disp when the channel disconnects. Without this, an
// already-queued requestRemoteClipboardText/respondWithLocalClipboardText (or a late
// requestResize) could run after FreeRDP has freed the channel context (use after
// free). The null checks in those methods only protect if the pointer is cleared here.
void vindaugaOnChannelDisconnected(void* context, const ChannelDisconnectedEventArgs* e) {
    freerdp_client_OnChannelDisconnectedEventHandler(context, e);
    if (!e->name)
        return;
    if (std::strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0) {
        auto* vctx = reinterpret_cast<VindaugaRdpContext*>(context);
        vctx->disp = nullptr;
        vctx->dispCapsReceived = false; // symmetric with disp above
    } else if (std::strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) == 0) {
        reinterpret_cast<VindaugaRdpContext*>(context)->cliprdr = nullptr;
    }
}

BOOL vindaugaPreConnect(freerdp* instance) {
    if (!freerdp_client_load_channels(instance)) {
        qCWarning(lcRdp) << "freerdp_client_load_channels failed";
        return FALSE;
    }
    // AVD session hosts render via the RDPGFX (Graphics Pipeline) channel, not the legacy
    // GDI bitmap-update path that the EndPaint hook alone covers. Without this
    // subscription the connection stays "connected" but frameReady is never emitted:
    // freerdp_client_OnChannelConnectedEventHandler calls gdi_graphics_pipeline_init()
    // when the rdpgfx channel connects, which routes RDPGFX updates into the same
    // gdi->primary_buffer that EndPaint reads.
    if (PubSub_SubscribeChannelConnected(instance->context->pubSub,
                                         vindaugaOnChannelConnected) < 0) {
        qCWarning(lcRdp) << "PubSub_SubscribeChannelConnected failed";
        return FALSE;
    }
    if (PubSub_SubscribeChannelDisconnected(instance->context->pubSub,
                                            vindaugaOnChannelDisconnected) < 0) {
        qCWarning(lcRdp) << "PubSub_SubscribeChannelDisconnected failed";
        return FALSE;
    }
    return TRUE;
}

// Reads hwnd->cinvalid/ninvalid, the list of disjoint dirty rectangles FreeRDP's GDI
// already builds per EndPaint (gdi_InvalidateRegion appends every rect to this
// unbounded, realloc-grown array while also growing hwnd->invalid to the bounding box).
// The bounding box alone is often a coarse overapproximation (e.g. a video window in one
// corner plus a blinking cursor elsewhere merge into one huge rect covering everything
// in between), so using the rect list copies less pixel data per EndPaint.
//
// gdi_init has set the local framebuffer format to PIXEL_FORMAT_BGRA32, which in memory
// (little-endian) is byte-for-byte identical to QImage::Format_ARGB32; no conversion.
BOOL vindaugaEndPaint(rdpContext* context) {
    rdpGdi* gdi = context->gdi;
    if (!gdi || gdi->suppressOutput)
        return TRUE;

    HGDI_WND hwnd = gdi->primary->hdc->hwnd;
    if (!hwnd || !hwnd->invalid || hwnd->invalid->null)
        return TRUE;

    auto* self = static_cast<RdpSession*>(reinterpret_cast<VindaugaRdpContext*>(context)->self);
    if (self) {
        // QImage wrapper over the GDI buffer, copied per region below because GDI reuses
        // the buffer on the next update (otherwise the frame data could change under the
        // UI thread, which paints asynchronously via Qt::QueuedConnection).
        const QImage frame(gdi->primary_buffer, gdi->width, gdi->height, gdi->stride,
                           QImage::Format_ARGB32);

        QList<std::pair<QImage, QRect>> regions;
        if (hwnd->cinvalid && hwnd->ninvalid > 0) {
            regions.reserve(static_cast<qsizetype>(hwnd->ninvalid));
            for (INT32 i = 0; i < hwnd->ninvalid; ++i) {
                const GDI_RGN& rgn = hwnd->cinvalid[i];
                if (rgn.null || rgn.w <= 0 || rgn.h <= 0)
                    continue;
                const QRect rect(rgn.x, rgn.y, rgn.w, rgn.h);
                regions.append({frame.copy(rect), rect});
            }
        }
        if (regions.isEmpty()) {
            // Defensive fallback: every known FreeRDP code path fills hwnd->cinvalid
            // together with hwnd->invalid (via gdi_InvalidateRegion), so this branch
            // should never be hit, but it guards against an unknown path that only sets
            // hwnd->invalid directly.
            const GDI_RGN* rgn = hwnd->invalid;
            const QRect damage(rgn->x, rgn->y, rgn->w, rgn->h);
            regions.append({frame.copy(damage), damage});
        }
        self->notifyFrameReady(regions);
    }

    hwnd->invalid->null = TRUE;
    hwnd->ninvalid = 0;
    return TRUE;
}

BOOL vindaugaDesktopResize(rdpContext* context) {
    rdpGdi* gdi = context->gdi;
    if (!gdi)
        return TRUE;

    const UINT32 width = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth);
    const UINT32 height = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight);
    if (!gdi_resize(gdi, width, height)) {
        qCWarning(lcRdp) << "gdi_resize failed";
        return FALSE;
    }

    auto* self = static_cast<RdpSession*>(reinterpret_cast<VindaugaRdpContext*>(context)->self);
    if (self)
        self->notifyDesktopResized(QSize(static_cast<int>(width), static_cast<int>(height)));
    return TRUE;
}

// Mouse pointer shape. The server sends Pointer_New/Set/SetNull/SetDefault (via the
// rdpPointer prototype registered with graphics_register_pointer in vindaugaClientNew)
// on the same thread as BeginPaint/EndPaint/DesktopResize: the session's worker thread,
// inside freerdp_check_event_handles(), not cliprdr's dedicated channel thread. Emitting
// directly from here is safe; Qt::AutoConnection queues to the GUI thread, same as
// notifyFrameReady.
struct VindaugaPointer {
    rdpPointer pointer;         // must be the first member; FreeRDP casts rdpPointer* to this
    BYTE* pixels = nullptr;     // ARGB32, pointer.width*pointer.height*4 bytes, set in New
};

BOOL vindaugaPointerNew(rdpContext* context, rdpPointer* pointer) {
    if (!context->gdi)
        return FALSE;
    auto* vp = reinterpret_cast<VindaugaPointer*>(pointer);
    const size_t size = static_cast<size_t>(pointer->width) * pointer->height * 4;
    vp->pixels = static_cast<BYTE*>(malloc(size));
    if (!vp->pixels)
        return FALSE;
    // PIXEL_FORMAT_BGRA32 is byte-for-byte identical to QImage::Format_ARGB32 in memory
    // (little-endian), so the buffer can be wrapped in a QImage in vindaugaPointerSet
    // without conversion. The FreeRDP helper handles both 32bpp ARGB and legacy 1bpp
    // AND/XOR mask pointers, including transparency.
    if (!freerdp_image_copy_from_pointer_data(vp->pixels, PIXEL_FORMAT_BGRA32, 0, 0, 0,
                                              pointer->width, pointer->height,
                                              pointer->xorMaskData, pointer->lengthXorMask,
                                              pointer->andMaskData, pointer->lengthAndMask,
                                              pointer->xorBpp, &context->gdi->palette)) {
        qCWarning(lcRdp) << "freerdp_image_copy_from_pointer_data failed (pointer shape)";
        free(vp->pixels);
        vp->pixels = nullptr;
        return FALSE;
    }
    return TRUE;
}

void vindaugaPointerFree(rdpContext* /*context*/, rdpPointer* pointer) {
    auto* vp = reinterpret_cast<VindaugaPointer*>(pointer);
    free(vp->pixels);
    vp->pixels = nullptr;
}

BOOL vindaugaPointerSet(rdpContext* context, rdpPointer* pointer) {
    auto* vp = reinterpret_cast<VindaugaPointer*>(pointer);
    auto* self = static_cast<RdpSession*>(reinterpret_cast<VindaugaRdpContext*>(context)->self);
    if (self && vp->pixels && pointer->width > 0 && pointer->height > 0) {
        // Deep copy (.copy()): vp->pixels is owned by FreeRDP's pointer object and may be
        // freed (vindaugaPointerFree, e.g. on cache eviction) any time after this call
        // returns; a QImage wrapping the raw buffer would not keep the data alive.
        const QImage view(vp->pixels, static_cast<int>(pointer->width),
                          static_cast<int>(pointer->height), static_cast<int>(pointer->width) * 4,
                          QImage::Format_ARGB32);
        self->notifyCursorChanged(
            view.copy(),
            QPoint(static_cast<int>(pointer->xPos), static_cast<int>(pointer->yPos)));
    }
    return TRUE;
}

BOOL vindaugaPointerSetNull(rdpContext* context) {
    auto* self = static_cast<RdpSession*>(reinterpret_cast<VindaugaRdpContext*>(context)->self);
    if (self)
        self->notifyCursorHidden();
    return TRUE;
}

BOOL vindaugaPointerSetDefault(rdpContext* context) {
    auto* self = static_cast<RdpSession*>(reinterpret_cast<VindaugaRdpContext*>(context)->self);
    if (self)
        self->notifyCursorReset();
    return TRUE;
}

BOOL vindaugaPostConnect(freerdp* instance) {
    // gdi_init() must run here rather than in the constructor: FreeRDP >= 3.31 requires
    // context->cache, which the core creates during the connect/capability negotiation
    // sequence. PostConnect is FreeRDP's documented point for this call.
    if (!gdi_init(instance, PIXEL_FORMAT_BGRA32)) {
        qCWarning(lcRdp) << "gdi_init failed (in PostConnect)";
        return FALSE;
    }
    instance->context->update->EndPaint = vindaugaEndPaint;
    instance->context->update->DesktopResize = vindaugaDesktopResize;
    return TRUE;
}

// FreeRDP calls this inside freerdp_connect() when a temporary condition blocks the
// connection and a retry may succeed; ERRCONNECT_TARGET_BOOTING (a deallocated AVD
// session host that Azure boots automatically) is exactly that situation. Because the
// retry happens inside the same freerdp_connect() call, the AAD/RDSTLS callbacks (already
// completed earlier in the sequence) are not re-run, so there are no repeated prompts.
// Returns milliseconds to wait before the next attempt, or -1 to give up (FreeRDP then
// treats it as a final failure, as if no callback were registered).
SSIZE_T vindaugaRetryDialog(freerdp* instance, const char* what, size_t current, void* /*userarg*/) {
    constexpr size_t kMaxAttempts = 20;   // ~20 x 15 s = 5 min, matching Azure's "up to 5 minutes"
    constexpr SSIZE_T kDelayMs = 15000;
    const QString whatStr = QString::fromUtf8(what ? what : "");
    if (current >= kMaxAttempts) {
        qCWarning(lcRdp) << "RetryDialog: giving up after" << current << "attempts (" << whatStr
                         << ")";
        return -1;
    }
    qCInfo(lcRdp) << "RetryDialog:" << whatStr << "— attempt" << (current + 1) << "of"
                  << kMaxAttempts << "— waiting" << (kDelayMs / 1000) << "s";
    auto* self =
        static_cast<RdpSession*>(reinterpret_cast<VindaugaRdpContext*>(instance->context)->self);
    if (self)
        self->notifyReconnecting(whatStr, static_cast<int>(current) + 1,
                                 static_cast<int>(kMaxAttempts));
    return kDelayMs;
}

BOOL vindaugaClientNew(freerdp* instance, rdpContext* context) {
    instance->PreConnect = vindaugaPreConnect;
    instance->PostConnect = vindaugaPostConnect;
    instance->AuthenticateEx = vindaugaAuthenticateEx;
    instance->VerifyCertificateEx = vindaugaVerifyCertificateEx;
    instance->RetryDialog = vindaugaRetryDialog;

    // Register the rdpPointer prototype for pointer shapes (New/Free/Set/SetNull/
    // SetDefault), following FreeRDP's own clients (xf_graphics_init). context->graphics
    // already exists here (created by freerdp_client_context_new before ClientNew);
    // context->gdi does not yet (gdi_init runs in vindaugaPostConnect), which is fine
    // because the callbacks that use context->gdi->palette only run during a live
    // connection. `size` is how much FreeRDP callocs per pointer object (the whole
    // VindaugaPointer), so the members after `rdpPointer pointer;` must remain plain POD:
    // no C++ constructors run, calloc only zero-initialises.
    rdpPointer pointerPrototype = {};
    pointerPrototype.size = sizeof(VindaugaPointer);
    pointerPrototype.New = vindaugaPointerNew;
    pointerPrototype.Free = vindaugaPointerFree;
    pointerPrototype.Set = vindaugaPointerSet;
    pointerPrototype.SetNull = vindaugaPointerSetNull;
    pointerPrototype.SetDefault = vindaugaPointerSetDefault;
    graphics_register_pointer(context->graphics, &pointerPrototype);

    return freerdp_set_common_access_token(context, vindaugaGetCommonAccessToken);
}

void vindaugaClientFree(freerdp* /*instance*/, rdpContext* /*context*/) {}

int vindaugaClientStart(rdpContext* /*context*/) {
    return 0;
}

int vindaugaClientStop(rdpContext* /*context*/) {
    return 0;
}

} // namespace

struct RdpSession::Impl {
    QString rdpText;
    QString aadToken;
    rdpContext* context = nullptr;
    QThread* thread = nullptr;
    AadInteractiveAuth* aadAuth = nullptr; // not owned by RdpSession
    RdstlsCredentialPrompt* rdstlsPrompt = nullptr; // not owned by RdpSession

    // Guard against self-triggering: armed (on the GUI thread) right before
    // setLocalClipboardText()'s QClipboard::setText() call, checked and cleared by the
    // clipboardConnection listener. Only accessed on the GUI thread, so no
    // synchronisation is needed.
    bool ignoringNextClipboardChange = false;
    // Connected in the constructor to QGuiApplication::clipboard()->dataChanged with qApp
    // as context, so the listener runs on the GUI thread even though RdpSession itself is
    // moved to the worker thread in start(). Disconnected explicitly in ~Impl() before
    // the rest of the cleanup, otherwise the lambda would keep referencing a dangling
    // `this`.
    QMetaObject::Connection clipboardConnection;

    // Last size requested via requestResize(), whether or not the request was actually
    // sent. Used to re-send the same request when the "disp" channel becomes ready: the
    // channel is negotiated asynchronously, well after the RDP connection (and the
    // connected() signal) is up, so a resize requested right after connected() would
    // otherwise be dropped without any follow-up. A default-constructed QSize
    // (isValid() == false) is the "no resize requested yet" sentinel.
    QSize lastRequestedResize;
    // Same "remember the last request" pattern as lastRequestedResize, for DPI scaling.
    qreal lastRequestedScale = 1.0;

    // The WaitForMultipleObjects in runConnectionLoop() only waits on FreeRDP's own
    // network/channel handles, with a 100 ms timeout as the only other wake-up. Input
    // slots queued from the GUI thread are delivered by processEvents() right after the
    // wait returns, so a click or key press could wait up to 100 ms whenever the server
    // happened to be quiet. This WinPR event (the same CreateEvent/SetEvent family
    // freerdp_abort_connect_context uses for stop()) is added to the handle array so the
    // GUI thread can wake the loop immediately via RdpSession::requestWakeUp().
    // Auto-reset (bManualReset=FALSE): only our loop waits on it, and the loop always
    // runs processEvents() after any wake-up, so there is no reason to keep it signalled
    // or reset it manually.
    HANDLE wakeEvent = nullptr;

    // Mouse position coalescing. Move/hover events fire far more often than other input
    // (continuously during a drag); queueing each one as its own QueuedConnection lambda
    // meant a quiet period accumulated a pile of stale positions that were all replayed
    // in order once the wait woke up, visible as a jumpy pointer. queueMouseMove() queues
    // at most one dispatchPendingMouseMove() at a time; a newer position arriving while
    // one is queued just overwrites pendingMousePos, so the last position wins.
    QMutex pendingMouseMutex;
    QPoint pendingMousePos;
    bool pendingMouseQueued = false;

    // Debounce for onDecodeFailureDetected(). Some screen regions freeze permanently
    // after a failed VAAPI/H264 decode because FreeRDP merely "ignores" the update
    // instead of asking the server for a new keyframe. A bad stretch can produce tens of
    // failures per second, and each recovery nudge (performResyncNudge) is a full RDPGFX
    // resync (decoder re-init etc.), so nudging on every one would be as bad as the
    // problem. Starts invalid (isValid() == false) so the first detection always nudges
    // immediately. No lock of its own: onDecodeFailureDetected(), the only reader/writer,
    // is invoked only while the global registration lock in Logging.cpp is held (see
    // DecodeFailureCallback in Logging.h), which serialises this state across threads.
    QElapsedTimer lastResyncNudgeTimer;

    ~Impl() {
        QObject::disconnect(clipboardConnection);
        if (wakeEvent)
            CloseHandle(wakeEvent);
        if (!context)
            return;
        gdi_free(context->instance);
        freerdp_client_context_free(context);
    }
};

RdpSession::RdpSession(QString rdpText, QString aadToken, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>()) {
    m_impl->rdpText = std::move(rdpText);
    m_impl->aadToken = std::move(aadToken);

    RDP_CLIENT_ENTRY_POINTS entryPoints = {};
    entryPoints.Version = RDP_CLIENT_INTERFACE_VERSION;
    entryPoints.Size = sizeof(RDP_CLIENT_ENTRY_POINTS_V1);
    entryPoints.GlobalInit = vindaugaGlobalInit;
    entryPoints.GlobalUninit = vindaugaGlobalUninit;
    entryPoints.ContextSize = sizeof(VindaugaRdpContext);
    entryPoints.ClientNew = vindaugaClientNew;
    entryPoints.ClientFree = vindaugaClientFree;
    entryPoints.ClientStart = vindaugaClientStart;
    entryPoints.ClientStop = vindaugaClientStop;

    rdpContext* context = freerdp_client_context_new(&entryPoints);
    if (!context) {
        qCWarning(lcRdp) << "freerdp_client_context_new failed";
        return;
    }
    reinterpret_cast<VindaugaRdpContext*>(context)->self = this;

    const QByteArray rdpUtf8 = m_impl->rdpText.toUtf8();
    rdpFile* file = freerdp_client_rdp_file_new();
    if (!file) {
        qCWarning(lcRdp) << "freerdp_client_rdp_file_new failed";
        freerdp_client_context_free(context);
        return;
    }

    const bool parsed = freerdp_client_parse_rdp_file_buffer(
        file, reinterpret_cast<const BYTE*>(rdpUtf8.constData()),
        static_cast<size_t>(rdpUtf8.size()));
    const bool populated =
        parsed && freerdp_client_populate_settings_from_rdp_file(file, context->settings);
    freerdp_client_rdp_file_free(file);

    if (!populated) {
        qCWarning(lcRdp) << "Failed to parse/populate settings from .rdp buffer";
        freerdp_client_context_free(context);
        return;
    }

    // See wakeEvent in Impl. Created here, once the context is known to be valid; ~Impl()
    // closes it regardless of how the constructor ends.
    m_impl->wakeEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_impl->wakeEvent)
        qCWarning(lcRdp) << "Failed to create wake event — input may see up to 100ms "
                             "extra latency on a quiet network";

    // Silence SCARD_E_NO_SERVICE spam on machines without pcscd/card reader; smartcard
    // redirection is unrelated to the AAD/RDSTLS authentication used here. Deliberately
    // overrides redirectsmartcards:i:1 from the .rdp file.
    freerdp_settings_set_bool(context->settings, FreeRDP_RedirectSmartCards, FALSE);

    // CONNECTION_TYPE_LAN is a static capability/Experience hint that may nudge the
    // server towards higher image quality. NetworkAutoDetect must stay enabled: the AVD
    // gateway sends an RTT Measure Request PDU regardless, and with auto-detect disabled
    // the client has not announced RNS_UD_CS_SUPPORT_NETCHAR_AUTODETECT, so autodetect.c
    // fails hard ("support was not enabled") and the whole connection drops.
    // TODO: it is unclear whether CONNECTION_TYPE_LAN alone helps against grainy image
    // quality. The CONNECTION_TYPE_LAN branch of freerdp_set_connection_type_from_file()
    // (unlike the AUTODETECT branch) does not set SupportGraphicsPipeline/RemoteFxCodec/
    // GfxH264/GfxAVC444, so they are set explicitly here to keep the RDPGFX pipeline.
    freerdp_settings_set_uint32(context->settings, FreeRDP_ConnectionType, CONNECTION_TYPE_LAN);
    freerdp_settings_set_bool(context->settings, FreeRDP_SupportGraphicsPipeline, TRUE);
    freerdp_settings_set_bool(context->settings, FreeRDP_RemoteFxCodec, TRUE);
    freerdp_settings_set_bool(context->settings, FreeRDP_GfxH264, TRUE);
    freerdp_settings_set_bool(context->settings, FreeRDP_GfxAVC444, TRUE);
    freerdp_settings_set_bool(context->settings, FreeRDP_GfxAVC444v2, TRUE);

    // Clipboard redirection is disabled for now: FreeRDP 3.30's own PDU parser
    // (cliprdr_read_format_list, cliprdr_common.c) fails on incoming format lists from
    // real Windows rdpclip senders ("invalid length, got 2, require at least 4") before
    // they ever reach our callbacks. This is a known upstream bug (the count pass and the
    // read pass used different loop conditions) fixed in FreeRDP 3.31.x. The format-name
    // mode is server-driven, so no client-side setting avoids the buggy branch. The
    // cliprdr callbacks above remain correct and are kept.
    // TODO: re-enable once FreeRDP >= 3.31.1 is the minimum supported version.
    freerdp_settings_set_bool(context->settings, FreeRDP_RedirectClipboard, FALSE);

    // gdi_init() is deliberately not called here; see vindaugaPostConnect. A session may
    // be constructed without ever connecting (e.g. in tests), and Impl::~Impl()'s
    // gdi_free() is null-safe whether or not gdi_init ever ran.

    // freerdp_keyboard_get_rdp_scancode_from_x11_keycode() (used by sendKeyEvent) reads
    // from an internal lookup table that is not self-initialising; without this call it
    // returns RDP_SCANCODE_UNKNOWN for every X11 keycode. WINPR_DEPRECATED since 3.11.0,
    // like the lookup function itself, but still exported and required.
    freerdp_keyboard_init_ex(freerdp_settings_get_uint32(context->settings, FreeRDP_KeyboardLayout),
                             freerdp_settings_get_string(context->settings,
                                                         FreeRDP_KeyboardRemappingList));

    m_impl->context = context;

    // Listen for local clipboard changes. QClipboard is a GUI-thread API, so the
    // connection uses qApp as context (3rd argument) to run the lambda on the GUI thread
    // regardless of which thread RdpSession is later moved to in start().
    // QGuiApplication::instance() may be absent (headless/Core-only use); clipboard sync
    // is then simply skipped and the rest of the session is unaffected.
    if (QGuiApplication::instance()) {
        m_impl->clipboardConnection = QObject::connect(
            QGuiApplication::clipboard(), &QClipboard::dataChanged, qApp, [this]() {
                if (m_impl->ignoringNextClipboardChange) {
                    m_impl->ignoringNextClipboardChange = false;
                    return;
                }
                QMetaObject::invokeMethod(this, "announceLocalClipboardText",
                                          Qt::QueuedConnection);
            });
    }

    qCDebug(lcRdp) << "RdpSession context created. gateway="
                    << freerdp_settings_get_string(context->settings, FreeRDP_GatewayHostname)
                    << "resourceprovider=arm:"
                    << freerdp_settings_get_bool(context->settings, FreeRDP_GatewayArmTransport);
}

RdpSession::~RdpSession() {
    stop();
    if (m_impl->thread) {
        m_impl->thread->quit();
        m_impl->thread->wait();
        delete m_impl->thread;
    }
}

void RdpSession::setAadInteractiveAuth(AadInteractiveAuth* auth) {
    m_impl->aadAuth = auth;
}

AadInteractiveAuth* RdpSession::aadInteractiveAuth() const {
    return m_impl->aadAuth;
}

void RdpSession::setRdstlsCredentialPrompt(RdstlsCredentialPrompt* prompt) {
    m_impl->rdstlsPrompt = prompt;
}

RdstlsCredentialPrompt* RdpSession::rdstlsCredentialPrompt() const {
    return m_impl->rdstlsPrompt;
}

void RdpSession::start() {
    if (m_impl->thread) {
        qCWarning(lcRdp) << "start() called again while a session is already running/has run";
        return;
    }
    if (!m_impl->context) {
        emit disconnected(QStringLiteral("Invalid context (parsing failed in constructor)"));
        return;
    }

    m_impl->thread = new QThread();
    moveToThread(m_impl->thread);
    connect(m_impl->thread, &QThread::started, this, &RdpSession::runConnectionLoop);
    m_impl->thread->start();
}

void RdpSession::stop() {
    if (!m_impl->context)
        return;
    // Safe to call from a thread other than the worker thread: that is exactly what
    // freerdp_abort_connect_context is for (it signals an event that the
    // WaitForMultipleObjects/freerdp_shall_disconnect_context in runConnectionLoop()
    // reacts to).
    freerdp_abort_connect_context(m_impl->context);
}

void RdpSession::runConnectionLoop() {
    rdpContext* context = m_impl->context;
    freerdp* instance = context->instance;

    if (!freerdp_connect(instance)) {
        const QString reason =
            QString::fromUtf8(freerdp_get_last_error_string(freerdp_get_last_error(context)));
        qCWarning(lcRdp) << "freerdp_connect failed:" << reason;
        emit disconnected(reason);
        thread()->quit();
        return;
    }

    qCInfo(lcRdp) << "Connected.";
    emit connected();

    // Registered for the whole session (removed in the cleanup below).
    // setDecodeFailureCallback is a global single-listener registry; that is fine because
    // only one RdpSession is active at a time and the owner tears down the previous
    // session (joining its thread) before constructing a new one. The lambda captures
    // only `this`, which outlives this runConnectionLoop() call.
    setDecodeFailureCallback([this]() { onDecodeFailureDetected(); });

    while (!freerdp_shall_disconnect_context(context)) {
        // WinPR's WaitForMultipleObjects never accepts more than MAXIMUM_WAIT_OBJECTS=64
        // handles (otherwise WAIT_FAILED, and the loop and hence the session dies).
        // Reserve one slot for wakeEvent by asking freerdp_get_event_handles for at most
        // 63.
        HANDLE handles[MAXIMUM_WAIT_OBJECTS];
        DWORD count = freerdp_get_event_handles(context, handles, MAXIMUM_WAIT_OBJECTS - 1);
        if (count == 0) {
            qCWarning(lcRdp) << "freerdp_get_event_handles returned 0 handles";
            break;
        }
        if (m_impl->wakeEvent)
            handles[count++] = m_impl->wakeEvent;
        const DWORD status = WaitForMultipleObjects(count, handles, FALSE, 100);
        if (status == WAIT_FAILED) {
            qCWarning(lcRdp) << "WaitForMultipleObjects failed";
            break;
        }
        if (!freerdp_check_event_handles(context))
            break;

        // Input slots and requestResize are delivered as queued slot calls from the UI
        // thread. This loop blocks for the whole session, so Qt's own exec() never runs
        // until we return; pump the event queue here so those calls get delivered.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
    }

    // Remove the listener before the rest of the cleanup: a late WLog line from the
    // disconnect sequence must not trigger performResyncNudge() on a session that is
    // already shutting down.
    setDecodeFailureCallback(nullptr);

    const QString reason =
        QString::fromUtf8(freerdp_get_last_error_string(freerdp_get_last_error(context)));
    PubSub_UnsubscribeChannelConnected(context->pubSub, vindaugaOnChannelConnected);
    PubSub_UnsubscribeChannelDisconnected(context->pubSub, vindaugaOnChannelDisconnected);
    freerdp_disconnect(instance);
    qCDebug(lcRdp) << "Disconnected:" << reason;
    emit disconnected(reason);
    thread()->quit();
}

void RdpSession::onDecodeFailureDetected() {
    // May be called from any thread (see DecodeFailureCallback in Logging.h), but never
    // concurrently: Logging.cpp holds its registration lock while invoking it. In
    // practice it fires on the worker thread, deep inside FreeRDP's own call stack while
    // an incoming PDU is being processed. Do not call back into FreeRDP from here (e.g.
    // requestResize()/SendMonitorLayout synchronously): re-entering FreeRDP's channel/
    // transport state from inside one of its own callbacks is not something its API
    // contract promises to be safe. Queue an independent call instead; it runs once the
    // stack has unwound back to the processEvents() pumping in runConnectionLoop(), the
    // same pattern the input slots use.
    //
    // Debounce (see Impl::lastResyncNudgeTimer; safe without a lock here because the
    // Logging.cpp lock already serialises all calls to this method): a bad stretch can
    // produce tens of failures per second, and nudging on every one would spam repeated
    // full RDPGFX resyncs.
    static constexpr qint64 kResyncCooldownMs = 4000;
    if (m_impl->lastResyncNudgeTimer.isValid() &&
        m_impl->lastResyncNudgeTimer.elapsed() < kResyncCooldownMs)
        return;
    m_impl->lastResyncNudgeTimer.restart();

    qCInfo(lcRdp) << "Decode failure detected (VAAPI/H264 — see 'ignoring update' in the log "
                     "just above) — asking the server for a fresh frame via an invisible "
                     "resize nudge";
    QMetaObject::invokeMethod(this, &RdpSession::performResyncNudge, Qt::QueuedConnection);
}

void RdpSession::performResyncNudge() {
    // FreeRDP's RDPGFX client API has no "request a new keyframe" concept (only
    // FrameAcknowledge/CacheImportOffer go client -> server), and h264_context_reset()
    // only resets the client decoder without telling the server, which would keep
    // sending delta frames against a GOP the client just discarded. The only mechanism
    // that involves the server is the same DispClientContext::SendMonitorLayout call (via
    // requestResize()) that a real window resize uses to trigger a full RDPGFX
    // ResetGraphics + CreateSurface + decoder re-init, here with the same size/scale as
    // last requested so there is no visible change. requestResize() has no "skip if
    // unchanged" check, so a request is actually sent each time; whether the server
    // treats an unchanged size as worth a full reset is not guaranteed.
    if (!lastRequestedResize().isValid())
        return; // no earlier resize to repeat; the UI normally sends one right after connected()
    requestResize(lastRequestedResize(), lastRequestedScale());
}

bool RdpSession::contextValid() const {
    return m_impl->context != nullptr;
}

QString RdpSession::gatewayHostname() const {
    if (!m_impl->context)
        return {};
    return QString::fromUtf8(
        freerdp_settings_get_string(m_impl->context->settings, FreeRDP_GatewayHostname));
}

bool RdpSession::isArmTransport() const {
    if (!m_impl->context)
        return false;
    return freerdp_settings_get_bool(m_impl->context->settings, FreeRDP_GatewayArmTransport);
}

// Input. All four share the same precondition: m_impl->context is always valid after a
// successful constructor (otherwise contextValid() == false and there is no session to
// drive); the only thing to guard against is input not being ready before
// freerdp_connect() has run PostConnect. rdpInput is always a valid pointer on the
// context (not lazily allocated), so checking the context is enough.
void RdpSession::sendKeyEvent(quint32 nativeScanCode, bool pressed, bool autoRepeat) {
    if (!m_impl->context)
        return;
    // WINPR_DEPRECATED since FreeRDP 3.11.0 ("implement yourself in client") but still
    // exported and functional; relies on the freerdp_keyboard_init_ex() call in the
    // constructor. X11/XKB keycodes are layout-independent (physical key position).
    // TODO: migrate to a dedicated table if/when FreeRDP removes this entirely.
    const DWORD rdpScancode =
        freerdp_keyboard_get_rdp_scancode_from_x11_keycode(static_cast<DWORD>(nativeScanCode));
    if (rdpScancode == RDP_SCANCODE_UNKNOWN) {
        qCDebug(lcRdp) << "sendKeyEvent: unknown nativeScanCode" << nativeScanCode
                       << "— no RDP scancode found, ignoring";
        return;
    }
    freerdp_input_send_keyboard_event_ex(m_impl->context->input, pressed ? TRUE : FALSE,
                                         autoRepeat ? TRUE : FALSE, rdpScancode);
}

void RdpSession::sendMouseMoveEvent(QPoint pos) {
    if (!m_impl->context)
        return;
    freerdp_input_send_mouse_event(m_impl->context->input, PTR_FLAGS_MOVE,
                                   static_cast<UINT16>(std::clamp(pos.x(), 0, 0xFFFF)),
                                   static_cast<UINT16>(std::clamp(pos.y(), 0, 0xFFFF)));
}

void RdpSession::requestWakeUp() {
    // Safe from any thread (typically the GUI thread right after queueing input). SetEvent
    // is thread-safe; wakeEvent may be nullptr if CreateEvent failed in the constructor,
    // in which case this silently degrades to timeout-only wake-ups.
    if (m_impl->wakeEvent)
        SetEvent(m_impl->wakeEvent);
}

void RdpSession::queueMouseMove(QPoint pos) {
    // See Impl::pendingMouseMutex/pendingMousePos/pendingMouseQueued. Called directly
    // from the GUI thread; not itself a slot, so no Qt::QueuedConnection marshalling here.
    // Only the dispatchPendingMouseMove() call below is queued.
    bool shouldSchedule = false;
    {
        QMutexLocker lock(&m_impl->pendingMouseMutex);
        m_impl->pendingMousePos = pos;
        shouldSchedule = !m_impl->pendingMouseQueued;
        m_impl->pendingMouseQueued = true;
    }
    if (shouldSchedule) {
        QMetaObject::invokeMethod(this, &RdpSession::dispatchPendingMouseMove, Qt::QueuedConnection);
    }
    requestWakeUp();
}

void RdpSession::dispatchPendingMouseMove() {
    // Runs on the worker thread (see queueMouseMove). Reads whatever the newest position
    // is now, not necessarily the one current when this dispatch was queued; that is the
    // point of the coalescing.
    QPoint pos;
    {
        QMutexLocker lock(&m_impl->pendingMouseMutex);
        pos = m_impl->pendingMousePos;
        m_impl->pendingMouseQueued = false;
    }
    sendMouseMoveEvent(pos);
}

void RdpSession::sendMouseButtonEvent(QPoint pos, Qt::MouseButton button, bool pressed) {
    if (!m_impl->context)
        return;
    UINT16 flags = 0;
    switch (button) {
    case Qt::LeftButton:
        flags = PTR_FLAGS_BUTTON1;
        break;
    case Qt::RightButton:
        flags = PTR_FLAGS_BUTTON2;
        break;
    case Qt::MiddleButton:
        flags = PTR_FLAGS_BUTTON3;
        break;
    default:
        return; // other buttons (X1/X2 etc.) are not forwarded
    }
    if (pressed)
        flags |= PTR_FLAGS_DOWN;
    freerdp_input_send_mouse_event(m_impl->context->input, flags,
                                   static_cast<UINT16>(std::clamp(pos.x(), 0, 0xFFFF)),
                                   static_cast<UINT16>(std::clamp(pos.y(), 0, 0xFFFF)));
}

void RdpSession::sendWheelEvent(QPoint pos, int deltaY) {
    if (!m_impl->context || deltaY == 0)
        return;
    UINT16 flags = PTR_FLAGS_WHEEL;
    if (deltaY < 0)
        flags |= PTR_FLAGS_WHEEL_NEGATIVE;
    flags |= static_cast<UINT16>(std::clamp(std::abs(deltaY), 0, 0xFF)) & WheelRotationMask;
    freerdp_input_send_mouse_event(m_impl->context->input, flags,
                                   static_cast<UINT16>(std::clamp(pos.x(), 0, 0xFFFF)),
                                   static_cast<UINT16>(std::clamp(pos.y(), 0, 0xFFFF)));
}

void RdpSession::requestResize(QSize size, qreal scale) {
    // Remember the last requested size and scale regardless of the outcome below, so
    // vindaugaDispCaps can re-send them once the disp channel is actually ready.
    m_impl->lastRequestedResize = size;
    m_impl->lastRequestedScale = scale;

    auto* vctx = reinterpret_cast<VindaugaRdpContext*>(m_impl->context);
    // vctx->disp alone (channel "connected", DVC tunnel open) is not enough: the server
    // silently ignores a DISPLAY_CONTROL_MONITOR_LAYOUT sent before its own
    // DISPLAYCONTROL_CAPS_PDU has been received (dispCapsReceived). See vindaugaDispCaps.
    if (!vctx || !vctx->disp || !vctx->dispCapsReceived) {
        qCInfo(lcRdp) << "requestResize: Display Control channel not ACTUALLY ready yet for"
                      << size << "(connected=" << (vctx && vctx->disp)
                      << "caps-received=" << (vctx && vctx->dispCapsReceived)
                      << "), ignoring (re-sent when DisplayControlCaps arrives)";
        return;
    }
    qCInfo(lcRdp) << "requestResize: sending" << size << "to the server";
    // DISPLAY_CONTROL_MONITOR_LAYOUT requires even dimensions within
    // [MIN,MAX]_MONITOR_WIDTH/HEIGHT (channels/disp.h).
    auto clampEven = [](int v, int lo, int hi) {
        v = std::clamp(v, lo, hi);
        return v - (v % 2);
    };
    DISPLAY_CONTROL_MONITOR_LAYOUT monitor = {};
    monitor.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
    monitor.Left = 0;
    monitor.Top = 0;
    monitor.Width = static_cast<UINT32>(clampEven(size.width(), DISPLAY_CONTROL_MIN_MONITOR_WIDTH,
                                                   DISPLAY_CONTROL_MAX_MONITOR_WIDTH));
    monitor.Height =
        static_cast<UINT32>(clampEven(size.height(), DISPLAY_CONTROL_MIN_MONITOR_HEIGHT,
                                      DISPLAY_CONTROL_MAX_MONITOR_HEIGHT));
    monitor.Orientation = 0;

    // MS-RDPEDISP requires DeviceScaleFactor to be exactly 100, 140 or 180; otherwise the
    // receiver ignores DesktopScaleFactor entirely. DesktopScaleFactor itself (100-500)
    // carries the actual precision; DeviceScaleFactor is a mandatory "which of three
    // buckets" companion.
    static constexpr std::array<UINT32, 3> kValidDeviceScaleFactors{100, 140, 180};
    const UINT32 desiredPercent = static_cast<UINT32>(std::lround(scale * 100.0));
    UINT32 nearestDeviceScale = kValidDeviceScaleFactors.front();
    UINT32 bestDiff = std::numeric_limits<UINT32>::max();
    for (UINT32 candidate : kValidDeviceScaleFactors) {
        const UINT32 diff = candidate > desiredPercent ? candidate - desiredPercent
                                                        : desiredPercent - candidate;
        if (diff < bestDiff) {
            bestDiff = diff;
            nearestDeviceScale = candidate;
        }
    }
    monitor.DesktopScaleFactor = std::clamp(desiredPercent, 100u, 500u);
    monitor.DeviceScaleFactor = nearestDeviceScale;

    if (vctx->disp->SendMonitorLayout(vctx->disp, 1, &monitor) != CHANNEL_RC_OK)
        qCWarning(lcRdp) << "DispClientContext::SendMonitorLayout failed";
}

QSize RdpSession::lastRequestedResize() const { return m_impl->lastRequestedResize; }

qreal RdpSession::lastRequestedScale() const { return m_impl->lastRequestedScale; }

// Runs either directly on the cliprdr channel thread (called synchronously from
// vindaugaCliprdrMonitorReady, which is safe; see RdpSession.h) or queued on the worker
// thread via QMetaObject::invokeMethod from the QClipboard::dataChanged listener in the
// constructor (which itself runs on the GUI thread). No-op if the server has not
// negotiated the cliprdr channel.
void RdpSession::announceLocalClipboardText() {
    auto* vctx = reinterpret_cast<VindaugaRdpContext*>(m_impl->context);
    if (!vctx || !vctx->cliprdr)
        return;

    CLIPRDR_FORMAT format = {};
    format.formatId = CF_UNICODETEXT;
    format.formatName = nullptr;

    CLIPRDR_FORMAT_LIST formatList = {};
    formatList.common.msgType = CB_FORMAT_LIST;
    formatList.numFormats = 1;
    formatList.formats = &format;

    if (vctx->cliprdr->ClientFormatList(vctx->cliprdr, &formatList) != CHANNEL_RC_OK)
        qCWarning(lcRdp) << "CliprdrClientContext::ClientFormatList failed";
}

// Runs on the worker thread; never called synchronously from
// vindaugaCliprdrServerFormatList (which runs on cliprdr's dedicated channel thread). See
// RdpSession.h for why a synchronous ClientFormatDataRequest from there crashes the
// connection.
void RdpSession::requestRemoteClipboardText() {
    auto* vctx = reinterpret_cast<VindaugaRdpContext*>(m_impl->context);
    if (!vctx || !vctx->cliprdr)
        return;

    CLIPRDR_FORMAT_DATA_REQUEST request = {};
    request.common.msgType = CB_FORMAT_DATA_REQUEST;
    request.requestedFormatId = CF_UNICODETEXT;
    if (vctx->cliprdr->ClientFormatDataRequest(vctx->cliprdr, &request) != CHANNEL_RC_OK)
        qCWarning(lcRdp) << "CliprdrClientContext::ClientFormatDataRequest failed";
}

// Runs on the worker thread; never called synchronously from
// vindaugaCliprdrServerFormatDataRequest, for the same reason as above.
void RdpSession::respondWithLocalClipboardText() {
    auto* vctx = reinterpret_cast<VindaugaRdpContext*>(m_impl->context);
    if (!vctx || !vctx->cliprdr)
        return;

    const QByteArray data = localClipboardTextUtf16();
    CLIPRDR_FORMAT_DATA_RESPONSE response = {};
    response.common.msgType = CB_FORMAT_DATA_RESPONSE;
    response.common.msgFlags = data.isEmpty() ? CB_RESPONSE_FAIL : CB_RESPONSE_OK;
    response.common.dataLen = static_cast<UINT32>(data.size());
    response.requestedFormatData = reinterpret_cast<const BYTE*>(data.constData());
    if (vctx->cliprdr->ClientFormatDataResponse(vctx->cliprdr, &response) != CHANNEL_RC_OK)
        qCWarning(lcRdp) << "CliprdrClientContext::ClientFormatDataResponse failed";
}

void RdpSession::setLocalClipboardText(const QString& text) {
    // Called from the worker thread (cliprdr ServerFormatDataResponse callback).
    // QClipboard is a GUI-thread API, so hop there. Non-blocking: nothing to wait for, and
    // a slow GUI-thread event queue must not stall the RDP event loop.
    QMetaObject::invokeMethod(qApp, [this, text]() {
        m_impl->ignoringNextClipboardChange = true; // see the dataChanged listener in the constructor
        QGuiApplication::clipboard()->setText(text);
    });
}

QByteArray RdpSession::localClipboardTextUtf16() const {
    // Called from the worker thread (cliprdr ServerFormatDataRequest callback), which must
    // return the data synchronously: a blocking GUI-thread hop, unlike
    // setLocalClipboardText.
    QString text;
    QMetaObject::invokeMethod(
        qApp, [&text]() { text = QGuiApplication::clipboard()->text(); },
        Qt::BlockingQueuedConnection);

    if (text.isEmpty())
        return {};
    // CF_UNICODETEXT: null-terminated UTF-16 on the wire. QString's internal buffer is
    // already null-terminated at text.size(), so +1 character (2 bytes) captures the
    // terminator.
    return QByteArray(reinterpret_cast<const char*>(text.utf16()),
                      static_cast<int>((text.size() + 1) * 2));
}

} // namespace vindauga
