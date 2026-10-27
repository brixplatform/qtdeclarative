// Copyright (C) 2026 Brix Platform contributors
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
#include "qqmllstypedocumentation_p.h"
#include "qqmllsutils_p.h"
#include <QtQmlDom/private/qqmldomelements_p.h>
#include <QtQmlDom/private/qqmldomexternalitems_p.h>
#include <QtQmlCompiler/private/qqmljstyperesolver_p.h>
#include <QtCore/qurl.h>
#include <QtCore/qfileinfo.h>
QT_BEGIN_NAMESPACE
using namespace QQmlJS::Dom;
using namespace Qt::StringLiterals;

std::optional<QByteArray> qmlTypeReference(const DomItem &file, QLspSpecification::Position position)
{
    const auto locations = QQmlLSUtils::itemsFromTextLocation(file, position.line, position.character);
    for (const auto &location : locations) {
        const auto item = location.domItem.qmlObject();
        const auto object = item.as<QmlObject>();
        if (!object || object->name().isEmpty() || !object->name().front().isUpper())
            continue;
        const auto tree = FileLocations::treeOf(item);
        const auto identifier = tree ? tree->info().regions.value(IdentifierRegion) : QQmlJS::SourceLocation{};
        const auto qml = file.as<QmlFile>();
        const auto offset = qml ? QQmlLSUtils::textOffsetFrom(qml->code(), position.line, position.character) : -1;
        if (!identifier.isValid() || offset < identifier.begin() || offset > identifier.end())
            continue;
        const auto scope = object->semanticScope();
        if (!scope || !scope->baseType())
            continue;
        const auto type = scope->baseType();
        QString text = u"# %1\n\nQML type · `%2`\n\n"_s.arg(object->name(), type->internalName());
        text += u"> API reference generated from resolved type metadata. "
                "The declaration below describes the API; it is not the implementation source.\n\n"_s;
        const auto definitions = QQmlLSUtils::findDefinitionOf(item, {});
        if (!definitions.isEmpty()) {
            text += u"[Declaration source](%1)\n\n"_s.arg(QUrl::fromLocalFile(
                    definitions.first().filename()).toString(QUrl::FullyEncoded));
        } else if (QFileInfo(type->filePath()).isAbsolute()) {
            text += u"[Type metadata/source](%1)\n\n"_s.arg(
                    QUrl::fromLocalFile(type->filePath()).toString(QUrl::FullyEncoded));
        }
        text += u"## API declaration\n\n```qml\n%1 {\n"_s.arg(object->name());
        auto properties = type->ownProperties();
        auto names = properties.keys();
        names.sort();
        int remaining = 96;
        for (const auto &name : names) {
            if (--remaining < 0)
                break;
            const auto property = properties.value(name);
            text += u"    %1property %2 %3\n"_s.arg(property.isWritable() ? QString() : u"readonly "_s,
                    property.typeName().isEmpty() ? u"var"_s : property.typeName(), name);
        }
        auto methods = type->ownMethods();
        auto methodNames = methods.keys();
        methodNames.removeDuplicates();
        methodNames.sort();
        for (const auto &name : methodNames) {
            for (const auto &method : type->ownMethods(name)) {
                if (--remaining < 0 || method.access() != QQmlJSMetaMethod::Public)
                    continue;
                QStringList parameters;
                for (const auto &parameter : method.parameters())
                    parameters.append(parameter.typeName() + u' ' + parameter.name());
                text += u"    %1 %2(%3)\n"_s.arg(
                        method.methodType() == QQmlJSMetaMethodType::Signal ? u"signal"_s : u"function"_s,
                        name, parameters.join(u", "_s));
            }
        }
        text += u"}\n```\n\n"_s;
        if (remaining < 0)
            text += u"Additional members were omitted from this bounded reference.\n\n"_s;
        if (type->baseType())
            text += u"Inherits `%1`. Inherited members remain available through completion.\n\n"_s.arg(
                    type->baseType()->internalName());
        return text.toUtf8();
    }
    return {};
}
QT_END_NAMESPACE
