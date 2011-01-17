#include "sentineloperation.h"
#include "abstractoperationobserver.h"

SentinelOperation::SentinelOperation() : AbstractOperation()
{
}

void SentinelOperation::execute()
{
}

int SentinelOperation::id() const
{
    return KSENTINEL_OPERATION;
};
