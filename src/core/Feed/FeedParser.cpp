#include "FeedParser.h"

#include <QXmlStreamReader>

#include "../Logging.h"

namespace vindauga {

QList<Workspace> FeedParser::parseDiscovery(const QByteArray& xml) {
    QList<Workspace> result;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QLatin1String("TenantFeedURL"))
            continue;

        const auto attrs = reader.attributes();
        Workspace w;
        w.tenantId = attrs.value(QLatin1String("TenantId")).toString();
        w.displayName = attrs.value(QLatin1String("TenantDisplayName")).toString();
        w.geo = attrs.value(QLatin1String("Geo")).toString();
        w.feedUrl = QUrl(attrs.value(QLatin1String("FeedURL")).toString());
        w.conditionalAccessClaims = attrs.value(QLatin1String("ConditionalAccessClaims")).toString();
        result.append(w);
    }

    if (reader.hasError())
        qCWarning(lcFeed) << "Discovery XML parse error:" << reader.errorString();

    return result;
}

QList<RdpResource> FeedParser::parseWebFeed(const QByteArray& xml, const QString& workspaceTenantId) {
    QList<RdpResource> result;
    QXmlStreamReader reader(xml);

    bool inResource = false;
    RdpResource current;

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement()) {
            const auto elementName = reader.name();
            if (elementName == QLatin1String("Resource")) {
                current = RdpResource();
                current.workspaceTenantId = workspaceTenantId;
                const auto attrs = reader.attributes();
                current.id = attrs.value(QLatin1String("ID")).toString();
                current.title = attrs.value(QLatin1String("Title")).toString();
                current.type = attrs.value(QLatin1String("Type")).toString();
                current.deviceState = attrs.value(QLatin1String("DeviceState")).toString();
                inResource = true;
            } else if (inResource && elementName == QLatin1String("IconRaw")) {
                current.iconUrl = QUrl(reader.attributes().value(QLatin1String("FileURL")).toString());
            } else if (inResource && elementName == QLatin1String("ResourceFile")) {
                const auto attrs = reader.attributes();
                if (attrs.value(QLatin1String("FileExtension")) == QLatin1String(".rdp"))
                    current.rdpFileUrl = QUrl(attrs.value(QLatin1String("URL")).toString());
            }
        } else if (reader.isEndElement() && reader.name() == QLatin1String("Resource")) {
            if (inResource) {
                result.append(current);
                inResource = false;
            }
        }
    }

    if (reader.hasError())
        qCWarning(lcFeed) << "Web feed XML parse error:" << reader.errorString();

    return result;
}

} // namespace vindauga
