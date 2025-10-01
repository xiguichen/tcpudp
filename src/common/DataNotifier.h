#include <functional>
#include <vector>
class DataNotifier
{
public:
  void onDataReceived(unsigned long id);
  void addListener(const std::function<void(unsigned long)> &listener);

private:
    std::vector<std::function<void(unsigned long)>> listeners;

};


class TcpDataAckNotifierSingleton
{
public:
  static TcpDataAckNotifierSingleton &getInstance();

  DataNotifier &getNotifier();

private:
    TcpDataAckNotifierSingleton() = default;
    ~TcpDataAckNotifierSingleton() = default;

    DataNotifier notifier;
};
