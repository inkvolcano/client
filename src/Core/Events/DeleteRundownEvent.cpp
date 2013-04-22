#include "DeleteRundownEvent.h"

#include "Global.h"

DeleteRundownEvent::DeleteRundownEvent(int index)
    : QEvent(static_cast<QEvent::Type>(Enum::EventType::DeleteRundown)), index(index)
{
}

int DeleteRundownEvent::getIndex() const
{
    return this->index;
}