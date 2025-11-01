#include "link_layer.h"
#include "serial_port.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define ESC 0x7D
#define ESC_FLAG (FLAG ^ 0x20)
#define ESC_ESC (ESC ^ 0x20)
#define _POSIX_SOURCE 1
#define BUF_SIZE 256
#define STUFFED_SIZE 512
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

int receiveSET(LinkLayer connectionParameters){
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
int sendSET(LinkLayer connectionParameters){
    unsigned char buf[BUF_SIZE] = {0};
    buf[0] = FLAG;
    buf[1] = A_TX;
    buf[2] = C_SET;
    buf[3] = A_TX ^ C_SET;
    buf[4] = FLAG;
    writeBytesSerialPort(buf, 5);
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
    sleep(0.1);
    return 0;
}
int receiveUA(LinkLayer connectionParameters){
    int state = STATE_START;
    unsigned char byte;
    unsigned char A;

    if (connectionParameters.role == LlRx) {
        A = A_TX;
    } else {
        A = A_RX;
    }

    while (state != STATE_STOP && alarmEnabled == TRUE){
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
                state = STATE_STOP;
                return 0;
            }
            else
                state = STATE_START;
            break;
        }
    }
    printf("Timeout: No response from receiver\n");
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
                printf("Error: Connection failed after %d retries\n", connectionParameters.nRetransmissions);
                return 1;
            }
            break;
        
    }
    return 0;
}

 
int sendIFrame(const unsigned char *buf, int bufSize, unsigned char C){
    unsigned char iframe[STUFFED_SIZE] = {0}; 
    
    iframe[0] = FLAG;
    iframe[1] = A_TX;
    iframe[2] = C;
    iframe[3] = A_TX ^ C;

    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }
    
    int stuffedSize = 4;
    for (int i = 0; i < bufSize; i++) {
        if (buf[i] == FLAG) {
            iframe[stuffedSize] = ESC;
            stuffedSize++;
            iframe[stuffedSize] = ESC_FLAG;
            stuffedSize++;
        } else if (buf[i] == ESC) {
            iframe[stuffedSize] = ESC;
            stuffedSize++;
            iframe[stuffedSize] = ESC_ESC;
            stuffedSize++;
        } else {
            iframe[stuffedSize] = buf[i];
            stuffedSize++;
        }
    }
    
    if (BCC2 == FLAG) {
        iframe[stuffedSize] = ESC;
        stuffedSize++;
        iframe[stuffedSize] = ESC_FLAG;
        stuffedSize++;
    } else if (BCC2 == ESC) {
        iframe[stuffedSize] = ESC;
        stuffedSize++;
        iframe[stuffedSize] = ESC_ESC;
        stuffedSize++;
    } else {
        iframe[stuffedSize] = BCC2;
        stuffedSize++;
    }
    
    iframe[stuffedSize] = FLAG;
    stuffedSize++;

    if (stuffedSize > STUFFED_SIZE) {
        printf("ERROR: Stuffed frame too large\n");
        return -1;
    }
    
    writeBytesSerialPort(iframe, stuffedSize);
    sleep(0.1);
    return 0;
}

int receiveIFRame(const int bufSize, unsigned char *packet, unsigned char *C) {
    if (!packet || bufSize <= 0) return -1;

    int state = STATE_START;
    unsigned char controlField = 0;
    unsigned char byte;
    unsigned char expectedBCC1;
    unsigned char BCC2 = 0;
    int dataIndex;
    int escapeNext = FALSE;
    unsigned char destuffed;

    while (state != STATE_STOP) {
        int res = readByteSerialPort(&byte);
        if (res == 0) continue;

        switch (state) {
            case STATE_START:
                if (byte == FLAG) {
                    dataIndex = 0;
                    escapeNext = 0;
                    state = STATE_FLAG_RCV;
                }
                break;

            case STATE_FLAG_RCV:
                if (byte == A_TX) {
                    state = STATE_A_RCV;
                } 
                else if (byte == FLAG) {} 
                else {
                    state = STATE_START;
                }
                break;

            case STATE_A_RCV:
                controlField = byte;
                expectedBCC1 = A_TX ^ controlField;
                state = STATE_C_RCV;
                break;

            case STATE_C_RCV:
                if (escapeNext) {
                    if (byte == ESC_FLAG){
                        destuffed = FLAG;
                    }
                    else if (byte == ESC_ESC){
                        destuffed = ESC;
                    }
                    if (destuffed == expectedBCC1) {
                        BCC2 = 0;
                        dataIndex = 0;
                        escapeNext = FALSE;
                        state = STATE_BCC_OK;
                    } 
                    else {
                        state = STATE_START;
                    }
                    escapeNext = FALSE;
                } 
                else if (byte == ESC) {
                    escapeNext = TRUE;
                }
                else if (byte == expectedBCC1) {
                    BCC2 = 0;
                    dataIndex = 0;
                    escapeNext = FALSE;
                    state = STATE_BCC_OK;
                } 
                else if (byte == FLAG) {
                    state = STATE_FLAG_RCV;
                } 
                else {
                    printf("BCC1 error: Packet corrupted, sending REJ\n");
                    return -1;
                }
                break;

            case STATE_BCC_OK:
                if (escapeNext) {
                    if (byte == ESC_FLAG){
                        destuffed = FLAG;
                    }
                    else if (byte == ESC_ESC){
                        destuffed = ESC;
                    }
                    if (dataIndex < bufSize) {
                        packet[dataIndex] = destuffed;
                        BCC2 ^= destuffed;
                        dataIndex++;
                    }
                    escapeNext = FALSE;
                } else if (byte == ESC) {
                    escapeNext = TRUE;
                } else if (byte == FLAG) {
                    state = STATE_START;
                } else {
                    if (dataIndex < bufSize) {
                        packet[dataIndex] = byte;
                        BCC2 ^= byte;
                        dataIndex++;
                    } else {
                        state = STATE_DATA_ALL;
                    }
                }
                
                if (dataIndex >= bufSize) {
                    state = STATE_DATA_ALL;
                }
                break;

            case STATE_DATA_ALL:
                if (escapeNext) {
                    if (byte == ESC_FLAG){
                        destuffed = FLAG;
                    }
                    else if (byte == ESC_ESC){
                        destuffed = ESC;
                    }
                    if (destuffed == BCC2) {
                        state = STATE_BCC2_OK;
                    } else {
                        printf("BCC2 error: Packet corrupted, sending REJ\n");
                        return -1;
                    }
                    escapeNext = FALSE;
                } else if (byte == ESC) {
                    escapeNext = TRUE;
                } else {
                    if (byte == BCC2) {
                        state = STATE_BCC2_OK;
                    } else {
                        printf("BCC2 error: Packet corrupted, sending REJ\n");
                        return -1;
                    }
                }
                break;

            case STATE_BCC2_OK:
                if (byte == FLAG) {
                    *C = controlField;
                    state = STATE_STOP;
                    return dataIndex;
                } else {
                    state = STATE_START;
                }
                break;
        }
    }

    return -1;
}

int sendResponse(unsigned char Creceived){
    unsigned char buf[BUF_SIZE] = {0};
    unsigned char C;
    if(Creceived == C_0){
        C = C_RR1;
    }
    else if (Creceived == C_1){
        C = C_RR0;
    }
    buf[0] = FLAG;
    buf[1] = A_RX;
    buf[2] = C;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = FLAG;
    writeBytesSerialPort(buf, 5);
    sleep(0.1);
    return 0; 
}

int sendReject(unsigned char Creceived){
    unsigned char buf[BUF_SIZE] = {0};
    unsigned char C;
    if(Creceived == C_0){
        C = C_REJ0;
    }
    else if (Creceived == C_1){
        C = C_REJ1;
    }
    buf[0] = FLAG;
    buf[1] = A_RX;
    buf[2] = C;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = FLAG;
    writeBytesSerialPort(buf, 5);
    fflush(stdout);
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
    int state = STATE_START;
    unsigned char byte;
    unsigned char C;

    while (state != STATE_STOP){
        int res = readByteSerialPort(&byte);
        if (res == 0)
            continue;
        switch (state)
        {
        case STATE_START:
            if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else return -1;
            break;

        case STATE_FLAG_RCV:
            if (byte == A_RX)
                state = STATE_A_RCV;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else 
                return -1;
            break;

        case STATE_A_RCV:
            if (byte == C_RR0 || byte == C_RR1 || byte == C_REJ0 || byte == C_REJ1){
                state = STATE_C_RCV;
                C = byte;
            }
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else
                return -1;
            break;

        case STATE_C_RCV:
            if (byte == (A_RX ^ C))
                state = STATE_BCC_OK;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else
                return -1;
            break;

        case STATE_BCC_OK:
            if (byte == FLAG)
            {
                state = STATE_STOP;
                return CtoR(C);
            }
            else
                return -1;
            break;
        }
    }
    return -1;
}

int invertC(int actualC){
    if (actualC == 0) return 1;
    else return 0;
}


int llwrite(const unsigned char *buf, int bufSize, LinkLayer connectionParameters)
{
    static int actualC = 0;
    static int frameCount = 0;
    frameCount++;
    
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;

    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    unsigned char C;
    if (actualC == 0) C = C_0;
    else if (actualC == 1) C = C_1;

    while(alarmCount < connectionParameters.nRetransmissions){
        if (alarmEnabled == FALSE)
        {
            alarm(connectionParameters.timeout);
            alarmEnabled = TRUE;
        }

        sendIFrame(buf, bufSize, C);
        int result = receiveResponse();
        
        if ((result == 2 && C == C_0) || (result == 3 && C == C_1)) {
            alarm(0);
            alarmEnabled = FALSE;
            printf("Last frame rejected\n");
            continue;
        }

        else if((result == 0 && actualC == 1) || (result == 1 && actualC == 0)){
            actualC = invertC(actualC);
            alarm(0);
            alarmEnabled = FALSE;
            alarmCount = 0;
            return bufSize; 
        }
        else {
            printf("Timeout: No response (frame %d: attempt %d)\n", frameCount, alarmCount);
        }
    }
    
    if(alarmCount >= connectionParameters.nRetransmissions){
        printf("Error: Maximum retransmissions (%d) reached\n", connectionParameters.nRetransmissions);
        alarm(0);
        alarmEnabled = FALSE;
        return -1;
    }
    
    return -1;
}


int findNextFrame() {
    unsigned char byte;
    int flags_found = 0;
    
    while (flags_found < 2) {
        int res = readByteSerialPort(&byte);
        if (res == 0) continue;
        
        if (byte == FLAG) {
            flags_found++;
        } else {
            flags_found = 0;
        }
    }
    return 0;
}

int llread(unsigned char *packet) {
    static int expectedC = 0;
    unsigned char C;
    int receivedC;
    
    while (TRUE) {
        int data_length = receiveIFRame(BUF_SIZE - 6, packet, &C);
        
        if (data_length > 0) {
            if (C == C_0){
                receivedC = 0;
            }
            else if (C == C_1){
                receivedC = 1;
            }
            
            if (receivedC == expectedC) {
                sendResponse(C);
                expectedC = invertC(expectedC);
                return data_length;
            }
            else if (receivedC == invertC(expectedC)) {
                if(expectedC == 0) sendResponse(C_RR0);
                if(expectedC == 1) sendResponse(C_RR1);
                return -1;
            } else {
                printf("Invalid sequence number\n");
                if(expectedC == 0) sendReject(C_0);
                if(expectedC == 1) sendReject(C_1);
            }
        }
        else if (data_length == -1) {
            if (expectedC == 0)
                sendReject(C_0);
            else
                sendReject(C_1);
        }
        else {
            findNextFrame();
        }
    }

    return -1;
}


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
                printf("Error: Failed to close connection\n");
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
                printf("Error: Failed to close connection\n");
                return 1;
            }
            break;
    }

    alarmEnabled = FALSE;
    alarm(0);
    
    closeSerialPort();
    
    if (success) {
        return 0;
    } else {
        return 1;
    }
}
