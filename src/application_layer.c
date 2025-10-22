// Application layer protocol implementation

#include "application_layer.h"

#include <stdio.h>

#define C_START 1
#define C_DATA 2
#define C_END 3

#define T_SIZE 0
#define BUF_SIZE 256
#define dataSize  BUF_SIZE-9



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
        filesizeback/=256;
    }
    buf[2] = L1;
    for (int i = 0; i < L1; i++) {
        int shift = (L1 - 1 - i) * 8;
        buf[3 + i] = filesize / (1 << shift);
        filesize = filesize % (1 << shift);
    }

}   

int getDataPacket(FILE* file, unsigned char * buf, int bytesLeft){
    int nrBytesInside = dataSize;
    if(bytesLeft < dataSize) nrBytesInside = bytesLeft;
    buf[0] = C_DATA; 
    buf[1] = nrBytesInside>>8;
    buf[2] = nrBytesInside;
    
    int bytesRead = fread(buf + 3, 1, nrBytesInside, file);
    if (bytesRead != nrBytesInside) {
        for (int i = bytesRead; i < nrBytesInside; i++) {
            buf[i+3] = 0;
        }
    }
    return nrBytesInside;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connection;
    unsigned char buf[BUF_SIZE-6] = {0};

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
            if (llwrite(buf, BUF_SIZE-6, connection) == 1) {
                fclose(file);
                break;
            }
            printf("Sending file");
            sleep(1);
            fseek(file, 0, SEEK_END);
            int bytesLeft = ftell(file);
            fseek(file, 0, SEEK_SET);
            
            while(bytesLeft > 0){
                int bytesWritten = getDataPacket(file, buf, bytesLeft);
                if (llwrite(buf, BUF_SIZE-6, connection) == 1) {
                    printf("\nError sending data packet\n");
                    fclose(file);
                    break;
                }
                bytesLeft -= bytesWritten;
                printf(".");fflush(stdout);
                if(bytesLeft == 0) {
                    fclose(file);
                    printf("\nFile transmission complete!\n");
                }
            }
            break;

        case (LlRx):
            if (llread(buf) == -1){ break;}
            printf("Receiving file");
            sleep(1);
            int rcvfilesize = 0;
            for (int i = 0; i < buf[2]; i++) {
                rcvfilesize = (rcvfilesize << 8) | buf[3 + i];
            }
            //printf("size:%d\n", rcvfilesize);
            FILE* refile = fopen(filename,"w");
            while(rcvfilesize > 0){
                llread(buf);
                int nrBytesInside = (buf[1] << 8) | buf[2];
                fwrite(buf + 3, 1, nrBytesInside, refile);
                rcvfilesize -= nrBytesInside;
                printf(".");fflush(stdout);
                if(rcvfilesize == 0){
                    fclose(refile);
                    printf("\nFile reception complete!\n");
                }
            }
            break;
    }   
    
    llclose(connection);
}


