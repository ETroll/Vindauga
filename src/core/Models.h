#pragma once
#include <QString>
#include <QUrl>

// Plain data structures for the feed-discovery flow (see FeedClient/AuthManager).
namespace vindauga {

// One tenant/workspace from feed discovery (step 1).
struct Workspace {
    QString tenantId;                 // TenantFeedURL/@TenantId
    QString displayName;              // @TenantDisplayName
    QString geo;                      // @Geo, e.g. "EU"
    QUrl    feedUrl;                  // @FeedURL (used in step 2)
    QString conditionalAccessClaims;  // @ConditionalAccessClaims (empty = no challenge)
};

// One desktop/app from the web feed (step 2).
struct RdpResource {
    QString id;                 // Resource/@ID
    QString title;              // Resource/@Title
    QString type;               // "Desktop" | "RemoteApp"
    QString deviceState;        // "Available" ...
    QUrl    rdpFileUrl;         // ResourceFile/@URL (do not modify the query; hash+endpointId are required)
    QUrl    iconUrl;            // IconRaw/@FileURL (optional, .ico)
    QUrl    iconPngUrl;         // Icon32/@FileURL (optional, 32x32 PNG; easier to show)
    QString workspaceTenantId;  // Workspace this resource came from
};

} // namespace vindauga
