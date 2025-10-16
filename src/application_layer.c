// Application layer protocol implementation

#include "application_layer.h"

#include <stdio.h>

#define C_START 1
#define C_DATA 2
#define C_END 3

#define T_SIZE 0
#define BUF_SIZE 256


void getStartControlPacket(FILE* file, unsigned char * buf){
    fseek(file,0,SEEK_END);
    int filesize = ftell(file);
    fseek(file,0,SEEK_SET);
    buf[0] = C_START; 
    buf[1] = T_SIZE;
    int filesizeback = filesize;
    int L1 = 0;
    while(filesizeback>0){
        L1++;
        filesizeback-=256;
    }
    buf[2] = L1;
}   

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connection;
    unsigned char buf[BUF_SIZE];

    strcpy(connection.serialPort, serialPort);    
    if (strcasecmp(role, "rx") == 0) {
        connection.role = LlRx;
    }
    else {
        connection.role = LlTx;
    }
    
    connection.baudRate = baudRate;
    connection.nRetransmissions = nTries;
    connection.timeout = timeout;
    
    if (llopen(connection) == 1){
        return;
    }
    switch (connection.role){
        case (LlTx):
            FILE* file = fopen(filename,"rb");
            getStartControlPacket(file, buf);

            llwrite(buf, BUF_SIZE-6, connection);

            break;
        case (LlRx):
            //TO_DO
            break;
    }    
}


