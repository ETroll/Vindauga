#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

#include "../Models.h"

// Parses feed-discovery XML (unused by the application). Pure QXmlStreamReader logic,
// no network I/O.
namespace vindauga {

class FeedParser {
public:
    // Step 1: discovery response (application/x-msts-radc-discovery+xml) -> one per <TenantFeedURL>.
    static QList<Workspace> parseDiscovery(const QByteArray& xml);

    // Step 2: web feed response (ResourceCollection) -> one per <Resource>.
    // workspaceTenantId is set on every RdpResource (it comes from the Workspace, not the feed XML).
    static QList<RdpResource> parseWebFeed(const QByteArray& xml, const QString& workspaceTenantId);
};

} // namespace vindauga
