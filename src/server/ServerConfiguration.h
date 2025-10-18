#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <string>

class ServerConfiguration
{
  private:
    static ServerConfiguration *instance;

    // Private constructor
    ServerConfiguration() = default;

  public:
    static ServerConfiguration *getInstance();
    int getPortNumber() const;

};

#endif // CONFIGURATION_H
