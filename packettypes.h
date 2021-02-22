#ifndef PACKETTYPES_H
#define PACKETTYPES_H
#include <QObject>

#pragma pack(push, 1)

// Fixed Size Packets
#define CONTROL_SIZE            0x10
#define WATCHDOG_SIZE           0x14
#define PING_SIZE               0x15
#define OPENCLOSE_SIZE          0x16
#define RETRANSMIT_RANGE_SIZE   0x18
#define TOKEN_SIZE              0x40
#define STATUS_SIZE             0x50
#define LOGIN_RESPONSE_SIZE     0x60
#define LOGIN_SIZE              0x80
#define CONNINFO_SIZE           0x90
#define CAPABILITIES_SIZE       0xA8

// Variable size packets + payload
#define CIV_SIZE                0x15
#define TXAUDIO_SIZE            0x18
#define DATA_SIZE               0x15

// 0x10 length control packet (connect/disconnect/idle.)
typedef union control_packet {
    struct {
        quint32 len;
        quint16 type;
        quint16 seq;
        quint32 sentid;
        quint32 rcvdid;
    };
    char packet[CONTROL_SIZE];
} *control_packet_t;


// 0x15 length ping packet 
// Also used for the slightly different civ header packet.
typedef union ping_packet {
    struct
    {
        quint32 len;        // 0x00
        quint16 type;       // 0x04
        quint16 seq;        // 0x06
        quint32 sentid;     // 0x08
        quint32 rcvdid;     // 0x0c
        char  reply;        // 0x10
        union { // This contains differences between the send/receive packet
            struct { // Ping
                quint32 time;      // 0x11
            };
            struct { // Send
                quint16 datalen;    // 0x11
                quint16 sendseq;    //0x13
            };
        };

    };
    char packet[PING_SIZE];
} *ping_packet_t, * data_packet_t, data_packet;

// 0x16 length open/close packet
typedef union openclose_packet {
    struct
    {
        quint32 len;        // 0x00
        quint16 type;       // 0x04
        quint16 seq;        // 0x06
        quint32 sentid;     // 0x08
        quint32 rcvdid;     // 0x0c
        quint16 data;       // 0x10
        char unused;        // 0x11
        quint16 sendseq;    //0x13
        char magic;         // 0x15

    };
    char packet[OPENCLOSE_SIZE];
} *startstop_packet_t;


// 0x18 length txaudio packet 
typedef union txaudio_packet {
    struct
    {
        quint32 len;        // 0x00
        quint16 type;       // 0x04
        quint16 seq;        // 0x06
        quint32 sentid;     // 0x08
        quint32 rcvdid;     // 0x0c
        quint16 ident;      // 0x10
        quint16 sendseq;    // 0x12
        quint16 unused;     // 0x14
        quint16 datalen;    // 0x16
    };
    char packet[TXAUDIO_SIZE];
} *txaudio_packet_t;

// 0x18 length retransmit_range packet 
typedef union retransmit_range_packet {
    struct
    {
        quint32 len;        // 0x00
        quint16 type;       // 0x04
        quint16 seq;        // 0x06
        quint32 sentid;     // 0x08
        quint32 rcvdid;     // 0x0c
        quint16 first;      // 0x10
        quint16 second;        // 0x12
        quint16 third;      // 0x14
        quint16 fourth;        // 0x16
    };
    char packet[RETRANSMIT_RANGE_SIZE];
} *retransmit_range_packet_t;


// 0x18 length txaudio packet 
/*            tx[0] = static_cast<quint8>(tx.length() & 0xff);
            tx[1] = static_cast<quint8>(tx.length() >> 8 & 0xff);
            tx[18] = static_cast<quint8>(sendAudioSeq >> 8 & 0xff);
            tx[19] = static_cast<quint8>(sendAudioSeq & 0xff);
            tx[22] = static_cast<quint8>(partial.length() >> 8 & 0xff);
            tx[23] = static_cast<quint8>(partial.length() & 0xff);*/


// 0x40 length token packet
typedef union token_packet {
    struct
    {
        quint32 len;                // 0x00
        quint16 type;               // 0x04
        quint16 seq;                // 0x06
        quint32 sentid;             // 0x08 
        quint32 rcvdid;             // 0x0c
        char unuseda[3];          // 0x10
        quint16 code;               // 0x13
        quint16 res;                // 0x15
        quint16 innerseq;           // 0x17
        char unusedc;             // 0x19
        quint16 tokrequest;         // 0x1a
        quint32 token;              // 0x1c
        char unusedd[16];         // 0x20
        quint32 response;           // 0x30
        char unusede[12];         // 0x34
    };
    char packet[TOKEN_SIZE];
} *token_packet_t;

// 0x50 length login status packet
typedef union status_packet {
    struct
    {
        quint32 len;                // 0x00
        quint16 type;               // 0x04
        quint16 seq;                // 0x06
        quint32 sentid;             // 0x08 
        quint32 rcvdid;             // 0x0c
        char unuseda[3];          // 0x10
        quint16 code;               // 0x13
        quint16 res;                // 0x15
        quint16 innerseq;           // 0x17
        char unusedc;             // 0x19
        quint16 tokrequest;         // 0x1a
        quint32 token;              // 0x1c 
        char unusedd[6];          // 0x20
        quint16 unknown;            // 0x26
        char unusede;             // 0x28
        char unusedf[2];          // 0x29
        char value[5];            // 0x2b
        quint32 error;              // 0x30
        char unusedg[12];         // 0x34
        char disc;                // 0x40
        char unusedh;             // 0x41
        quint32 civport;            // 0x42 // Sent bigendian
        quint32 audioport;          // 0x46 // Sent bigendian
        char unusedi[5];          // 0x4a
    };
    char packet[STATUS_SIZE];
} *status_packet_t;

// 0x60 length login status packet
typedef union login_response_packet {
    struct
    {
        quint32 len;                // 0x00
        quint16 type;               // 0x04
        quint16 seq;                // 0x06
        quint32 sentid;             // 0x08 
        quint32 rcvdid;             // 0x0c
        char unuseda[3];            // 0x10
        quint16 code;               // 0x13
        quint16 res;                // 0x15
        quint16 innerseq;           // 0x17
        char unusedc;               // 0x19
        quint16 tokrequest;         // 0x1a
        quint32 token;              // 0x1c 
        quint16 authstartid;        // 0x20
        char unusedd[14];           // 0x22
        quint32 error;              // 0x30
        char unusede[12];           // 0x34
        char connection[16];        // 0x40
        char unusedf[16];           // 0x50
    };
    char packet[LOGIN_RESPONSE_SIZE];
} *login_response_packet_t;

// 0x80 length login packet
typedef union login_packet {
    struct
    {
        quint32 len;                // 0x00
        quint16 type;               // 0x04
        quint16 seq;                // 0x06
        quint32 sentid;             // 0x08 
        quint32 rcvdid;             // 0x0c
        char unuseda[3];            // 0x10
        quint16 code;               // 0x13
        quint16 res;                // 0x15
        quint16 innerseq;           // 0x17
        char unusedb;               // 0x19
        quint16 tokrequest;         // 0x1a
        quint32 token;              // 0x1c 
        char unusedc[32];           // 0x20
        char username[16];          // 0x40
        char password[16];          // 0x50
        char name[16];              // 0x60
        char unusedf[16];           // 0x70
    };
    char packet[LOGIN_SIZE];
} *login_packet_t;

// 0x90 length conninfo and stream request packet
typedef union conninfo_packet {
    struct
    {
        quint32 len;              // 0x00
        quint16 type;             // 0x04
        quint16 seq;              // 0x06
        quint32 sentid;           // 0x08 
        quint32 rcvdid;           // 0x0c
        char unuseda[3];          // 0x10
        quint16 code;             // 0x13
        quint16 res;              // 0x15
        quint16 innerseq;         // 0x17
        char unusedb;             // 0x19
        quint16 tokrequest;       // 0x1a
        quint32 token;            // 0x1c 
        quint16 authstartid;      // 0x20
        char unusedd[5];          // 0x22
        quint32 resb;             // 0x27
        char identa;              // 0x2b
        quint32 identb;           // 0x2c
        char unusedf[16];         // 0x30
        char name[16];            // 0x40
        char unusedg[16];         // 0x50
        union { // This contains differences between the send/receive packet
            struct { // Receive
                quint32 busy;            // 0x60
                char computer[16];        // 0x64
                char unusedi[16];         // 0x74
                quint32 ipaddress;        // 0x84
                char unusedj[8];          // 0x78
            };
            struct { // Send
                char username[16];    // 0x60 
                char rxenable;        // 0x70
                char txenable;        // 0x71
                char rxcodec;         // 0x72
                char txcodec;         // 0x73
                quint32 rxsample;     // 0x74
                quint32 txsample;     // 0x78
                quint32 civport;      // 0x7c
                quint32 audioport;    // 0x80
                quint32 txbuffer;     // 0x84
                char unusedl[8];      // 0x88
            };
        };
    };
    char packet[CONNINFO_SIZE];
} *conninfo_packet_t;

// 0xA8 length capabilities packet
typedef union capabilities_packet {
    struct
    {
        quint32 len;                // 0x00
        quint16 type;               // 0x04
        quint16 seq;                // 0x06
        quint32 sentid;             // 0x08 
        quint32 rcvdid;             // 0x0c
        char unuseda[3];          // 0x10
        quint16 code;               // 0x13
        quint16 res;                // 0x15
        quint16 innerseq;           // 0x17
        char unusedc;             // 0x19
        quint16 tokrequest;         // 0x1a
        quint32 token;              // 0x1c 
        char unusedd[33];         // 0x20
        char capa;                // 0x41
        char unusede[7];          // 0x42
        quint16 capb;               // 0x49
        char unusedf[2];          // 0x4b
        char capc;                // 0x4d
        quint32 capd;               // 0x4e
        char name[32];            // 0x52
        char audio[32];           // 0x72
        char caparray[10];        // 0x92
        char unusedh[2];          // 0x9c
        quint16 cape;               // 0x9e
        quint16 capf;               // 0xa0
        char unusedi;             // 0xa2
        quint16 capg;               // 0xa3
        char unusedj[3];          // 0xa5
    };
    char packet[CAPABILITIES_SIZE];
} *capabilities_packet_t;




#pragma pack(pop)


#endif // PACKETTYPES_H
