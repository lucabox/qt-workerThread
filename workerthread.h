#ifndef WORKERTHREAD_H
#define WORKERTHREAD_H

#include <QThread>
#include <QSemaphore>

class QueueHandler;
class AbstractOperation;

class WorkerThread : public QThread
{
    Q_OBJECT
public:
    WorkerThread(QObject* aParent = 0);
    ~WorkerThread();
    /**
      * Starts the thread, call it BEFORE adding any requests.
      * Ideally call this method as soon as the WorkerThread has been created
      */
    void startThread(QThread::Priority aPriority = QThread::LowestPriority);
    /**
      * Ends the thread (synchronously).
      * It cancels all current operations and stops the thread.
      */
    void terminateThread();
    /**
      * Add a normal priority @aNewOperation to the thread
      */
    virtual void addOperation(AbstractOperation* aNewOperation);
    /**
      * Add a high priority @aNewOperation to the thread
      */
    virtual void addHighPriorityOperation(AbstractOperation* aNewOperation);
    /**
      * Cancel an operation by Id
      */
    void cancelOperation(int aOperationId);
    /**
      * Cancel all current operations. (the current one might not be cancelled).
      */
    void cancelAllOperations();
signals:
    void emptyQueue();
protected:
    //override this method to provide your own queue handler
    //note that you should do this only if you want your operations
    //to be able to access services such as databases and network
    //while being executed in the worker thread
    virtual QueueHandler* createQueueHandler();
private:
    //do not override
    void run();
protected:
    // used to start the thread
    QSemaphore m_semaphore;
protected:
    QThread* m_mainThread;
private:
    QueueHandler* m_queueHandler;
};

bool genericOperationValidator(void* aOperation);

#endif //WORKERTHREAD_H
