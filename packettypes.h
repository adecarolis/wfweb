#ifndef PACKETTYPES_H
#define PACKETTYPES_H
#include <QObject>

// Various settings used by both client and server
#define PURGE_SECONDS 10
#define TOKEN_RENEWAL 60000
#define PING_PERIOD 500
#define IDLE_PERIOD 100
#define AREYOUTHERE_PERIOD 500
#define WATCHDOG_PERIOD 500             
#define RETRANSMIT_PERIOD 100           // How often to attempt retransmit
#define LOCK_PERIOD 10                  // How long to try to lock mutex (ms)
#define STALE_CONNECTION 15             // Not heard from in this many seconds
#define BUFSIZE 500 // Number of packets to buffer
#define MAX_MISSING 50 // More than this indicates serious network problem 
#define AUDIO_PERIOD 20 
#define GUIDLEN 16


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
#define CAPABILITIES_SIZE       0x42
#define RADIO_CAP_SIZE          0x66

// Variable size packets + payload
#define CIV_SIZE                0x15
#define AUDIO_SIZE            0x18
#define DATA_SIZE               0x15

// Yaesu specific defines
#define YAESU_MAX_FRAME_SIZE 16640

#pragma pack(push)
#pragma pack(1)

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


// 0x14 length watchdog packet
typedef union watchdog_packet {
    struct {
        quint32 len;        // 0x00
        quint16 type;       // 0x04
        quint16 seq;        // 0x06
        quint32 sentid;     // 0x08
        quint32 rcvdid;     // 0x0c
        quint16  secondsa;        // 0x10
        quint16  secondsb;        // 0x12
    };
    char packet[WATCHDOG_SIZE];
} *watchdog_packet_t;


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
        quint8  reply;        // 0x10
        union { // This contains differences between the send/receive packet
            struct { // Ping packet
                quint32 time;      // 0x11 (uptime of device)
            };
            struct { // CIV header packet
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


// 0x18 length audio packet 
typedef union audio_packet {
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
    char packet[AUDIO_SIZE];
} *audio_packet_t;

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
        quint32 payloadsize;      // 0x10
        quint8 requestreply;      // 0x14
        quint8 requesttype;       // 0x15
        quint16 innerseq;         // 0x16
        char unusedb[2];          // 0x18
        quint16 tokrequest;         // 0x1a
        quint32 token;              // 0x1c
        union {
            struct {
                quint16 authstartid;    // 0x20
                char unusedg2[2];       // 0x22
                quint16 resetcap;       // 0x24
                char unusedg1;          // 0x26
                quint16 commoncap;      // 0x27
                char unusedh;           // 0x29
                quint8 macaddress[6];   // 0x2a
            };
            quint8 guid[GUIDLEN];                  // 0x20
        };
        quint32 response;           // 0x30
        char unusede[12];           // 0x34
    };
    char packet[TOKEN_SIZE];
} *token_packet_t;

// 0x50 length login status packet
typedef union status_packet {
    struct
    {
        quint32 len;                // 0x00         0
        quint16 type;               // 0x04         4
        quint16 seq;                // 0x06         6
        quint32 sentid;             // 0x08         8
        quint32 rcvdid;             // 0x0c         12
        quint32 payloadsize;      // 0x10           18
        quint8 requestreply;      // 0x14           19
        quint8 requesttype;       // 0x15           20
        quint16 innerseq;         // 0x16           22
        char unusedb[2];          // 0x18
        quint16 tokrequest;         // 0x1a
        quint32 token;              // 0x1c 
        union {
            struct {
                quint16 authstartid;    // 0x20
                char unusedd[5];        // 0x22
                quint16 commoncap;      // 0x27
                char unusede;           // 0x29
                quint8 macaddress[6];     // 0x2a
            };
            quint8 guid[GUIDLEN];                  // 0x20
        };
        quint32 error;             // 0x30
        char unusedg[12];         // 0x34
        char disc;                // 0x40
        char unusedh;             // 0x41
        quint16 civport;          // 0x42 // Sent bigendian
        quint16 unusedi;          // 0x44 // Sent bigendian
        quint16 audioport;        // 0x46 // Sent bigendian
        char unusedj[7];          // 0x49
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
        quint32 payloadsize;      // 0x10
        quint8 requestreply;      // 0x14
        quint8 requesttype;       // 0x15
        quint16 innerseq;         // 0x16
        char unusedb[2];          // 0x18
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
        quint32 payloadsize;        // 0x10
        quint8 requestreply;        // 0x14
        quint8 requesttype;         // 0x15
        quint16 innerseq;           // 0x16
        char unusedb[2];            // 0x18
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
        quint32 payloadsize;      // 0x10
        quint8 requestreply;      // 0x14
        quint8 requesttype;       // 0x15
        quint16 innerseq;         // 0x16
        char unusedb[2];          // 0x18
        quint16 tokrequest;       // 0x1a
        quint32 token;            // 0x1c 
        union {
            struct {
                quint16 authstartid;    // 0x20
                char unusedg[5];        // 0x22
                quint16 commoncap;      // 0x27
                char unusedh;           // 0x29
                quint8 macaddress[6];     // 0x2a
            };
            quint8 guid[GUIDLEN];                  // 0x20
        };
        char unusedab[16];        // 0x30
        char name[32];                  // 0x40
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
                quint8 convert;      // 0x88
                char unusedl[7];      // 0x89
            };
        };
    };
    char packet[CONNINFO_SIZE];
} *conninfo_packet_t;


// 0x64 length radio capabilities part of cap packet.

typedef union radio_cap_packet {
    struct
    {
        union {
            struct {
                quint8 unusede[7];          // 0x00
                quint16 commoncap;          // 0x07
                quint8 unused;              // 0x09
                quint8 macaddress[6];       // 0x0a
            };
            quint8 guid[GUIDLEN];           // 0x0
        };
        char name[32];            // 0x10
        char audio[32];           // 0x30
        quint16 conntype;         // 0x50
        char civ;                 // 0x52
        quint16 rxsample;         // 0x53
        quint16 txsample;         // 0x55
        quint8 enablea;           // 0x57
        quint8 enableb;           // 0x58
        quint8 enablec;           // 0x59
        quint32 baudrate;         // 0x5a
        quint16 capf;             // 0x5e
        char unusedi;             // 0x60
        quint16 capg;             // 0x61
        char unusedj[3];          // 0x63
    };
    char packet[RADIO_CAP_SIZE];
} *radio_cap_packet_t;



// 0xA8 length capabilities packet
typedef union capabilities_packet {
    struct
    {
        quint32 len;              // 0x00
        quint16 type;             // 0x04
        quint16 seq;              // 0x06
        quint32 sentid;           // 0x08 
        quint32 rcvdid;           // 0x0c
        quint32 payloadsize;      // 0x10
        quint8 requestreply;      // 0x14
        quint8 requesttype;       // 0x15
        quint16 innerseq;         // 0x16
        char unusedb[2];          // 0x18
        quint16 tokrequest;       // 0x1a
        quint32 token;            // 0x1c 
        char unusedd[32];         // 0x20
        quint16 numradios;        // 0x40
    };
    char packet[CAPABILITIES_SIZE];
} *capabilities_packet_t;


typedef union streamdeck_image_header {
    struct
    {
        quint8 cmd;
        quint8 suffix;
        quint8 button;
        quint8 isLast;
        quint16 length;
        quint16 index;
    };
    char packet[8];
} *streamdeck_image_header_t;

typedef union streamdeck_v1_image_header {
    struct
    {
        quint8 cmd;
        quint8 suffix;
        quint16 index;
        quint8 isLast;
        quint8 button;
        quint8 unused[10];
    };
    char packet[16];
} *streamdeck_v1_image_header_t;

typedef union streamdeck_lcd_header {
    struct
    {
        quint8 cmd;
        quint8 suffix;
        quint16 x;
        quint16 y;
        quint16 width;
        quint16 height;
        quint8 isLast;
        quint16 index;
        quint16 length;
        quint8 unused;
    };
    char packet[16];
} *streamdeck_lcd_header_t;

typedef union yaesu_scope_data {
    struct {
        quint8 wf1[850];
        quint8 wf2[850];
        quint8 audio1fft[200];
        quint8 audio1scope[400];
        quint8 audio2fft[200];
        quint8 audio2scope[400];
        quint8 data[150];
        quint8 unused[1042];
        quint8 sync[4];
    };
    quint8 packet[4096];

} *yaesu_scope_data_t;

typedef union rtp_header
{
    struct {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
        quint8         csrc:4;
        quint8         extension:1;
        quint8         padding:1;
        quint8         version:2;
        quint8         payloadType:7;
        quint8         marker:1;
#elif G_BYTE_ORDER == G_BIG_ENDIAN
        quint8         version:2;
        quint8         padding:1;
        quint8         extension:1;
        quint8         csrc:4;
        quint8         marker:1;
        quint8         payloadType:7;
#else
#error "G_BYTE_ORDER is not defined"
#endif
        quint16 seq;
        quint32 timestamp;
        quint32 ssrc;
    };
    quint8 packet[12];
} *rtp_header_t;



// Yaesu packets

enum yaesuDeviceId : quint16 {
    ScuLan = 0,
    UsbCat = 1,
    UsbAudio = 2,
    AnalogAudio = 3,
    LoopbackAudio = 4,
    SerialCat = 5,
    Scope = 6
};

enum yaesuMessageId : quint16 {
    C2R_Login = 0x101,
    C2R_Logout = 0x102,
    C2R_Open = 0x103,
    C2R_Connect = 0x104,
    C2R_Data = 0x105,
    C2R_Close = 0x106,
    C2R_HealthCheck = 0x107,
    C2R_CatRate = 0x108,
    R2C_LoginReply = 0x181,
    R2C_LogoutReply = 0x182,
    R2C_OpenReply = 0x183,
    R2C_ConnectReply = 0x184,
    R2C_Data = 0x185,
    R2C_CloseReply = 0x186,
    R2C_HealthReply = 0x187
};



struct yaesuFrameHeader {
    yaesuDeviceId device;
    yaesuMessageId msgtype;
    quint16 len;
};

#pragma warning( push )
#pragma warning( disable : 4200 )
struct yaesuGenericFrame {
    yaesuFrameHeader hdr;
    quint8 data[];
};
#pragma warning( pop )

struct yaesuSessionFrame {
    yaesuFrameHeader hdr;
    quint32 session;
};

struct yaesuCatDataPacket {
    quint16 packet_id;
    quint16 reserved;
    quint8 data[0x4000];
};

struct yaesuR2C_CatDataFrame {
    yaesuFrameHeader hdr;
    yaesuCatDataPacket data;
};

struct yaesuC2R_CatDataFrame {
    yaesuFrameHeader hdr;
    quint32 session;
    yaesuCatDataPacket data;
};

struct yaesuR2C_ScopeDataFrame {
    yaesuFrameHeader hdr;
    quint8 data[0x1000];
};

struct yaesuR2C_LoginReplyFrame {
    yaesuFrameHeader hdr;
    quint32 session;
    quint32 reserved;
    quint16 vala;
    quint16 valb;
    quint16 valc;
};

enum yaesuAudioFormat : quint8 {
    UnknownAudio = 0,
    ShortLE = 1,
    ShortBE = 2,
    MuLaw = 3
};


struct yaesuAudioData {
    quint16 seqNum;
    yaesuAudioFormat format;
    quint8 channels;
    quint32 sampleRate;
    quint8 resendMode;
    quint8 reserved[7];
    quint16 playbackLatency;
    quint16 recordLatency;
    quint8 playbackVol;
    quint8 recordVol;
    quint16 pcmDataLen;
    quint8 pcmData[0x500];
};

struct yaesuC2R_AudioData {
    yaesuFrameHeader hdr;
    quint32 session;
    yaesuAudioData data;
};

struct yaesuR2C_AudioData {
    yaesuFrameHeader hdr;
    yaesuAudioData data;
};

struct yaesuC2R_LoginFrame {
    yaesuFrameHeader hdr;
    quint8 reserved_1;
    quint8 reserved_0;
    char username[33];
    char pw[33];
};

struct yaesuC2R_LogoutFrame {
    yaesuFrameHeader hdr;
    quint8 reserved_1;
    quint32 session;
    quint32 sleep;
};

struct yaesuC2R_DeviceOpenFrame {
    yaesuFrameHeader hdr;
    quint32 session;
    quint16 netPort;
};

typedef yaesuSessionFrame yaesuC2R_HeartbeatFrame;
typedef yaesuSessionFrame yaesuC2R_DeviceConnectFrame;

struct yaesuC2R_CatRateFrame {
    yaesuFrameHeader hdr;
    quint32 session;
    quint32 baud;
};

struct yaesuC2R_CatOpenFrame {
    yaesuFrameHeader hdr;
    quint32 session;
    quint16 netPort;
    quint32 baud;
};

struct yaesuResultFrame {
    yaesuFrameHeader hdr;
    quint32 result;
};

typedef yaesuResultFrame yaesuR2C_DeviceOpenFrame;
typedef yaesuResultFrame yaesuR2C_DeviceConnectFrame;


struct yaesuR2C_HeartbeatFrame {
    yaesuFrameHeader hdr;
    quint8 result;
};

struct yaesuControlDataFrame {
    yaesuFrameHeader hdr;
    quint32 session;
    quint16 type;
    quint16 opt;
};

struct yaesuControlCommandFrame {
    yaesuFrameHeader hdr;
    quint32 session;
    quint32 reserved;
    char text[256];
};

struct yaesuControlCommandFrameB {
    yaesuFrameHeader hdr;
    quint32 session;
    quint32 reserved;
    quint16 vala;
    quint16 valb;
    quint16 valc;
    quint16 vald;
    char text[256];
};

struct yaesuEncodedFrame {
    uint16_t type;
    uint16_t length;
    uint16_t encodingType;
    uint8_t firstKey;
    uint8_t secondKey;
    uint8_t encodedData[YAESU_MAX_FRAME_SIZE];
};

#pragma pack(pop)


#endif // PACKETTYPES_H

