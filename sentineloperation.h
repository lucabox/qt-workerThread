#ifndef SENTINELOPERATION_H
#define SENTINELOPERATION_H

#include "abstractoperation.h"

#define KSENTINEL_OPERATION 0

class AbstractOperationObserver;

class SentinelOperation : public AbstractOperation
{
public:
    SentinelOperation();
public: // from AbstractOperation
    void execute();
    int id() const;
};


#endif // SENTINELOPERATION_H
