// Application layer protocol implementation

#include "application_layer.h"

#include <stdio.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connection;
    strcpy(connection.serialPort, serialPort);
    if (strcasecmp(role, "rx") == 0)
    connection.role = LlRx;
    else
    connection.role = LlTx;

    connection.baudRate = baudRate;
    connection.nRetransmissions = nTries;
    connection.timeout = timeout;
    llopen(connection);
    
}
