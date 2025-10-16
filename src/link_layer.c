// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h> // added for memcpy

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256
#define FLAG 0x7E
#define A_TX 0x03
#define A_RX 0x01
#define C_SET 0x03
#define C_UA 0x07

#define STATE_START 0
#define STATE_FLAG_RCV 1
#define STATE_A_RCV 2
#define STATE_C_RCV 3
#define STATE_BCC_OK 4
#define STATE_DATA_ALL 5
#define STATE_BCC2_OK 6
#define STATE_STOP 7

int C_RR = 0;

volatile int STOP = FALSE;
int alarmCount = 0;
int alarmEnabled = FALSE;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
}

void printBufferNamed(const unsigned char *buffer, int size) {
    printf("---- Buffer Dump (%d bytes) ----\n", size);
    for (int i = 0; i < size; i++) {
        unsigned char byte = buffer[i];
        const char *name = NULL;

        // Match special byte names
        switch (byte) {
            case FLAG: name = "FLAG"; break;
            case A_TX: name = "A_TX"; break;
            case A_RX: name = "A_RX"; break;
            case C_UA: name = "C_UA"; break;
            default: name = NULL; break;
        }

        if (name)
            printf("[%02d]: 0x%02X  (%3d)  <-- %s\n", i, byte, byte, name);
        else
            printf("[%02d]: 0x%02X  (%3d)\n", i, byte, byte);
    }
    printf("-------------------------------\n");
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////

int receiveSET(LinkLayer connectionParameters){
    int nBytesBuf = 0;
    int state = STATE_START;
    unsigned char byte;

    while (state != STATE_STOP){
        int res = readByteSerialPort(&byte);
        if (res == 0)
            continue;
        switch (state)
        {
        case STATE_START:
            if (byte == FLAG)
                state = STATE_FLAG_RCV;
            break;

        case STATE_FLAG_RCV:
            if (byte == A_TX)
                state = STATE_A_RCV;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else 
                state = STATE_START;
            break;

        case STATE_A_RCV:
            if (byte == C_SET)
                state = STATE_C_RCV;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else
                state = STATE_START;
            break;

        case STATE_C_RCV:
            if (byte == (A_TX ^ C_SET))
                state = STATE_BCC_OK;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else
                state = STATE_START;
            break;

        case STATE_BCC_OK:
            if (byte == FLAG)
            {
                printf("Recebi o SET\n");
                state = STATE_STOP;
                nBytesBuf += res;
                return 0;
            }
            else
                state = STATE_START;
            break;
        }
    }
    return 1;
}

int sendSET(LinkLayer connectionParameters){
    unsigned char buf[BUF_SIZE] = {0};
    buf[0] = FLAG;
    buf[1] = A_TX;
    buf[2] = C_SET;
    buf[3] = A_TX ^ C_SET;
    buf[4] = FLAG;
    writeBytesSerialPort(buf, 5);
    printf("SET Sent\n");
    sleep(1);
    return 0;
}
int sendUA(LinkLayer connectionParameters){
    unsigned char buf[BUF_SIZE] = {0};
    buf[0] = FLAG;
    buf[1] = A_RX;
    buf[2] = C_UA;
    buf[3] = A_RX ^ C_UA;
    buf[4] = FLAG;
    writeBytesSerialPort(buf, 5);
    printf("UA Sent\n");
    sleep(1);
    return 0;
}
int receiveUA(LinkLayer connectionParameters){
    int nBytesBuf = 0;
    int state = STATE_START;
    unsigned char byte;

    while (state != STATE_STOP && alarmEnabled){
        int res = readByteSerialPort(&byte);
        if (res == 0)
            continue;
        switch (state)
        {
        case STATE_START:
            if (byte == FLAG)
                state = STATE_FLAG_RCV;
            break;

        case STATE_FLAG_RCV:
            if (byte == A_RX)
                state = STATE_A_RCV;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else 
                state = STATE_START;
            break;

        case STATE_A_RCV:
            if (byte == C_UA)
                state = STATE_C_RCV;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else
                state = STATE_START;
            break;

        case STATE_C_RCV:
            if (byte == (A_RX ^ C_UA))
                state = STATE_BCC_OK;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else
                state = STATE_START;
            break;

        case STATE_BCC_OK:
            if (byte == FLAG)
            {
                printf("Ligação estabelecida\n");
                state = STATE_STOP;
                nBytesBuf += res;
                return 0;
            }
            else
                state = STATE_START;
            break;
        }
    }
    return 1;
}


int llopen(LinkLayer connectionParameters)
{
    openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);

    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    printf("Alarm configured\n");

    switch (connectionParameters.role){
    
        case (LlRx):
            if (receiveSET(connectionParameters) == 0){
                sendUA(connectionParameters);
            }
            break; 
            
        case (LlTx):
            while(alarmCount < connectionParameters.nRetransmissions){
                if (alarmEnabled == FALSE)
                {
                    alarm(connectionParameters.timeout);
                    alarmEnabled = TRUE;
                }
                sendSET(connectionParameters);
                if(receiveUA(connectionParameters) == 0){
                    alarmEnabled = FALSE;
                    alarmCount = 0;
                    break;
                }
            }
            if(alarmCount == connectionParameters.nRetransmissions){
                printf("erro de ligação\n");
                return 1;
            }
            break;
        
    }
    return 0;
}

 
int sendIFrame(const unsigned char *buf, int bufSize){
    int iframeLen = bufSize + 6; // FLAG,A,C,BCC1,data,BCC2,FLAG
    unsigned char iframe[BUF_SIZE + 6] = {0}; // safe fixed max
    iframe[0] = FLAG;
    iframe[1] = A_TX;
    iframe[2] = C_RR;
    iframe[3] = iframe[1] ^ iframe[2]; // BCC1
    memcpy(iframe + 4, buf, bufSize);
    unsigned char BCC2 = 0;
    for (int i = 0; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }
    iframe[4 + bufSize] = BCC2;
    iframe[5 + bufSize] = FLAG;

    printBufferNamed(iframe, iframeLen);
    writeBytesSerialPort(iframe, iframeLen);
    printf("IFrame Sent\n");
    sleep(1);
    return 0;
}

int receiveIFRame(int bufSize){
    int d = 0;
    int nBytesBuf = 0;
    int state = STATE_START;
    unsigned char byte;
    unsigned char BCC2;

    while (state != STATE_STOP && alarmEnabled){
        int res = readByteSerialPort(&byte);
        printf("%d\n", state);
        if (res == 0)
            continue;
        switch (state)
        {
        case STATE_START:
            if (byte == FLAG)
                state = STATE_FLAG_RCV;
            break;

        case STATE_FLAG_RCV:
            if (byte == A_RX)
                state = STATE_A_RCV;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else 
                state = STATE_START;
            break;

        case STATE_A_RCV:
            if (byte == C_UA)
                state = STATE_C_RCV;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else
                state = STATE_START;
            break;

        case STATE_C_RCV:
            if (byte == (A_RX ^ C_UA))
                state = STATE_BCC_OK;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else
                state = STATE_START;
            break;

        case STATE_BCC_OK:
            if (d == 0) {
                BCC2 = byte; 
                d++;
            }
            else {
                BCC2 ^= byte;
                d++;
            }
            if(d == bufSize){
                state = STATE_DATA_ALL; 
            }
            break;
        case STATE_DATA_ALL:
            if (byte == BCC2){
                state = STATE_BCC2_OK;
            }
            else state = STATE_START;
            break;
        case STATE_BCC2_OK:
            if (byte == FLAG)
            {
                printf("Ligação estabelecida\n");
                state = STATE_STOP;
                nBytesBuf += res;
                return 0;
            }
            else
                state = STATE_START;
            break;
        }


    }
    return 1;
}
////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize, LinkLayer connectionParameters)
{
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    printf("Alarm configured\n");
    while(alarmCount < connectionParameters.nRetransmissions){
        if (alarmEnabled == FALSE)
        {
            alarm(connectionParameters.timeout);
            alarmEnabled = TRUE;
        }
        sendIFrame(buf, bufSize);
        if(receiveIFRame(bufSize) == 0){
            alarmEnabled = FALSE;
            alarmCount = 0;
            break;
        }
    }
    if(alarmCount == connectionParameters.nRetransmissions){
        printf("erro de ligação\n");
        return 1;
    }
    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose()
{

    return 0;
}
