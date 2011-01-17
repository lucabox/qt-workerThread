#include "abstractoperationobserver.h"

#include "activelogs.h"
#ifdef ABSTRACT_OPERATION_HANDLER
    #define ENABLE_LOG_MACROS
#endif
#include "logmacros.h"

AbstractOperationObserver::AbstractOperationObserver(QObject* aParent)
    : QObject(aParent)
{
}

void AbstractOperationObserver::handledOperationFinished(void* aOperation) {
    DEBUG_ENTER_FN();
    AbstractOperation* operation = reinterpret_cast<AbstractOperation*>(aOperation);
    handledOperationFinished(operation);
    DEBUG_EXIT_FN();
}
