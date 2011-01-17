#include "queuehandler.h"
#include "workerthread.h"
#include "abstractoperation.h"
#include "abstractoperationobserver.h"
#include "sentineloperation.h"

#include <QMutexLocker>
#include <QTimer>
#include <QTimerEvent>


#include <QMetaObject>


#define INCONSISTENT_STATE() CRITICAL_TAG(CLASS_TAG(), "Inconsistent state: should never reach this")
#define HEX(toHex) QString::number((int)toHex,16)

#include "activelogs.h"
#ifdef WORKER_THREAD_QUEUE_HANDLER
#define ENABLE_LOG_MACROS
#endif
#include "logmacros.h"

#include <QStateMachine>
#include <QFinalState>

QueueHandler::QueueHandler(QSemaphore& aSemaphore, QThread* aMainThread, QThread* aWorkerThread) :
        QObject(0),
        m_mainThread(aMainThread),
        m_workerThread(aWorkerThread),
        m_semaphore(aSemaphore),
        m_exitThread(false),
        m_cancelAllOperations(false),
        m_currentOperation(0)
{
    QStateMachine* s_machine = new QStateMachine(this);
    QState* waiting = new QState(s_machine);
    QState* processing = new QState(s_machine);
    QState* exiting = new QState(s_machine);
    QFinalState* exit = new QFinalState(s_machine);

    s_machine->setInitialState(waiting);
    waiting->addTransition(this, SIGNAL(operationNeeded()), waiting);
    waiting->addTransition(this, SIGNAL(operationRetrieved()), processing);
    waiting->addTransition(this, SIGNAL(cleanUpAndExit()), exiting);
    connect(waiting, SIGNAL(entered()), this, SLOT(onWaiting()));


    processing->addTransition(this, SIGNAL(operationNeeded()), waiting);
    connect(processing, SIGNAL(entered()), this, SLOT(onProcessing()));

    connect(exiting, SIGNAL(entered()), this, SLOT(onExiting()));
    exiting->addTransition(this, SIGNAL(exit()), exit);

    connect(s_machine, SIGNAL(finished()), this, SLOT(deleteLater()));

    s_machine->start();
}

QueueHandler::~QueueHandler() {
    DEBUG_ENTER_FN();
    m_semaphore.release(1);
    DEBUG_EXIT_FN();
}

void QueueHandler::addOperation(AbstractOperation* aNewOperation) {
    DEBUG_ENTER_FN();
    {
        QMutexLocker locker(&m_queueMutex);
        addOperationToQueue(aNewOperation, m_normalPriorityQueue);
        m_operationWait.release(1);
    }
    DEBUG_EXIT_FN();
}

void QueueHandler::addHighPriorityOperation(AbstractOperation* aNewOperation) {
    DEBUG_ENTER_FN();
    {
        QMutexLocker locker(&m_queueMutex);
        addOperationToQueue(aNewOperation, m_highPriorityQueue);
        m_operationWait.release(1);
    }
    DEBUG_EXIT_FN();
}


void QueueHandler::addOperationToQueue(AbstractOperation* aOperation, OperationsQueue& aOperationQueue) {
    VERBOSE_ENTER_FN();
    // get rid of a previous istance of the operation if it is in the queue
    removeOperationFromQueue(aOperation->id(), aOperationQueue);
    // add request to the queue
    aOperationQueue.enqueue(aOperation->id(), aOperation);
    aOperation->setQueueHandler(this);
    aOperation->setStatus(AbstractOperation::OperationNotStarted);
    VERBOSE_EXIT_FN();
}

void QueueHandler::removeOperationFromQueue(int aId, OperationsQueue& aOperationQueue) {
    VERBOSE_ENTER_FN();
    // get rid of a previous istance of the operation if it is in the queue
    if(AbstractOperation* operation = aOperationQueue.remove(aId)) {
        operation->setStatus(AbstractOperation::OperationCancelled);
        operation->cleanThreadSpecificResources();
        endOperation(operation);
        m_operationWait.acquire(1);
    }
    VERBOSE_EXIT_FN();
}

void QueueHandler::cancelAllOperations() {
    DEBUG_ENTER_FN();
    {
        QMutexLocker locker(&m_mutex_currentOperation);
        {
            QMutexLocker locker(&m_queueMutex);

            // not use addOperation and addHighPriorityOperation because they will lock m_queueMutex
            // and we need to put those two operation together in both queues.
            // otherwise the queue will be likely left in a inconsistent state.
            addOperationToQueue(new SentinelOperation(), m_normalPriorityQueue);
            addOperationToQueue(new SentinelOperation(), m_highPriorityQueue);
            m_operationWait.release(2);
        }
        // we need to set both variables at the same time or the first two if in onWaiting
        // won't work.
        setCurrentOperationCanContinue(false);
        setCancelAllOperations(true);
    }
    QTimer::singleShot(0, this, SLOT(doCancelAllOperations()));
    DEBUG_EXIT_FN();
}

void QueueHandler::doCancelOperation(int aOperationId) {
    DEBUG_ENTER_FN();
    {
        QMutexLocker locker(&m_mutex_currentOperation);
        {
            QMutexLocker locker(&m_queueMutex);
            removeOperationFromQueue(aOperationId, m_normalPriorityQueue);
            removeOperationFromQueue(aOperationId, m_highPriorityQueue);
        }

        AbstractOperation* operation = m_currentOperation;
        if(operation &&
                operation->id() == aOperationId) {
            operation->setStatus(AbstractOperation::OperationCancelled);
            m_currentOperationCanContinue = false;
        }
    }

    DEBUG_EXIT_FN();
}

void QueueHandler::operationFinished() {
    DEBUG_ENTER_FN();
    Q_ASSERT(workerThreadCheck());
    {
        QMutexLocker locker(&m_mutex_currentOperation);
        if( AbstractOperation* operation = m_currentOperation ) {
            m_currentOperation = 0;
            DEBUG_TAG( CLASS_TAG(), "operationFinished, ptr:" << HEX(operation) << "id:" <<operation->id());
            if(m_timerId != 0) {
                killTimer(m_timerId);
                VERBOSE_TAG( CLASS_TAG(), "its timer id was" << m_timerId);
                m_timerId = 0;            
            }
            operation->cleanThreadSpecificResources();
            endOperation(operation);
        }

        {
            QMutexLocker locker(&m_queueMutex);
            if( 0 == m_normalPriorityQueue.count() &&
                    0 == m_highPriorityQueue.count()) {
                emit emptyQueue();
            }
        }
    }
    emit operationNeeded();
    DEBUG_EXIT_FN();
}

void QueueHandler::onWaiting() {
    DEBUG_ENTER_FN();
    Q_ASSERT(workerThreadCheck());

    m_operationWait.acquire(1);
    {
        QMutexLocker locker(&m_mutex_currentOperation);
        if(getCancelAllOperations()) {
            // we enter this ONLY if doCancelAllOperations() has not been called yet.
            // this happens when there was no current operation when cancelAllOperations() was called
            // we need to release then the semaphore we just acquired
            fixCancelAllSemaphore();
            //just wait here doCancelAllOperations() will be called eventually
            return;
        }
        if(getTerminateThread()) {
            // we enter this ONLY after a call to terminateThread()
            emit cleanUpAndExit();
            return;
        }

        AbstractOperation* nextOperation = 0;
        {
            QMutexLocker locker(&m_queueMutex);
            nextOperation = dequeueOperation( m_highPriorityQueue );
            if(nextOperation == 0) {
                // we haven't got a high priority operation
                nextOperation = dequeueOperation( m_normalPriorityQueue );
                if(nextOperation == 0) {
                    // we haven't got a normal priority operation either!
                    INCONSISTENT_STATE();
                    emit operationNeeded();
                }
            }
        }
        m_currentOperation = nextOperation;
        setCurrentOperationCanContinue(true);
        emit operationRetrieved();
    }
    DEBUG_EXIT_FN();
}

void QueueHandler::onProcessing() {
    DEBUG_ENTER_FN();
    Q_ASSERT(workerThreadCheck());
    AbstractOperation* nextOperation = 0;
    {
        QMutexLocker locker(&m_mutex_currentOperation);
        nextOperation = m_currentOperation;
    }
    if(nextOperation) {
        DEBUG_TAG( CLASS_TAG(), "processing request ptr:" << HEX(nextOperation) << "id:" << nextOperation->id());
        nextOperation->started();
        nextOperation->execute();
    } else {
        INCONSISTENT_STATE();
        emit operationNeeded();
    }
    DEBUG_EXIT_FN();
}

void QueueHandler::onExiting() {
    DEBUG_ENTER_FN();
    Q_ASSERT(workerThreadCheck());
    doCancelAllOperations();
    emit exit();
    DEBUG_EXIT_FN();
}

AbstractOperation* QueueHandler::dequeueOperation(OperationsQueue& aOperationQueue) {
    AbstractOperation* result = 0;
    if( aOperationQueue.count() ) {
        if( aOperationQueue.head() != KSENTINEL_OPERATION ) {
            result = aOperationQueue.dequeue();
            DEBUG_TAG( CLASS_TAG(), "dequeue a operation, ptr:" << HEX(result) << "id:" << result->id());
        } else {
            INCONSISTENT_STATE();
        }
    }
    return result;
}


void QueueHandler::startTimer(int aTimeoutInterval) {
    Q_ASSERT(workerThreadCheck());
    if(m_timerId != 0) {
        killTimer(m_timerId);
    }
    m_timerId = QObject::startTimer(aTimeoutInterval);
    VERBOSE_TAG( CLASS_TAG(), "started timer" << m_timerId << "with timeout" << aTimeoutInterval);
}

void QueueHandler::timerEvent(QTimerEvent * event) {
    Q_ASSERT(workerThreadCheck());
    WARNING_TAG( CLASS_TAG(), "timerEvent" << event->timerId());
    if(event &&
        (event->timerId() == m_timerId) &&
            m_currentOperation != 0) {

        WARNING_TAG( CLASS_TAG(), "an operation timed out");
        killTimer(m_timerId);
        // get rid of the operation which timeouted
        {
            QMutexLocker locker(&m_mutex_currentOperation);
            if(AbstractOperation* operation = m_currentOperation) {
                operation->setStatus(AbstractOperation::OperationTimedOut);
                operation->cancel();
            } else {
                INCONSISTENT_STATE();
            }
        }
        operationFinished();
    }
}

void QueueHandler::fixCancelAllSemaphore() {
    DEBUG_ENTER_FN();
    m_operationWait.release(1);
    DEBUG_EXIT_FN();
}

//deletes all operation and stops when a KSENTINEL_OPERATION is met
//the sentinel operation is cancelled too
void QueueHandler::doCancelAllOperations() {
    DEBUG_ENTER_FN();
    {
        QMutexLocker locker(&m_queueMutex);
        while( (m_normalPriorityQueue.count() > 0) ) {
            int opId = m_normalPriorityQueue.head();
            removeOperationFromQueue( opId, m_normalPriorityQueue );
            if(opId == KSENTINEL_OPERATION) {
                break;
            }
        }
        while( (m_highPriorityQueue.count() > 0) ) {
            int opId = m_highPriorityQueue.head();
            removeOperationFromQueue( opId, m_highPriorityQueue );
            if(opId == KSENTINEL_OPERATION) {
                break;
            }
        }
    }

    {
        QMutexLocker locker(&m_mutex_currentOperation);
        AbstractOperation* operation = m_currentOperation;
        if(operation) {
            operation->setStatus(AbstractOperation::OperationCancelled);
            operation->cancel();
        }
        setCancelAllOperations(false);
    }
    operationFinished();
    DEBUG_EXIT_FN();
}

void QueueHandler::endOperation(AbstractOperation* aOperation) {
    DEBUG_ENTER_FN();
    if(aOperation) {
        QObject* observer = aOperation->observer();
        QByteArray callbackMethodFullSig( aOperation->callbackMethod() );
        int parenthesis = callbackMethodFullSig.indexOf("(");
        QByteArray callbackMethod( callbackMethodFullSig.left(parenthesis) );

        if(observer) {
            if(false == QMetaObject::invokeMethod( observer, callbackMethod.data(), Qt::AutoConnection, Q_ARG( void*, (void*) aOperation ))) {
                CRITICAL_TAG( CLASS_TAG(), "could not invoke" << callbackMethod.data() << "callback method for operation!");
            }
        }
    }
    DEBUG_EXIT_FN();
}

bool QueueHandler::currentOperationCanContinue() {
    VERBOSE_ENTER_FN();
    bool result = false;
    {
        QMutexLocker locker(&m_mutex_currentOperation);
        result = m_currentOperationCanContinue;
    }
    VERBOSE_EXIT_FN();
    return result;
}

void QueueHandler::setCurrentOperationCanContinue(bool aCanContinue) {
    VERBOSE_ENTER_FN();
    m_currentOperationCanContinue = aCanContinue;
    VERBOSE_EXIT_FN();
}

bool QueueHandler::getCancelAllOperations() {
    VERBOSE_ENTER_FN();
    VERBOSE_EXIT_FN();
    return m_cancelAllOperations;
}

void QueueHandler::setCancelAllOperations(bool aHasToCancelAll) {
    DEBUG_ENTER_FN();
    m_cancelAllOperations = aHasToCancelAll;
    DEBUG_EXIT_FN();
}

bool QueueHandler::workerThreadCheck() {
    bool result = m_workerThread == QThread::currentThread();
    if(!result) {
        CRITICAL("wrong thread!!!! main is" << m_mainThread << "worker thread" << m_workerThread);
    }
    return result;
}

bool QueueHandler::getTerminateThread() {
    VERBOSE_ENTER_FN();
    VERBOSE_EXIT_FN();
    return m_exitThread;
}

void QueueHandler::setTerminateThread(bool aHasToTerminate) {
    VERBOSE_ENTER_FN();
    m_exitThread = aHasToTerminate;
    VERBOSE_EXIT_FN();
}

void QueueHandler::terminateThread() {
    DEBUG_ENTER_FN();
    {
        QMutexLocker locker(&m_mutex_currentOperation);
        setCurrentOperationCanContinue(false);
        setTerminateThread(true);
    }
    //fake an operation has come
    m_operationWait.release(1);
    m_semaphore.acquire(1);
    DEBUG_EXIT_FN();
}
