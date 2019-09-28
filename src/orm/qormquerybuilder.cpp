#include "qormquerybuilder.h"

#include "qormabstractprovider.h"
#include "qormerror.h"
#include "qormfilter.h"
#include "qormfilterexpression.h"
#include "qormmetadatacache.h"
#include "qormorder.h"
#include "qormquery.h"
#include "qormqueryresult.h"
#include "qormrelation.h"
#include "qormsession.h"

#include <QDebug>

QT_BEGIN_NAMESPACE

class QOrmQueryBuilderPrivate : public QSharedData
{
    friend class QOrmQueryBuilder;

    QOrmQueryBuilderPrivate(QOrmSession* ormSession, const QOrmRelation& relation)
        : m_session{ormSession}
        , m_relation{relation}
    {
        Q_ASSERT(ormSession != nullptr);
    }

    template<typename ForwardIterable>
    Q_REQUIRED_RESULT static std::optional<QOrmFilter> foldFilters(const QOrmRelation& relation,
                                                                   const ForwardIterable& filters);

    Q_REQUIRED_RESULT
    static QOrmFilterExpression resolvedFilterExpression(const QOrmRelation& relation,
                                                         const QOrmFilterExpression& expression);

    QOrmSession* m_session{nullptr};

    QOrmRelation m_relation;
    std::optional<QOrmMetadata> m_projection;

    std::vector<QOrmFilter> m_filters;
    std::optional<QOrmOrderBuilder> m_orderBuilder;
};

template<typename ForwardIterable>
std::optional<QOrmFilter> QOrmQueryBuilderPrivate::foldFilters(const QOrmRelation& relation,
                                                               const ForwardIterable& filters)
{
    return std::accumulate(std::cbegin(filters),
                           std::cend(filters),
                           std::optional<QOrmFilter>{},
                           [relation](std::optional<QOrmFilter> lhs,
                                      const QOrmFilter& rhs) -> std::optional<QOrmFilter> {
                               if (rhs.type() == QOrm::FilterType::Expression)
                               {
                                   if (!lhs.has_value())
                                   {
                                       return rhs;
                                   }
                                   else
                                   {
                                       Q_ASSERT(lhs->type() == QOrm::FilterType::Expression);
                                       return std::make_optional<QOrmFilter>(
                                           *lhs->expression() &&
                                           resolvedFilterExpression(relation, *rhs.expression()));
                                   }
                               }

                               return std::nullopt;
                           });
}

QOrmFilterExpression
QOrmQueryBuilderPrivate::resolvedFilterExpression(const QOrmRelation& relation,
                                                  const QOrmFilterExpression& expression)
{    
    switch (expression.type())
    {
        case QOrm::FilterExpressionType::TerminalPredicate:
        {
            const QOrmFilterTerminalPredicate* predicate = expression.terminalPredicate();

            if (predicate->isResolved())
                return *predicate;

            const QOrmPropertyMapping* propertyMapping = nullptr;

            switch (relation.type())
            {
                case QOrm::RelationType::Mapping:
                    propertyMapping = relation.mapping()->classPropertyMapping(
                        predicate->classProperty()->descriptor());
                    break;

                case QOrm::RelationType::Query:
                    Q_ASSERT(relation.query()->projection().has_value());

                    propertyMapping = relation.query()->projection()->classPropertyMapping(
                        predicate->classProperty()->descriptor());
                    break;
            }

            if (propertyMapping == nullptr)
            {
                qCritical() << "QtOrm: Unable to resolve filter expression for class property"
                            << predicate->classProperty()->descriptor() << ", relation" << relation;
                qFatal("QtOrm: Malformed query filter");
            }

            return QOrmFilterTerminalPredicate{
                *propertyMapping, predicate->comparison(), predicate->value()};
        }

        case QOrm::FilterExpressionType::BinaryPredicate:
        {
            const QOrmFilterBinaryPredicate* predicate = expression.binaryPredicate();
            return QOrmFilterBinaryPredicate{resolvedFilterExpression(relation, predicate->lhs()),
                                             predicate->logicalOperator(),
                                             resolvedFilterExpression(relation, predicate->rhs())};
        }

        case QOrm::FilterExpressionType::UnaryPredicate:
        {
            const QOrmFilterUnaryPredicate* predicate = expression.unaryPredicate();
            return QOrmFilterUnaryPredicate{predicate->logicalOperator(),
                                            resolvedFilterExpression(relation, predicate->rhs())};
        }
    }

    qFatal("QtOrm: Unexpected state in %s", __PRETTY_FUNCTION__);
}

QOrmQueryBuilder::QOrmQueryBuilder(QOrmSession* ormSession, const QOrmRelation& relation)
    : d{new QOrmQueryBuilderPrivate{ormSession, relation}}
{
}

QOrmQueryBuilder::QOrmQueryBuilder(const QOrmQueryBuilder&) = default;

QOrmQueryBuilder::QOrmQueryBuilder(QOrmQueryBuilder&&) = default;

QOrmQueryBuilder::~QOrmQueryBuilder() = default;

QOrmQueryBuilder& QOrmQueryBuilder::operator=(const QOrmQueryBuilder&) = default;

QOrmQueryBuilder& QOrmQueryBuilder::operator=(QOrmQueryBuilder&&) = default;

QOrmQueryBuilder& QOrmQueryBuilder::filter(QOrmFilterExpression expression)
{
    d->m_filters.push_back(QOrmFilter{expression});
    return *this;
}

QOrmQueryBuilder& QOrmQueryBuilder::order(QOrmOrderBuilder orderBuilder)
{
    d->m_orderBuilder = orderBuilder;
    return *this;
}

QOrmQueryBuilder& QOrmQueryBuilder::projection(const QMetaObject& projectionMetaObject)
{
    d->m_projection = (*d->m_session->metadataCache())[projectionMetaObject];
    return *this;
}

QOrmQuery QOrmQueryBuilder::build(QOrm::Operation operation) const
{
    return QOrmQuery{operation,
                     d->m_relation,
                     d->m_projection,
                     d->foldFilters(d->m_relation, d->m_filters),
                     d->m_orderBuilder->build()};
}

QOrmQueryResult QOrmQueryBuilder::select()
{
    std::optional<QOrmFilter> filter =
        QOrmQueryBuilderPrivate::foldFilters(d->m_relation, d->m_filters);

    QOrmQuery query{
        QOrm::Operation::Read, d->m_relation, d->m_projection, filter, d->m_orderBuilder->build()};

    return d->m_session->execute(query);
}

QOrmQueryResult QOrmQueryBuilder::select(const QMetaObject& projectionMetaObject) const
{
    const QOrmMetadata& projection = (*d->m_session->metadataCache())[projectionMetaObject];

    std::optional<QOrmFilter> filter =
        QOrmQueryBuilderPrivate::foldFilters(d->m_relation, d->m_filters);

    QOrmQuery query{
        QOrm::Operation::Read, d->m_relation, projection, filter, d->m_orderBuilder->build()};

    return d->m_session->execute(query);
}

QOrmQueryResult QOrmQueryBuilder::remove(QOrm::RemoveMode removeMode) const
{
    if (removeMode != QOrm::RemoveMode::ForceRemoveAll && d->m_filters.empty())
    {
        qCritical() << "QtORM: Attempting to remove all entries in" << d->m_relation
                    << ". Either provide a filter or pass QOrm::RemoveMode::ForceRemoveAll";
        qFatal("QtORM: Security check failure");
    }

    std::optional<QOrmFilter> filter =
        QOrmQueryBuilderPrivate::foldFilters(d->m_relation, d->m_filters);

    QOrmQuery query{QOrm::Operation::Delete,
                    d->m_relation,
                    d->m_projection,
                    filter,
                    d->m_orderBuilder->build()};

    return d->m_session->execute(query);
}

QT_END_NAMESPACE
