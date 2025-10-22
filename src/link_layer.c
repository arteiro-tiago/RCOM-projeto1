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
#define C_RR0 0xAA
#define C_RR1 0xAB
#define C_REJ0 0x54
#define C_REJ1 0x55
#define C_DISC 0x0B
#define C_0 0x00
#define C_1 0x80

#define STATE_START 0
#define STATE_FLAG_RCV 1
#define STATE_A_RCV 2
#define STATE_C_RCV 3
#define STATE_BCC_OK 4
#define STATE_DATA_ALL 5
#define STATE_BCC2_OK 6
#define STATE_STOP 7


volatile int STOP = FALSE;
int alarmCount = 0;
int alarmEnabled = FALSE;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
}

/*void printBufferNamed(const unsigned char *buffer, int size) {
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
} */

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
    //printf("SET Sent\n");
    sleep(0.1);
    return 0;
}
int sendUA(LinkLayer connectionParameters){
    unsigned char A;
    unsigned char buf[BUF_SIZE] = {0};
    
    if (connectionParameters.role == LlTx) {
        A = A_TX;
    } else {
        A = A_RX;
    }

    buf[0] = FLAG;
    buf[1] = A;
    buf[2] = C_UA;
    buf[3] = A ^ C_UA;
    buf[4] = FLAG;
    writeBytesSerialPort(buf, 5);
    //printf("UA Sent\n");
    sleep(0.1);
    return 0;
}
int receiveUA(LinkLayer connectionParameters){
    int nBytesBuf = 0;
    int state = STATE_START;
    unsigned char byte;
    unsigned char A;

    if (connectionParameters.role == LlRx) {
        A = A_TX;
    } else {
        A = A_RX;
    }

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
            if (byte == A)
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
            if (byte == (A ^ C_UA))
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
                    alarm(0);
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

 
int sendIFrame(const unsigned char *buf, int bufSize, unsigned char C){
    int iframeLen = bufSize + 6; 
    unsigned char iframe[BUF_SIZE] = {0}; 
    iframe[0] = FLAG;
    iframe[1] = A_TX;
    iframe[2] = C;
    iframe[3] = iframe[1] ^ iframe[2];
    memcpy(iframe + 4, buf, bufSize);
    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }
    iframe[4 + bufSize] = BCC2;
    iframe[5 + bufSize] = FLAG;

    writeBytesSerialPort(iframe, iframeLen);
    //printf("IFrame Sent\n");
    sleep(0.1);
    return 0;
}

int receiveIFRame(const int bufSize, unsigned char *packet, unsigned char *C){
    if (!packet || bufSize <= 0) return -1;

    int state = STATE_START;
    unsigned char controlField = 0;
    unsigned char byte;
    unsigned char expectedBCC1;
    unsigned char BCC2 = 0;
    int dataIndex = 0;

    while (state != STATE_STOP) {
        int res = readByteSerialPort(&byte);
        if (res == 0)
            continue;

        switch (state) {
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
                controlField = byte;
                expectedBCC1 = A_TX ^ controlField;
                state = STATE_C_RCV;
                break;

            case STATE_C_RCV:
                if (byte == expectedBCC1) {
                    BCC2 = 0;
                    dataIndex = 0;
                    state = STATE_BCC_OK; 
                } else if (byte == FLAG) {
                    state = STATE_FLAG_RCV;
                } else {
                    state = STATE_START;
                }
                break;

            case STATE_BCC_OK:
                packet[dataIndex] = byte;
                dataIndex++;
                BCC2 ^= byte;
                if (dataIndex >= bufSize) {
                    state = STATE_DATA_ALL;
                }
                break;

            case STATE_DATA_ALL:
                if (byte == BCC2) {
                    state = STATE_BCC2_OK;
                } else {
                    state = STATE_START;
                }
                break;

            case STATE_BCC2_OK:
                if (byte == FLAG) {
                    //printf("IFrame received\n");
                    *C = controlField;
                    state = STATE_STOP;
                    return 0;
                } else {
                    state = STATE_START;
                }
                break;
        }
    }
    //printBufferNamed(data, dataIndex);

    return -1;
}

int sendResponse(unsigned char Creceived){
    unsigned char buf[BUF_SIZE] = {0};
    unsigned char C;
    if(Creceived == C_0){
        C = C_RR1;
    }
    else{
        C = C_RR0;
    }
    buf[0] = FLAG;
    buf[1] = A_RX;
    buf[2] = C;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = FLAG;
    writeBytesSerialPort(buf, 5);
    //printf("RR Sent\n");
    sleep(0.1);
    return 0; 
}
int sendReject(unsigned char Creceived){
    unsigned char buf[BUF_SIZE] = {0};
    unsigned char C;
    if(Creceived == C_0){
        C = C_REJ0;
    }
    else{
        C = C_REJ1;
    }
    buf[0] = FLAG;
    buf[1] = A_RX;
    buf[2] = C;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = FLAG;
    writeBytesSerialPort(buf, 5);
    //printf("RR Sent\n");
    sleep(0.1);
    return 0; 
}
int CtoR(unsigned char C){
    switch (C)
    {
    case C_RR0:
        return 0;
        break;
    
    case C_RR1:
        return 1;
        break;
    
    case C_REJ0:
        return 2;
        break;
    
    case C_REJ1:
        return 3;
        break;
    
    default:
        return -1;
        break;
    }
}
int receiveResponse(){
    int nBytesBuf = 0;
    int state = STATE_START;
    unsigned char byte;
    unsigned char C;

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
            
            if (byte == C_RR0 || byte == C_RR1 || byte == C_REJ0 || byte == C_REJ1){
                state = STATE_C_RCV;
                C = byte;
            }
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else
                state = STATE_START;
            break;

        case STATE_C_RCV:
            if (byte == (A_RX ^ C))
                state = STATE_BCC_OK;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else
                state = STATE_START;
            break;

        case STATE_BCC_OK:
            if (byte == FLAG)
            {
                //printf("IFrame foi recebido com sucesso\n");
                state = STATE_STOP;
                nBytesBuf += res;
                return CtoR(C);
            }
            else
                state = STATE_START;
            break;
        }
    }
    return -1;
}

int invertC(int actualC){
    if (actualC == 0) return 1;
    else return 0;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize, LinkLayer connectionParameters)
{
    static int actualC = 0;
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;

    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    unsigned char C;
    if (actualC == 0) C = C_0;
    else C = C_1;

    while(alarmCount < connectionParameters.nRetransmissions){
        if (alarmEnabled == FALSE)
        {
            alarm(connectionParameters.timeout);
            alarmEnabled = TRUE;
        }
        sendIFrame(buf, bufSize, C);
        int result = receiveResponse();
        if((result == 0 && C == C_0) || (result == 1 && C == C_1)){
            C = invertC(C);
            alarm(0);
            alarmEnabled = FALSE;
            alarmCount = 0;
            break;
        }
        else if ((result == 2 && C == C_0) || (result == 3 && C == C_1)){
            continue;
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
    static int expectedC = 0;
    unsigned char C;
    int got = receiveIFRame(BUF_SIZE - 6, packet, &C);

    if (got == 0) {
        int receivedC;
        if (C == C_0) receivedC = 0;
        else receivedC = 1; 
        if (receivedC == expectedC) {
            sendResponse(C); // RR para o próximo
            expectedC ^= 1; // alterna o esperado
            return 0;
        } else if (receivedC == (expectedC ^ 1)) {
            // Frame duplicado — já recebido, reenviar RR anterior
            sendResponse((expectedC == 0) ? C_RR0 : C_RR1);
            return -1;
        }
        else{
            return -1;
        }
    } else {
        sendReject(expectedC ? C_1 : C_0);
        return -1;
    }
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////


int sendDISC(LinkLayer connectionParameters) {
    unsigned char buf[BUF_SIZE] = {0};
    buf[0] = FLAG;
    if (connectionParameters.role == LlTx) {
        buf[1] = A_TX;
    } else {
        buf[1] = A_RX;
    }
    buf[2] = C_DISC;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = FLAG;
    writeBytesSerialPort(buf, 5);
    printf("DISC Sent\n");
    sleep(0.1);
    return 0;
}

int receiveDISC(LinkLayer connectionParameters) {
    int state = STATE_START;
    unsigned char byte;
    unsigned char A;

    if (connectionParameters.role == LlTx) {
        A = A_RX;
    } else {
        A = A_TX;
    }

    while (state != STATE_STOP) {
        int res = readByteSerialPort(&byte);
        if (res == 0) 
            continue;

        switch (state) {
            case STATE_START:
                if (byte == FLAG) 
                state = STATE_FLAG_RCV;
                break;

            case STATE_FLAG_RCV:
                if (byte == A)    
                    state = STATE_A_RCV;
                else if (byte == FLAG) 
                    state = STATE_FLAG_RCV;
                else 
                    state = STATE_START;
                break;

            case STATE_A_RCV:
                if (byte == C_DISC) 
                    state = STATE_C_RCV;
                else if (byte == FLAG) 
                    state = STATE_FLAG_RCV;
                else 
                    state = STATE_START;
                break;

            case STATE_C_RCV:
                if (byte == (A ^ C_DISC)) 
                    state = STATE_BCC_OK;  
                else if (byte == FLAG) 
                    state = STATE_FLAG_RCV;
                else 
                    state = STATE_START;
                break;

            case STATE_BCC_OK:
                if (byte == FLAG) {
                    printf("Recebi o DISC\n");
                    state = STATE_STOP;
                    return 0;
                }
                else 
                    state = STATE_START;
                break;
        }
    }
    return 1;
}

int llclose(LinkLayer connectionParameters) {
    printf("Closing\n");

    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }


    alarmCount = 0;
    alarmEnabled = FALSE;
    alarm(0);

    int success = 0;
    
    switch (connectionParameters.role) {
        case (LlRx):
            while(alarmCount < connectionParameters.nRetransmissions) {
                if (alarmEnabled == FALSE) {
                    alarm(connectionParameters.timeout);
                    alarmEnabled = TRUE;
                }

                if (receiveDISC(connectionParameters) == 0) {
                    sendDISC(connectionParameters);
                    if (receiveUA(connectionParameters) == 0) {
                        alarmEnabled = FALSE;
                        alarmCount = 0;
                        success = 1;
                        break;
                    }
                }
            }

            if(alarmCount == connectionParameters.nRetransmissions) {
                printf("erro ao fechar\n");
                return 1;
            }
            break;

        case (LlTx):
            while(alarmCount < connectionParameters.nRetransmissions) {
                if (alarmEnabled == FALSE) {
                    alarm(connectionParameters.timeout);
                    alarmEnabled = TRUE;
                }

                sendDISC(connectionParameters);

                if (receiveDISC(connectionParameters) == 0) {
                    sendUA(connectionParameters);
                    alarmEnabled = FALSE;
                    alarmCount = 0;
                    success = 1;
                    break;
                }
            }
            
            if(alarmCount == connectionParameters.nRetransmissions) {
                printf("erro ao fechar\n");
                return 1;
            }
            break;
    }

    alarmEnabled = FALSE;
    alarm(0);
    
    closeSerialPort();
    
    if (success) {
        printf("Connection closed successfully.\n");
        return 0;
    } else {
        printf("Error closing connection.\n");
        return 1;
    }
}

