#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <string>

class Configuration {
private:
    static Configuration* instance;
    Configuration() = default; // Private constructor

public:
    static Configuration* getInstance();
public:
    std::string getSocketAddress() const;
    int getPortNumber() const;
};

#endif // CONFIGURATION_H

