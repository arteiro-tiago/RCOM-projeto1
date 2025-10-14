// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256
#define FLAG 0x7E
#define A 0x03
#define C 0x03
#define BCC1 A ^ C
#define SENDER 0x03
#define RECEIVER 0x01
#define SET 0x03
#define UA 0x07

#define STATE_START 0
#define STATE_FLAG_RCV 1
#define STATE_A_RCV 2
#define STATE_C_RCV 3
#define STATE_BCC_OK 4
#define STATE_STOP 5

volatile int STOP = FALSE;
////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    int nBytesBuf = 0;
    unsigned char frame[BUF_SIZE] = {0};

    unsigned char uaframe[BUF_SIZE] = {0};
    int start = FALSE;
    int state = STATE_START;
    int nSets = 0;
    unsigned char byte;
    unsigned char buf[BUF_SIZE] = {0};
    openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);

    int flag = 1;
    ////////////////READER///////////////
    printf("%d", connectionParameters.role);
    switch (connectionParameters.role)
    {
    case (LlRx):
        printf("sou o readerrrr!");
        int res = readByteSerialPort(&byte);
        while (flag)
        {
            if (res == 0)
                continue;
            switch (state)
            {
            case STATE_START:
                if (byte == FLAG)
                    state = STATE_FLAG_RCV;
                break;

            case STATE_FLAG_RCV:
                if (byte == SENDER)
                    state = STATE_A_RCV;
                else if (byte != FLAG)
                    state = STATE_START;
                break;

            case STATE_A_RCV:
                if (byte == SET)
                    state = STATE_C_RCV;
                else if (byte == FLAG)
                    state = STATE_FLAG_RCV;
                else
                    state = STATE_START;
                break;

            case STATE_C_RCV:
                if (byte == (SENDER ^ SET))
                    state = STATE_BCC_OK;
                else if (byte == FLAG)
                    state = STATE_FLAG_RCV;
                else
                    state = STATE_START;
                break;

            case STATE_BCC_OK:
                if (byte == FLAG)
                {
                    printf("recebi um set!\n");
                    flag = 0;
                    nBytesBuf += res;
                }
                else
                {
                    state = STATE_START;
                }
                break;
            }
        }
        break; 

        ////////////////WRITER///////////////

    case (LlTx):
        buf[0] = FLAG;
        buf[1] = A;
        buf[2] = C;
        buf[3] = BCC1;
        buf[4] = FLAG;

        int bytesSent = writeBytesSerialPort(buf, 5);
        printf("Frame Sent\n");
        sleep(1);
        break;
    
    }
    return 0;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO: Implement this function

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
