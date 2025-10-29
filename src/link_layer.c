// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h> // added for memcpy

// MISC


#define ESC 0x7D
#define ESC_FLAG (FLAG ^ 0x20)  // 0x5E
#define ESC_ESC (ESC ^ 0x20)    // 0x5D

#define _POSIX_SOURCE 1 // POSIX compliant source
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
    printf("UA Sent\n");
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
    printf("Timeout!!!\n");
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
    unsigned char iframe[STUFFED_SIZE] = {0}; 
    int stuffedSize = 0;
    
    // Start with header (no stuffing needed for control bytes)
    iframe[stuffedSize++] = FLAG;
    iframe[stuffedSize++] = A_TX;
    iframe[stuffedSize++] = C;
    iframe[stuffedSize++] = A_TX ^ C;
    
    // Calculate BCC2 before stuffing
    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }
    
    // Apply byte stuffing to data
    for (int i = 0; i < bufSize; i++) {
        if (buf[i] == FLAG) {
            iframe[stuffedSize++] = ESC;
            iframe[stuffedSize++] = ESC_FLAG;
        } else if (buf[i] == ESC) {
            iframe[stuffedSize++] = ESC;
            iframe[stuffedSize++] = ESC_ESC;
        } else {
            iframe[stuffedSize++] = buf[i];
        }
    }
    
    // Apply byte stuffing to BCC2 if needed
    if (BCC2 == FLAG) {
        iframe[stuffedSize++] = ESC;
        iframe[stuffedSize++] = ESC_FLAG;
    } else if (BCC2 == ESC) {
        iframe[stuffedSize++] = ESC;
        iframe[stuffedSize++] = ESC_ESC;
    } else {
        iframe[stuffedSize++] = BCC2;
    }
    
    // End flag
    iframe[stuffedSize++] = FLAG;

    if (stuffedSize > STUFFED_SIZE) {
        printf("ERROR: Stuffed frame too large: %d > %d\n", stuffedSize, STUFFED_SIZE);
        return -1;
    }
    
    writeBytesSerialPort(iframe, stuffedSize);
    printf("IFrame Sent: seq=%d, data=%d bytes, stuffed=%d bytes\n", 
           (C == C_0) ? 0 : 1, bufSize, stuffedSize);
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
    int dataIndex = 0;
    int escapeNext = 0;

    while (state != STATE_STOP) {
        int res = readByteSerialPort(&byte);
        if (res == 0) continue;

        // Debug: log state transitions for first few frames
        static int frame_debug = 0;
        if (frame_debug < 10) {
            printf("State=%d, byte=0x%02X, escapeNext=%d\n", state, byte, escapeNext);
        }

        switch (state) {
            case STATE_START:
                if (byte == FLAG) {
                    dataIndex = 0;
                    escapeNext = 0;
                    state = STATE_FLAG_RCV;
                    if (frame_debug < 10) frame_debug++;
                }
                break;

            case STATE_FLAG_RCV:
                if (byte == A_TX) {
                    state = STATE_A_RCV;
                } else if (byte == FLAG) {
                    // Stay in FLAG_RCV - this is normal
                } else {
                    // Unexpected byte - restart
                    printf("Unexpected byte in FLAG_RCV: 0x%02X, restarting\n", byte);
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
                    // Handle stuffed byte in control field (shouldn't happen normally)
                    unsigned char destuffed = byte ^ 0x20;
                    if (destuffed == expectedBCC1) {
                        BCC2 = 0;
                        dataIndex = 0;
                        escapeNext = 0;
                        state = STATE_BCC_OK;
                    } else {
                        state = STATE_START;
                    }
                    escapeNext = 0;
                } else if (byte == ESC) {
                    escapeNext = 1;
                } else if (byte == expectedBCC1) {
                    BCC2 = 0;
                    dataIndex = 0;
                    escapeNext = 0;
                    state = STATE_BCC_OK;
                } else if (byte == FLAG) {
                    state = STATE_FLAG_RCV;
                } else {
                    state = STATE_START;
                }
                break;

            case STATE_BCC_OK:
                if (escapeNext) {
                    // Destuff this byte
                    unsigned char destuffedByte = byte ^ 0x20;
                    if (dataIndex < bufSize) {
                        packet[dataIndex] = destuffedByte;
                        BCC2 ^= destuffedByte;
                        dataIndex++;
                    }
                    escapeNext = 0;
                } else if (byte == ESC) {
                    escapeNext = 1;
                } else if (byte == FLAG) {
                    // Early FLAG - frame error, restart
                    printf("Early FLAG in data section\n");
                    state = STATE_START;
                } else {
                    // Normal byte
                    if (dataIndex < bufSize) {
                        packet[dataIndex] = byte;
                        BCC2 ^= byte;
                        dataIndex++;
                    } else {
                        // Buffer full, assume we have all data
                        state = STATE_DATA_ALL;
                    }
                }
                
                // Safety check: if we've collected enough data, move to next state
                if (dataIndex >= bufSize) {
                    state = STATE_DATA_ALL;
                }
                break;

            case STATE_DATA_ALL:
                // Handle BCC2 with stuffing
                if (escapeNext) {
                    unsigned char destuffedBCC2 = byte ^ 0x20;
                    if (destuffedBCC2 == BCC2) {
                        state = STATE_BCC2_OK;
                    } else {
                        printf("BCC2 error: expected 0x%02X, got 0x%02X\n", BCC2, destuffedBCC2);
                        return -1;
                    }
                    escapeNext = 0;
                } else if (byte == ESC) {
                    escapeNext = 1;
                } else {
                    if (byte == BCC2) {
                        state = STATE_BCC2_OK;
                    } else {
                        printf("BCC2 error: expected 0x%02X, got 0x%02X\n", BCC2, byte);
                        return -1;
                    }
                }
                break;

            case STATE_BCC2_OK:
                if (byte == FLAG) {
                    printf("IFrame received successfully (seq=%d, data_size=%d)\n", 
                           (controlField == C_0) ? 0 : 1, dataIndex);
                    *C = controlField;
                    state = STATE_STOP;
                    return dataIndex;
                } else {
                    printf("Missing end FLAG, got 0x%02X\n", byte);
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
    else{
        C = C_RR0;
    }
    buf[0] = FLAG;
    buf[1] = A_RX;
    buf[2] = C;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = FLAG;
    writeBytesSerialPort(buf, 5);
    printf("RR Sent\n");
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
    printf("REJ Sent\n");
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

    while (state != STATE_STOP){
        int res = readByteSerialPort(&byte);
        if (res == 0)
            continue;
        switch (state)
        {
        case STATE_START:
            printf("FLAG1: %x ",byte);
            fflush(stdout);
            if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else return -1;
            break;

        case STATE_FLAG_RCV:
            printf("A: %x ",byte);
            fflush(stdout);
            if (byte == A_RX)
                state = STATE_A_RCV;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else 
                return -1;
            break;

        case STATE_A_RCV:
            printf("C: %x ",byte);
            fflush(stdout);
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
            printf("BCC1: %x ",byte);
            fflush(stdout);
            if (byte == (A_RX ^ C))
                state = STATE_BCC_OK;
            else if (byte == FLAG)
                state = STATE_FLAG_RCV;
            else
                return -1;
            break;

        case STATE_BCC_OK:
            printf("FLAG2: %x\n",byte);
            fflush(stdout);
            if (byte == FLAG)
            {
                printf("IFrame foi recebido com sucesso\n");
                state = STATE_STOP;
                nBytesBuf += res;
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

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize, LinkLayer connectionParameters)
{
    static int actualC = 0;
    static int frameCount = 0;
    frameCount++;
    
    printf("[llwrite %d] Starting with seq=%d, size=%d\n", frameCount, actualC, bufSize);

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

    while(alarmCount < 50000){
        if (alarmEnabled == FALSE)
        {
            alarm(connectionParameters.timeout);
            alarmEnabled = TRUE;
        }
        printf("[llwrite %d] Attempt %d, seq=%d\n", frameCount, alarmCount + 1, actualC);
        sendIFrame(buf, bufSize, C);
        printf("SErá????\n");
        int result = receiveResponse();
        printf("[llwrite %d] Response: %d (0=RR0, 1=RR1, 2=REJ0, 3=REJ1)\n", frameCount, result);
        // Handle REJECT - retry immediately without counting as a retransmission
        if ((result == 2 && C == C_0) || (result == 3 && C == C_1)) {
            
            printf("[llwrite %d] REJ received for seq %d - retrying\n", frameCount, actualC);
            // Reset alarm and continue (don't increment alarmCount)
            alarm(0);
            alarmEnabled = FALSE;
            continue;
        }
        // Handle successful RR response
        else if((result == 0 && actualC == 1) || (result == 1 && actualC == 0)){
            printf("[llwrite %d] SUCCESS - advancing seq from %d to %d\n", 
                   frameCount, actualC, invertC(actualC));
            actualC = invertC(actualC);
            alarm(0);
            alarmEnabled = FALSE;
            alarmCount = 0;
            return 0;  // Success
        }
        else{
            printf("[llwrite %d] Unexpected response %d for seq %d\n", 
                   frameCount, result, actualC);
        }
        // For timeout or other errors, the loop will continue and alarmCount will increment
    }
    
    // If we get here, we exceeded the retransmission limit
    if(alarmCount >= connectionParameters.nRetransmissions){
        printf("Error: Maximum retransmissions (%d) reached\n", connectionParameters.nRetransmissions);
        alarm(0);
        alarmEnabled = FALSE;
        return 1;
    }
    
    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
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
    return 0; // Found frame start
}

// Modified llread with better error recovery
int llread(unsigned char *packet) {
    static int expectedC = 0;
    unsigned char C;

    // Try to receive frame with retries
    int max_retries = 3;
    int retry = 0;
    
    while (retry < max_retries) {
        int data_length = receiveIFRame(BUF_SIZE - 6, packet, &C);
        
        if (data_length > 0) {
            int receivedC = (C == C_0) ? 0 : 1;
            
            if (receivedC == expectedC) {
                sendResponse(C);
                expectedC ^= 1;
                return data_length;
            } else if (receivedC == (expectedC ^ 1)) {
                // Duplicate frame
                sendResponse((expectedC == 0) ? C_RR0 : C_RR1);
                printf("Duplicate frame (expected %d, got %d)\n", expectedC, receivedC);
                return -1;
            } else {
                printf("Invalid sequence (expected %d, got %d)\n", expectedC, receivedC);
                sendReject(expectedC ? C_1 : C_0);
                retry++;
            }
        } else {
            // Frame error, try to resynchronize
            printf("Frame error, resynchronizing...\n");
            findNextFrame();
            retry++;
        }
    }
    
    printf("Max retries exceeded in llread\n");
    return -1;
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
