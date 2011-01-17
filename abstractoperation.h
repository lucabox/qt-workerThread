#ifndef ABSTRACTOPERATION_H
#define ABSTRACTOPERATION_H

#include <QObject>
#include <QByteArray>
#include <QMetaType>

class WorkerThread;
class QueueHandler;


//TODO:
// - simplify the QueueHandler class so that what the operation sees is only operationFinished
// - unit tests!
// - implement priority queue?
//

const int KDefaultTimeoutOperation = 4 * 1000;

class AbstractOperation
{
public:
    enum OperationStatus
    {
        OperationNotStarted     = 0x00010000,
        OperationRunning        = 0x00020000,
        OperationSuccess        = 0x00040000,
        OperationTimedOut       = 0x00080000,
        OperationCancelled      = 0x00100000,
        OperationFailed         = 0x00F00000
    };
    static const int MASK_OperationStatus                = 0xFFFF0000;
    static const int MASK_OperationCustomStatusCode      = 0x0000FFFF;
public:
    AbstractOperation(QObject* aObserver = 0, const char* aSlot = 0);
    virtual ~AbstractOperation();
public:
    // implement this function to perform the specific operation
    virtual void execute() = 0;
    /**
      * Getter for the operation status.
      */
    OperationStatus status() const;
    /**
      * Get your custom code.
      */
    int customCode() const;
    /**
      * The id of this operation (by default is its address).
      */
    virtual int id() const;

    QObject* observer();
    const char* callbackMethod();
protected:
    /**
      * This is the first function that should be executed in the execute of the Operation.
      * As parameter put the desired timeout it the default one does not suit
      * that particular operation.
      */
    void started(int aTimeout = KDefaultTimeoutOperation);
    /**
      * When writing a operation, it should be periodically checking this method
      * and, if possible, gracefully stop the ongoing operation should this
      * method return false.
      */
    bool canContinue();
    // status related operations
    /**
      * Set the operation status code to @aStatus.
      */
    void setStatus(OperationStatus aStatus);
    /**
      * Set your custom status code to @aCode
      */
    void setCustomCode(int aCode);
    // just two helper functions to set the status (instead of using directly setStatus)
    /**
      * Set the operation status code to @OperationStatus::OperationSuccess
      */
    void success();
    /**
      * Set the operation status code to @OperationStatus::OperationFailed
      */
    void failed();
    /**
      * Call this function when the operation is finished (whether successfully or not)
      * If this function is not called, the operation will eventually killed
      * by the timeout event.
      */
    void finished();
    /**
      * Function called if the operation has to stop, whether for a timeout or because it has been cancelled.
      * Do any clean up here if needed.
      */
    virtual void cancel();
    /**
      * Function called just before the operation is given back to the thread that created it.
      * Do any clean up of any object created in the execute function (which belong to the worker thread)
      * This is the LAST function of this operation to be called in the worker thread.
      * If no obsverver is present the operation will self descruct (here!).
      */
    virtual void cleanThreadSpecificResources();
    /**
      * Function to get a pointer to a QueueHandler
      * This should be used only if your implementation wants to offer other services
      * to the operation (such as database or network accesss)
      */
    QueueHandler* queueHandler();
private: // defyning methods not to be used by anyone apart from the QueueHandler
    friend class QueueHandler;
    void setQueueHandler(QueueHandler* aQueueHandler);
private:
    QObject* m_observer;
    QByteArray m_slotToBeCalled;

    int m_status;
    QueueHandler* m_queueHandler;
};

#endif // ABSTRACTOPERATION_H
