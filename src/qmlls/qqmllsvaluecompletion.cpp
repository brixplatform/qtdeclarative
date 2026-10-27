// Copyright (C) 2026 Brix Platform contributors
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
#include "qqmllsvaluecompletion_p.h"
#include "qqmllsutils_p.h"
#include <QtQml/private/qqmljslexer_p.h>
#include <QtCore/private/qduplicatetracker_p.h>
#include <QtCore/qregularexpression.h>
QT_BEGIN_NAMESPACE
using namespace QQmlJS::Dom;
using namespace QLspSpecification;
using namespace Qt::StringLiterals;
namespace QQmlLSValueCompletion {
using Scope = QQmlJSScope::ConstPtr;

QQmlJSMetaProperty bindingProperty(const DomItem &binding,
                                 const std::shared_ptr<QQmlJSTypeResolver> &resolver)
{
    Scope scope = binding.qmlObject().semanticScope();
    QStringList names = binding.field(Fields::name).value().toString().split(u'.');
    while (scope && names.size() > 1) {
        const auto property = scope->property(names.first());
        if (property.isValid()) {
            names.removeFirst();
            scope = property.type();
            continue;
        }
        // An attached provider can be qualified by an import alias.
        bool attached = false;
        for (qsizetype count = names.size() - 1; count > 0; --count) {
            const auto imported = resolver->typeForName(names.first(count).join(u'.'));
            if (!imported || !imported->attachedType())
                continue;
            names = names.sliced(count);
            scope = imported->attachedType();
            attached = true;
            break;
        }
        if (!attached)
            scope = resolver->typeForId(scope, names.takeFirst());
    }
    return scope && !names.isEmpty() ? scope->property(names.first()) : QQmlJSMetaProperty{};
}

bool numeric(const Scope &type)
{
    if (!type)
        return false;
    const QString name = type->internalName();
    return name == u"int" || name == u"uint" || name == u"double" || name == u"float";
}

void rankBindingValues(const DomItem &item, const CompletionContextStrings &context,
                          QList<CompletionItem> &items)
{
    const auto binding = item.filterUp([](DomType type, const DomItem &) {
        return type == DomType::Binding;
    }, FilterUpOptions::ReturnOuter);
    const auto file = item.containingFile().as<QmlFile>();
    if (!binding || !file || !file->typeResolver())
        return;
    const auto locations = FileLocations::treeOf(binding);
    if (!locations)
        return;
    const auto colon = locations->info().regions.value(ColonTokenRegion);
    if (!colon.isValid() || context.offset() < colon.end()
        || context.offset() > locations->info().fullRegion.end())
        return;
    const QString expression = file->code().mid(colon.end(), context.offset() - colon.end());
    // Expected binding types do not describe arguments, literals, nested JavaScript
    // statements, ternaries, or object initializers. Let their normal completion
    // handlers supply the context instead.
    QQmlJS::Lexer lexer(nullptr);
    lexer.setCode(expression, 1, false);
    for (int token = lexer.lex(); token; token = lexer.lex()) {
        if (token != QQmlJSGrammar::T_IDENTIFIER && token != QQmlJSGrammar::T_DOT
            && token != QQmlJSGrammar::T_OR && token != QQmlJSGrammar::T_THIS)
            return;
    }
    const auto resolver = file->typeResolver();
    const auto property = bindingProperty(binding, resolver);
    const auto expected = property.type();
    if (!expected || expected->internalName() == u"QVariant" || expected->internalName() == u"QJSValue")
        return;
    const QString qualifier = context.base().toString().chopped(1);
    auto append = [&](const QByteArray &label, const QByteArray &insertion,
                      CompletionItemKind kind, const QByteArray &detail) {
        auto found = std::find_if(items.begin(), items.end(), [&](const auto &value) {
            return value.label == label;
        });
        if (found == items.end()) {
            CompletionItem completion;
            completion.label = label;
            completion.kind = kind;
            if (insertion != label)
                completion.insertText = insertion;
            items.append(completion);
            found = std::prev(items.end());
        }
        if (!found->detail || found->detail->isEmpty())
            found->detail = detail;
        found->sortText = "0" + label;
        found->filterText = label.mid(label.lastIndexOf('.') + 1);
    };
    for (auto &candidate : items)
        candidate.sortText = "1" + candidate.sortText.value_or(candidate.label);

    if (expected->scopeType() == QQmlSA::ScopeType::EnumScope) {
        const auto owner = expected->parentScope();
        if (!owner)
            return;
        QQmlJSMetaEnum enumeration;
        for (const auto &value : owner->enumerations()) {
            if (value.type() && value.type()->isSameType(expected)) {
                enumeration = value;
                break;
            }
        }
        QString ownerName = owner->internalName() == u"Qt" ? u"Qt"_s : resolver->nameForType(owner);
        if (!enumeration.isValid())
            return;
        QString enumQualifier = ownerName;
        if (ownerName.isEmpty()) {
            if (!enumeration.isQml() || owner != binding.qmlObject().semanticScope())
                return;
            enumQualifier = enumeration.name();
        } else if (enumeration.isScoped()) {
            enumQualifier += u'.' + enumeration.name();
        }
        const QString scopedName = ownerName.isEmpty() ? enumeration.name()
                : ownerName + u'.' + enumeration.name();
        if (!qualifier.isEmpty() && qualifier != ownerName && qualifier != enumQualifier
            && qualifier != scopedName)
            return;
        const QByteArray detail = scopedName.toUtf8();
        const QStringList used = expression.split(u'|');
        int usedBits = 0;
        for (qsizetype i = 0; i + 1 < used.size(); ++i)
            usedBits |= enumeration.value(used[i].trimmed().section(u'.', -1));
        const auto keys = enumeration.keys();
        QSet<QByteArray> suppressed;
        for (const QString &key : keys) {
            const int value = enumeration.value(key);
            bool repeated = false;
            for (qsizetype i = 0; i + 1 < used.size(); ++i)
                repeated |= used[i].trimmed().section(u'.', -1) == key;
            repeated |= enumeration.isFlag() && enumeration.hasValues() && value != 0
                    && (usedBits & value) == value;
            if ((enumeration.isFlag() && key.endsWith(u"_Mask")) || repeated) {
                suppressed.insert((qualifier.isEmpty() ? enumQualifier + u'.' + key : key).toUtf8());
                continue;
            }
            const QByteArray label = (qualifier.isEmpty() ? enumQualifier + u'.' + key : key).toUtf8();
            append(label, label, CompletionItemKind::EnumMember, detail);
        }
        items.removeIf([&](const CompletionItem &candidate) {
            return suppressed.contains(candidate.label);
        });
        return;
    }
    if (!qualifier.isEmpty())
        return;
    if (expected->internalName() == u"bool") {
        append("true", "true", CompletionItemKind::Value, "bool");
        append("false", "false", CompletionItemKind::Value, "bool");
    }
    const auto referrer = item.nearestSemanticScope();
    QDuplicateTracker<QString> seen;
    for (auto scope = binding.qmlObject().semanticScope(); scope; scope = scope->parentScope()) {
        for (const auto &[name, candidate] : scope->properties().asKeyValueRange()) {
            if (seen.hasSeen(name) || name == property.propertyName() || !candidate.type()
                || candidate.isList() != property.isList())
                continue;
            if (!expected->isSameType(candidate.type())
                && !(numeric(expected) && numeric(candidate.type()))
                && !(expected->accessSemantics() == QQmlSA::AccessSemantics::Reference
                     && candidate.type()->inherits(expected)))
                continue;
            const QString id = resolver->objectsById().id(scope, referrer);
            const QString insertion = id.isEmpty() ? name : id + u'.' + name;
            append(name.toUtf8(), insertion.toUtf8(), CompletionItemKind::Property,
                   candidate.typeName().toUtf8());
        }
        // Unqualified properties from an enclosing object are not in scope.
        // Grouped/attached scopes may reach their containing QML object, but
        // never cross into another component or a parent object's namespace.
        if (scope->scopeType() == QQmlSA::ScopeType::QMLScope)
            break;
    }
}
namespace {
struct Access {
    Scope scope;
    QStringList path;
};

Access access(const DomItem &item, int depth = 0)
{
    if (depth > 16)
        return {};
    if (QQmlLSUtils::isFieldMemberExpression(item)) {
        auto result = access(item.field(Fields::left), depth + 1);
        const QString name = item.field(Fields::right).field(Fields::identifier).value().toString();
        if (!result.scope || name.isEmpty())
            return {};
        result.path.append(name);
        return result;
    }
    const auto type = QQmlLSUtils::resolveExpressionType(item, QQmlLSUtils::ResolveOwnerType);
    if (!type || !type->semanticScope)
        return {};
    if (type->type == QQmlLSUtils::QmlObjectIdIdentifier)
        return {type->semanticScope, {}};
    if (type->name && (type->type == QQmlLSUtils::PropertyIdentifier
                       || type->type == QQmlLSUtils::JavaScriptIdentifier))
        return {type->semanticScope, {*type->name}};
    return {};
}

QString propertyName(const DomItem &item)
{
    const auto name = item.field(Fields::name);
    if (name.internalKind() == DomType::ScriptIdentifierExpression)
        return name.field(Fields::identifier).value().toString();
    if (name.internalKind() == DomType::ScriptLiteral)
        return name.field(Fields::value).value().toString();
    return {};
}

DomItem initializer(const Access &value, const DomItem &context)
{
    if (!value.scope || value.path.isEmpty()
        || value.scope->filePath() != context.canonicalFilePath())
        return {};
    const auto object = QQmlLSUtils::sourceLocationToDomItem(
            context.containingFile(), value.scope->sourceLocation()).qmlObject();
    return object.field(Fields::bindings).key(value.path.first()).index(0)
            .field(Fields::value).field(Fields::scriptElement);
}

void objectRecords(DomItem expression, const DomItem &context, QList<DomItem> &records, int depth = 0)
{
    if (!expression || depth > 8 || records.size() >= 128)
        return;
    switch (expression.internalKind()) {
    case DomType::ScriptObject:
        records.append(expression);
        return;
    case DomType::ScriptParenthesizedExpression:
        objectRecords(expression.field(Fields::expression), context, records, depth + 1);
        return;
    case DomType::ScriptArray:
        for (const auto &entry : expression.field(Fields::elements).values()) {
            if (records.size() >= 128)
                break;
            objectRecords(entry.field(Fields::initializer), context, records, depth + 1);
        }
        return;
    default: {
        const auto target = access(expression);
        if (target.scope && target.path.size() == 1)
            objectRecords(initializer(target, context), context, records, depth + 1);
        return;
    }
    }
}
} // namespace

bool dynamicMembers(const DomItem &owner, const DomItem &context,
                    std::back_insert_iterator<QList<CompletionItem>> result)
{
    const auto type = QQmlLSUtils::resolveExpressionType(owner,
            QQmlLSUtils::ResolveActualTypeForFieldMemberExpression);
    if (!type || (type->semanticScope && type->semanticScope->internalName() != u"QVariant"
                 && type->semanticScope->internalName() != u"QJSValue"))
        return false;
    const auto target = access(owner);
    if (!target.scope || target.path.isEmpty())
        return false;
    QMap<QString, QString> members;
    QList<DomItem> records;
    objectRecords(initializer(target, context), context, records);
    // A delegate's modelData comes from its owning view's model binding.
    if (target.path.first() == u"modelData") {
        auto delegate = context.qmlObject().filterUp([](DomType type, const DomItem &item) {
            return type == DomType::Binding && item.field(Fields::name).value().toString() == u"delegate";
        }, FilterUpOptions::ReturnOuter);
        if (delegate) {
            const auto model = delegate.qmlObject().field(Fields::bindings).key(u"model"_s).index(0);
            objectRecords(model.field(Fields::value).field(Fields::scriptElement), context, records);
        }
    }
    for (const auto &part : target.path.sliced(1)) {
        QList<DomItem> nested;
        for (const auto &record : records)
            for (const auto &property : record.field(Fields::properties).values())
                if (propertyName(property) == part)
                    objectRecords(property.field(Fields::initializer), context, nested);
        records = nested;
    }
    for (const auto &record : records) {
        for (const auto &property : record.field(Fields::properties).values()) {
            if (members.size() >= 128)
                break;
            const QString name = propertyName(property);
            // A dot completion must be a JavaScript IdentifierName. Keys such
            // as "long-name" need a bracket edit, not invalid dot insertion.
            static const QRegularExpression identifier(
                    u"^[\\p{L}\\p{Nl}_$][\\p{L}\\p{Nl}\\p{Mn}\\p{Mc}\\p{Nd}\\p{Pc}$\\x{200C}\\x{200D}]*$"_s);
            if (!identifier.match(name).hasMatch())
                continue;
            const auto value = property.field(Fields::initializer);
            const auto literal = value.field(Fields::value).value();
            QString type = u"var"_s;
            if (value.internalKind() == DomType::ScriptLiteral) {
                if (literal.isString()) type = u"QString"_s;
                else if (literal.isBool()) type = u"bool"_s;
                else if (literal.isInteger() || literal.isDouble()) type = u"double"_s;
            } else if (value.internalKind() == DomType::ScriptObject) {
                type = u"object"_s;
            }
            if (members.contains(name) && members.value(name) != u"has type "_s + type)
                type = u"var"_s;
            members.insert(name, u"has type "_s + type);
        }
    }
    // Dynamic inputs may have no declaration shape (for example a reusable
    // delegate's model supplied by another component). Offer only members
    // actually observed on this same resolved symbol, with honest provenance.
    int visited = 0;
    const FieldFilter filter{ {}, {{QString(), Fields::propertyInfos.toString()},
                                  {QString(), Fields::defaultPropertyName.toString()},
                                  {QString(), Fields::get.toString()}} };
    context.component().visitTree(Path(), emptyChildrenVisitor,
        VisitOption::Recurse | VisitOption::VisitSelf,
        [&](Path, const DomItem &current, bool) {
            if (++visited > 20000 || members.size() >= 128)
                return false;
            if (!QQmlLSUtils::isFieldMemberExpression(current))
                return true;
            const auto receiver = access(current.field(Fields::left));
            if (receiver.scope != target.scope || receiver.path != target.path)
                return true;
            const QString name = current.field(Fields::right).field(Fields::identifier).value().toString();
            if (!name.isEmpty() && name != u"_dummyIdentifier" && !members.contains(name))
                members.insert(name, u"var — observed on this dynamic value"_s);
            return true;
        }, emptyChildrenVisitor, filter);
    for (const auto &[name, detail] : members.asKeyValueRange()) {
        CompletionItem item;
        item.label = name.toUtf8();
        item.kind = CompletionItemKind::Field;
        item.detail = detail.toUtf8();
        if (detail.startsWith(u"has type ")) {
            MarkupContent documentation;
            documentation.kind = MarkupKind::Markdown;
            documentation.value = "Inferred from a model/record literal. Dynamic values may differ at runtime.";
            item.documentation = documentation;
        }
        item.sortText = "0" + item.label;
        result = item;
    }
    return !members.isEmpty();
}

} // namespace


QT_END_NAMESPACE
