// Copyright (C) 2026 Brix Platform contributors
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
#ifndef QQMLLSTYPEDOCUMENTATION_P_H
#define QQMLLSTYPEDOCUMENTATION_P_H
#include <QtQmlDom/private/qqmldomitem_p.h>
#include <QtLanguageServer/private/qlanguageserverspectypes_p.h>
QT_BEGIN_NAMESPACE
std::optional<QByteArray> qmlTypeReference(const QQmlJS::Dom::DomItem &file,
                                          QLspSpecification::Position position);
QT_END_NAMESPACE
#endif
