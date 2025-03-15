#include "Client.h"
#include <Log.h>
using namespace Logger;

int main() {
    Log::getInstance().setLogLevel(LogLevel::LOG_ERROR);
    while(true)
    {
        Log::getInstance().info("Starting Client");
        Client client("config.json");
        client.configure();
    }
    return 0;
}
