#ifndef __COMMON_HEADER__
#define __COMMON_HEADER__

namespace MonLib
{
    //// MMO Server에서 쓰는 세션 아이디
    //typedef __int64 SESSION_ID;

    //--------------------------------------------
    // enum define
    //--------------------------------------------

    // Common
    //enum COMMON_DEFINE
    //{
    //    TIME_MANAGER_UPDATE_TICK = 50
    //};

    // Network
    enum NETWORK_DEFINE
    {
        SERVER_NAME_LEN = 31,
        IP_V4_MAX_LEN = 15
    };

    // DB
    enum DB_DEFINE
    {
        DB_USER_MAX_LEN = 64,
        DB_PASSWORD_MAX_LEN = 64,
        DB_NAME_MAX_LEN = 64,
    };

    // 랜서버, 랜클라이언트, 넷서버와 MMO 서버등에서 쓰는 패킷 완료 코드
    enum COMPLETE_PACKET_DEFINE
    {
        Packet_Error = 0,
        Packet_Complete,
        Packet_NotComplete
    };

    enum ACCOUNT_DEFINE
    {
        ID_MAX_LEN = 20,
        NICK_MAX_LEN = 20,
        SESSION_KEY_BYTE_LEN = 64,

        ACCOUNT_NUM_DEFAULT = -51,
        SESSION_ID_DEFAULT = -52
    };

    enum CONTENTS_DEFINE
    {
        SECTOR_DEFAULT_X = -1,
        SECTOR_DEFAULT_Y = -1,
        //SECTOR_MAX_X = 49,
        //SECTOR_MAX_Y = 49
        SECTOR_MAX_X = 40,
        SECTOR_MAX_Y = 20
    };

    //--------------------------------------------
    // struct define
    //--------------------------------------------

    // 랜서버만 쓰는 헤더
#pragma pack(push, 1)
    struct st_LAN_HEADER
    {
        WORD PayloadSize;
    };
#pragma pack(pop)

    // Packet Header -> 넷서버 및 채팅서버, MMO 서버도 쓴다.
#pragma pack(push, 1)
    struct st_PACKET_HEADER
    {
        BYTE Code;
        WORD Len;
        BYTE XORCode;
        BYTE CheckSum;
    };
#pragma pack(pop)

    // 랜서버와 넷서버에서 쓰는 구조체
    struct st_IO_COMPARE
    {
        __int64 IOCount;
        __int64 ReleaseFlag;
    };

    // 넷서버와 MMO서버에서 쓰는 구조체
    struct st_CLIENT_CONNECT_INFO
    {
        SOCKET  _Socket;
        WCHAR   _IP[IP_V4_MAX_LEN + 1];
        USHORT  _Port;
    };
}

#endif