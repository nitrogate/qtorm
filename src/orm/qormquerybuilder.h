#ifndef QORMQUERYBUILDER_H
#define QORMQUERYBUILDER_H

#include <QtOrm/qormglobal.h>
#include <QtOrm/qormquery.h>

#include <QtCore/qobject.h>
#include <QtCore/qshareddata.h>
#include <QtCore/qvector.h>

QT_BEGIN_NAMESPACE

class QOrmAbstractProvider;
class QOrmWhereClauseBuilder;
class QOrmOrderClauseBuilder;
class QOrmQueryBuilderPrivate;

class Q_ORM_EXPORT QOrmQueryBuilder
{
public:
    QOrmQueryBuilder(QOrmAbstractProvider* provider, const QMetaObject& projectionMetaObject);
    QOrmQueryBuilder(const QOrmQueryBuilder&);
    ~QOrmQueryBuilder();

    QOrmQueryBuilder& operator=(const QOrmQueryBuilder&);

#ifdef Q_COMPILER_RVALUE_REFS
    QOrmQueryBuilder(QOrmQueryBuilder&&);
    QOrmQueryBuilder& operator=(QOrmQueryBuilder&&);
#endif

    QOrmQueryBuilder& first(int n);
    QOrmQueryBuilder& last(int n);

    QOrmQueryBuilder& where(QOrmWhereClauseBuilder whereClause);
    QOrmQueryBuilder& order(QOrmOrderClauseBuilder orderClause);

    template<typename T>
    QOrmQuery select()
    {
        return select(T::staticMetaObject);
    }

private:
    QOrmQuery select(const QMetaObject& projectionMetaObject);

    QSharedDataPointer<QOrmQueryBuilderPrivate> d;
};

QT_END_NAMESPACE

#endif // QORMQUERYBUILDER_H