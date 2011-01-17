#ifndef QUEUEHANDLER_H
#define QUEUEHANDLER_H

#include <QObject>
#include <QSemaphore>
#include <QMutex>
#include <QQueue>
#include <QHash>

class AbstractOperation;

class QueueHandler : public QObject
{
    static const char* CLASS_TAG() {
        return "QueueHandler";
    }

    Q_OBJECT
public:
    explicit QueueHandler(QSemaphore& aSemaphore, QThread* aMainThread, QThread* aWorkerThread);
    ~QueueHandler();
    //don't call this functions directly, used internally by AbstractOperation
    void startTimer(int aTimeoutInterval);
protected: //from QObject
    void timerEvent(QTimerEvent * event);
public:
    /**
      * Add Request to the worker thread.
      */
    virtual void addOperation(AbstractOperation* aNewOperation);
    /**
      * Add a High Priority Request to the worker thread.
      */
    virtual void addHighPriorityOperation(AbstractOperation* aNewOperation);
public:
    /**
      * Cancels all the request which are currently in the queue.
      */
    void cancelAllOperations();
    /**
      * Callback called from a AbstractOperation which just finished.
      * emits requestFinished().
      * aFinishedOperation is a pointer to the operation which just terminated.
      */
    void operationFinished();
    /**
      * Tell the current operation to stop or not at the first occasion.
      */
    void setCurrentOperationCanContinue(bool aCanContinue);
    /**
      * Check whether the current operation is supposed to stop.
      */
    bool currentOperationCanContinue();
    /**
      * Tell the thread to stop (clean up all operation and exit).
      */
    void setTerminateThread(bool aHasToTerminate);
    /**
      * Check whether the thread has to stop.
      */
    bool getTerminateThread();
    /**
      * Start operations to stop the thread.
      * This is a Synchronous API.
      */
    void terminateThread();
    /**
      * Check whether we have to cancel all operations.
      */
    bool getCancelAllOperations();
    /**
      * Tell to cancel all operations.
      */
    void setCancelAllOperations(bool aHasToCancelAll);
    /**
      * Just a useful debug function to check whether we are in the worker thread.
      */
    bool workerThreadCheck();
signals:
    void operationRetrieved();
    void operationNeeded();
    void cleanUpAndExit();
    void exit();
    void emptyQueue();
private slots:
    /**
      * Function called when we enter the _waiting_ state.
      */
    void onWaiting();
    /**
      * Function called when we enter the _processing_ state.
      */
    void onProcessing();
    /**
      * Function called when we enter the _exiting_ state.
      */
    void onExiting();
    /**
      * Process the first item in the queue if
      * - there isn't another operation currently being processed
      * - there queue is not empty
      */
    void doCancelAllOperations();
    /**
      * Cancel a Request if it has not started yet
      */
    void doCancelOperation(int aOperationId);
protected:
    virtual void endOperation(AbstractOperation* aOperation);
private:
    struct OperationsQueue {
        QQueue<int> m_queue;
        QHash<int, AbstractOperation*> m_hash;

        inline bool contains(int aId) {
            return m_queue.contains(aId);
        }

        inline int count() {
            return m_queue.count();
        }

        inline int head() {
            return m_queue.head();
        }

        inline void add(int aId, AbstractOperation* aOp) {
            m_queue.enqueue(aId);
            m_hash.insert(aId, aOp);
        }
        inline AbstractOperation* remove(int aId) {
            m_queue.removeOne(aId);
            return m_hash.take(aId);
        }
        inline AbstractOperation* dequeue() {
            AbstractOperation* result = 0;
            if(!m_queue.isEmpty()) {
                int headId = m_queue.dequeue();
                return m_hash.take(headId);
            }
            return result;
        }
        inline void enqueue(int aId, AbstractOperation* aOp) {
            m_queue.enqueue(aId);
            m_hash.insert(aId, aOp);
        }
    };
    void addOperationToQueue(AbstractOperation* aNewOperation, OperationsQueue& aOperationQueue);
    void removeOperationFromQueue(int aId, OperationsQueue& aOperationQueue);
    AbstractOperation* dequeueOperation(OperationsQueue& aOperationQueue);
    //bool checkForSentinelOperations();
    void fixCancelAllSemaphore();
protected:
    QThread* m_mainThread;
    QThread* m_workerThread;
private:
    // semaphore used to sync main thread and worker thread
    QSemaphore& m_semaphore;
    // semaphore used to signal the addition/processing of a operation
    QSemaphore m_operationWait;
    // mutex to control access to the request queues
    QMutex m_queueMutex;
    // mutex to control access to the current operation
    QMutex m_mutex_currentOperation;
    bool m_currentOperationCanContinue;
    bool m_exitThread;
    bool m_cancelAllOperations;

    //the operation queues
    OperationsQueue m_normalPriorityQueue;
    OperationsQueue m_highPriorityQueue;

    //the current operation being executed
    AbstractOperation* m_currentOperation;
    //the timer Id checking on the lifespan of the operation
    int m_timerId;
};

#endif // QUEUEHANDLER_H
