// application_layer_fixed.c
// Corrections applied to fix incomplete reception and proper llclose handling (no goto).

#include "application_layer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define C_START 1
#define C_DATA 2
#define C_END 3

#define T_SIZE 0
#define BUF_SIZE 256
#define dataSize (BUF_SIZE - 9)

#define WAITING_START 1
#define RECEIVING_DATA 2
#define WAITING_END 3
#define COMPLETE 4

void getStartControlPacket(FILE *file, unsigned char *buf)
{
    fseek(file, 0, SEEK_END);
    int filesize = ftell(file);
    fseek(file, 0, SEEK_SET);
    memset(buf, 0, BUF_SIZE);
    buf[0] = C_START;
    buf[1] = T_SIZE;
    int filesizeback = filesize;
    int L1 = 0;
    if (filesize == 0) L1 = 1;
    while (filesizeback > 0)
    {
        L1++;
        filesizeback /= 256;
    }
    buf[2] = L1;
    for (int i = 0; i < L1; i++)
    {
        int shift = (L1 - 1 - i) * 8;
        buf[3 + i] = (filesize >> shift) & 0xFF;
    }
}

void getEndControlPacket(FILE *file, unsigned char *buf)
{
    fseek(file, 0, SEEK_END);
    int filesize = ftell(file);
    fseek(file, 0, SEEK_SET);
    memset(buf, 0, BUF_SIZE);
    buf[0] = C_END;
    buf[1] = T_SIZE;
    int filesizeback = filesize;
    int L1 = 0;
    if (filesize == 0) L1 = 1;
    while (filesizeback > 0)
    {
        L1++;
        filesizeback /= 256;
    }
    buf[2] = L1;
    for (int i = 0; i < L1; i++)
    {
        int shift = (L1 - 1 - i) * 8;
        buf[3 + i] = (filesize >> shift) & 0xFF;
    }
}

int getDataPacket(FILE *file, unsigned char *buf, int bytesLeft)
{
    int nrBytesInside = dataSize;
    if (bytesLeft < dataSize)
        nrBytesInside = bytesLeft;
    buf[0] = C_DATA;
    buf[1] = (nrBytesInside >> 8) & 0xFF;
    buf[2] = nrBytesInside & 0xFF;

    int bytesRead = fread(buf + 3, 1, nrBytesInside, file);
    if (bytesRead != nrBytesInside)
    {
        for (int i = bytesRead; i < nrBytesInside; i++)
        {
            buf[i + 3] = 0;
        }
    }
    return nrBytesInside;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connection;
    unsigned char buf[BUF_SIZE] = {0};

    strcpy(connection.serialPort, serialPort);
    if (strcasecmp(role, "rx") == 0)
    {
        connection.role = LlRx;
    }
    else
    {
        connection.role = LlTx;
    }

    connection.baudRate = baudRate;
    connection.nRetransmissions = nTries;
    connection.timeout = timeout;

    if (llopen(connection) == 1)
    {
        return;
    }

    if (connection.role == LlTx)
    {
        FILE *file = fopen(filename, "rb");
        if (!file)
        {
            perror("fopen");
            llclose(connection);
            return;
        }

        getStartControlPacket(file, buf);
        if (llwrite(buf, BUF_SIZE - 6, connection) == 1)
        {
            fclose(file);
            llclose(connection);
            return;
        }
        printf("Sending file");
        fflush(stdout);

        fseek(file, 0, SEEK_END);
        int bytesLeft = ftell(file);
        fseek(file, 0, SEEK_SET);

        while (bytesLeft > 0)
        {
            int bytesWritten = getDataPacket(file, buf, bytesLeft);
            int writeResult = llwrite(buf, BUF_SIZE - 6, connection);
            
            if (writeResult == 1)  // Fatal error
            {
                printf("\nFatal error sending data packet after %d retries\n", connection.nRetransmissions);
                fclose(file);
                llclose(connection);
                return;
            }
            else if (writeResult == 0)  // Success
            {
                bytesLeft -= bytesWritten;
                printf(".");
                fflush(stdout);
            }
            // If writeResult == -1 (REJ received), we'll retry the same packet
            // Note: You'll need to modify llwrite to return -1 for REJ cases
        }
        printf("\nFile transmission complete!\n");

        getEndControlPacket(file, buf);
        llwrite(buf, BUF_SIZE - 6, connection);

        fclose(file);
        llclose(connection);
        printf("Closing file and connection.\n");
    }
    else if (connection.role == LlRx)
    {
        int readvalue = 0;
        FILE *refile = NULL;
        int state = WAITING_START;
        int rcvfilesize = 0;
        int done = 0;

        while (!done)
        {
            readvalue = llread(buf);
            if (readvalue == -1)
                continue;

            switch (state)
            {
            case WAITING_START:
                if (buf[0] == C_START)
                {
                    printf("Receiving file");
                    fflush(stdout);

                    rcvfilesize = 0;
                    int L1 = buf[2];
                    for (int i = 0; i < L1; i++)
                        rcvfilesize = (rcvfilesize << 8) | buf[3 + i];

                    refile = fopen(filename, "wb");
                    if (!refile)
                    {
                        perror("fopen");
                        done = 1;
                        break;
                    }
                    state = RECEIVING_DATA;
                }
                break;

            case RECEIVING_DATA:
                if (buf[0] == C_DATA)
                {
                    int nrBytesInside = (buf[1] << 8) | buf[2];
                    fwrite(buf + 3, 1, nrBytesInside, refile);
                    rcvfilesize -= nrBytesInside;
                    printf(".");
                    fflush(stdout);
                    if (rcvfilesize <= 0)
                    {
                        fclose(refile);
                        refile = NULL;
                        printf("\nFile reception complete!\n");
                        state = WAITING_END;
                    }
                }
                break;

            case WAITING_END:
                if (buf[0] == C_END)
                {
                    done = 1;
                }
                break;
            }
        }

        if (refile)
            fclose(refile);
        llclose(connection);
        //printf("\nConnection closed successfully.\n");
    }
}
