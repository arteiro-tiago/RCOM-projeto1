// Application layer protocol implementation

#include "application_layer.h"

#include <stdio.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connection;
    strcpy(connection.serialPort, serialPort);
    if(strcmp(role, "tx") == 0){
        connection.role=LlTx;
    }
    else{
        connection.role=LlRx;
    }
    connection.baudRate = baudRate;
    connection.nRetransmissions = nTries;
    connection.timeout = timeout;
    llopen(connection);
    
}
