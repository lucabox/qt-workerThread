#include "workerthread.h"

#include "abstractoperation.h"
//#include "abstractoperationobserver.h"
#include "queuehandler.h"

#include <QStringList>
#include <QMetaObject>


#include "activelogs.h"
#ifdef WORKER_THREAD
    #define ENABLE_LOG_MACROS
#endif
#include "logmacros.h"


#define KSENTINEL_OPERATION 0

//Worker Thread
WorkerThread::WorkerThread(QObject* aParent)
    : QThread(aParent),
    m_queueHandler(0)
{
    m_mainThread = currentThread();
}

WorkerThread::~WorkerThread() {
    //terminateThread();
}

void WorkerThread::startThread(QThread::Priority aPriority) {
    start(aPriority);
    m_semaphore.acquire(1);
}

void WorkerThread::terminateThread() {
    if(m_queueHandler) {
        m_queueHandler->terminateThread();
        m_queueHandler = 0;
    }
    quit();
    if(isRunning()) {
        wait(1500);
    }
}

void WorkerThread::addOperation(AbstractOperation* aNewOperation) {
    if(m_queueHandler) {
        m_queueHandler->addOperation(aNewOperation);
    }
}

void WorkerThread::addHighPriorityOperation(AbstractOperation* aNewOperation) {
    if(m_queueHandler) {
        m_queueHandler->addHighPriorityOperation(aNewOperation);
    }
}

void WorkerThread::cancelAllOperations() {
    if(m_queueHandler) {
        m_queueHandler->cancelAllOperations();
    }
}

void WorkerThread::cancelOperation(int aOperationId) {
    if(m_queueHandler) {
        QMetaObject::invokeMethod(m_queueHandler, "doCancelOperation", Qt::AutoConnection,  Q_ARG( int, aOperationId));
    }
}

QueueHandler* WorkerThread::createQueueHandler() {
    return new QueueHandler(m_semaphore, m_mainThread, this);
}


void WorkerThread::run() {
    //connect to the signal request finished so that when an operation finishes
    //we look for the next one in the queue
    m_queueHandler = createQueueHandler();
    connect(m_queueHandler, SIGNAL(emptyQueue()), this, SIGNAL(emptyQueue()));
    m_semaphore.release(1);
    exec();
}


bool genericOperationValidator(void* aOperation) {
    DEBUG_ENTER_FN();
    AbstractOperation* op = reinterpret_cast<AbstractOperation*>(aOperation);
    bool result = false;
    if(op) {
        switch(op->status()) {
        case AbstractOperation::OperationSuccess: {
            result = true;
            } break;
        case AbstractOperation::OperationCancelled: {
            WARNING("operation cancelled");
            } break;
        case AbstractOperation::OperationTimedOut: {
            WARNING("operation timeouted");
            } break;
        case AbstractOperation::OperationFailed: {
            WARNING("operation failed");
            } break;
        default:
            WARNING("should never reach this");
        }
    } else {
        CRITICAL("received a empty operation");
    }
    return result;
    DEBUG_EXIT_FN();
}

