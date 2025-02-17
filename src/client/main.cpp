#include "Client.h"

int main() {
    Client client("config.json");
    client.configure();
    return 0;
}
