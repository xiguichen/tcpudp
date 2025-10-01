#include "DataNotifier.h"

void DataNotifier::onDataReceived(unsigned long id)
{
    for (const auto &listener : listeners)
    {
        listener(id);
    }
}

TcpDataAckNotifierSingleton &TcpDataAckNotifierSingleton::getInstance()
{
    static TcpDataAckNotifierSingleton instance;
    return instance;
}
DataNotifier &TcpDataAckNotifierSingleton::getNotifier()
{
    return notifier;
}

void DataNotifier::addListener(const std::function<void(unsigned long)> &listener)
{
    listeners.push_back(listener);
}

