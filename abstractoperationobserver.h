#ifndef ABSTRACTOPERATIONOBSERVER_H
#define ABSTRACTOPERATIONOBSERVER_H

#include <QObject>
#include "abstractoperation.h"

class AbstractOperationObserver : public QObject {
    Q_OBJECT
public:
    AbstractOperationObserver(QObject* aParent = 0);
public slots:
    virtual void handledOperationFinished(AbstractOperation* aOperation) = 0;
    void handledOperationFinished(void* aOperation);
};

#endif // ABSTRACTOPERATIONOBSERVER_H
