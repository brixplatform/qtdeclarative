// Copyright (C) 2026 Brix Platform contributors
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
#ifndef QQMLLSVALUECOMPLETION_P_H
#define QQMLLSVALUECOMPLETION_P_H
#include "qqmlcompletioncontextstrings_p.h"
#include <QtQmlDom/private/qqmldomitem_p.h>
#include <QtLanguageServer/private/qlanguageserverspectypes_p.h>
QT_BEGIN_NAMESPACE
namespace QQmlLSValueCompletion {
void rankBindingValues(const QQmlJS::Dom::DomItem &item, const CompletionContextStrings &context,
                       QList<QLspSpecification::CompletionItem> &items);
bool dynamicMembers(const QQmlJS::Dom::DomItem &owner, const QQmlJS::Dom::DomItem &context,
                    std::back_insert_iterator<QList<QLspSpecification::CompletionItem>> result);
}
QT_END_NAMESPACE
#endif
