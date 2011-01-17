#include "abstractoperation.h"
#include "abstractoperationobserver.h"
#include "queuehandler.h"
#include <QThread>
#include <QMetaObject>

#include "activelogs.h"
#ifdef ABSTRACT_OPERATION
    #define ENABLE_LOG_MACROS
#endif
#include "logmacros.h"

//TODOs and known problems:
/*

1) cancel current operation (calling @cancelAllOperations will cancel just the operations which are in the queue)
2) create a *real* priority queue, the current implementation suffers of Starvation of the normal queue if too many high priority are enqueued
3) implement pre-emption?

*/
AbstractOperation::AbstractOperation(QObject* aObserver, const char* aSlot) :
        m_observer(aObserver),
        m_status(OperationNotStarted)
{
    if(m_observer) {
        Q_ASSERT(aSlot);
        //TODO hackish! find a better way!!
        const char* methodName = aSlot + 1;
        m_slotToBeCalled.append(methodName);
    } else {
        WARNING("this operation does not have a observer, it will selfdestruct when ended" << this->id());
    }
}

AbstractOperation::~AbstractOperation() {
}

void AbstractOperation::setQueueHandler(QueueHandler* aQueueHandler) {
    m_queueHandler = aQueueHandler;
}

void AbstractOperation::setStatus(OperationStatus aStatus) {
    int statusTemp = m_status;
    m_status = aStatus;
    m_status |= (MASK_OperationCustomStatusCode & statusTemp);
}

AbstractOperation::OperationStatus AbstractOperation::status() const {
    return (AbstractOperation::OperationStatus)(m_status & MASK_OperationStatus);
}

void AbstractOperation::setCustomCode(int aCode) {
    m_status &= (MASK_OperationCustomStatusCode & aCode);
}

int AbstractOperation::customCode() const{
    return m_status & MASK_OperationCustomStatusCode;
}

QObject* AbstractOperation::observer() {
    return m_observer;
}

const char* AbstractOperation::callbackMethod() {
    return m_slotToBeCalled.data();
}

void AbstractOperation::started(int aTimeout) {
    setStatus(OperationRunning);
    m_queueHandler->startTimer(aTimeout);
}

void AbstractOperation::success() {
    setStatus(OperationSuccess);
}

void AbstractOperation::failed() {
    setStatus(OperationFailed);
}

void AbstractOperation::finished() {
    //do NOT make the operation call operationFinished if it timeout
    //since the queue handler would have called it already!
    if( status() != OperationTimedOut) {
        m_queueHandler->operationFinished();
    }
}

void AbstractOperation::cancel() {
}

int AbstractOperation::id() const {
    return (int)this;
}

bool AbstractOperation::canContinue() {
    Q_ASSERT(m_queueHandler);
    return m_queueHandler->currentOperationCanContinue();
}

void AbstractOperation::cleanThreadSpecificResources() {
    if( m_observer == 0 ) {
        delete this;
    }
}

QueueHandler* AbstractOperation::queueHandler() {
    return m_queueHandler;
}
