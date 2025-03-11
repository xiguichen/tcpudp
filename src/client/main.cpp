#include "Client.h"
#include <Log.h>
using namespace Logger;

int main() {
    Log::getInstance().setLogLevel(LogLevel::ERROR);
    Client client("config.json");
    client.configure();
    return 0;
}
