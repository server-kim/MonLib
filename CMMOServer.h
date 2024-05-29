#ifndef __MMO_SERVER_HEADER__
#define __MMO_SERVER_HEADER__

namespace MonLib
{
    enum SESSION_MODE
    {
        MODE_NONE = 0,          // 세션 미사용 상태

        MODE_AUTH,              // 커넥트 후 인증 모드 상태
        MODE_AUTH_TO_GAME,      // 게임모드로 전환중
        MODE_GAME,              // 인증 후 게임모드 상태

        MODE_LOGOUT_IN_AUTH,    // 인증모드 종료 준비
        MODE_LOGOUT_IN_GAME,    // 게임모드 종료 준비

        MODE_WAIT_LOGOUT        // 최종 종료 준비
    };

    class NetSession
    {
    public:
        __int64                 _SessionID;

        SESSION_MODE            _Mode;
        __int64                 _ArrayIndex;

        st_CLIENT_CONNECT_INFO  _ClientInfo;

        StreamingQueue          _RecvQ;
        LockfreeQueue<Packet *> _SendQ;
        LockfreeQueue<Packet *> _CompletePacketQ;

        OVERLAPPED              _RecvOL;
        OVERLAPPED              _SendOL;

        int                     _SendPacketCnt;
        int                     _SendPacketSize;

        LONG                    _SendIO;
        LONG64                  _IOCount;

        bool                    _LogoutFlag;
        bool                    _AuthToGameFlag;

        bool                    _DisconnectFlag;

        DWORD                   _LastRecvPacketTime;

        NetSession(void);
        virtual ~NetSession(void);

        void SessionInit(void);

        void SetMode_Game(void);
        bool SendPacket(Packet *p_Packet);
        void Disconnect(bool Force);      // socket 접속 강제 끊기

        void CompleteRecv(DWORD Transferred);
        void CompleteSend(DWORD Transferred);

        bool RecvPost(void);
        bool SendPost(void);

        virtual void OnAuth_ClientJoin(void) = 0;
        virtual void OnAuth_ClientLeave(bool ToGame) = 0;
        virtual void OnAuth_Packet(Packet *p_Packet) = 0;
        virtual void OnAuth_Timeout(void) = 0;

        virtual void OnGame_ClientJoin(void) = 0;
        virtual void OnGame_ClientLeave(void) = 0;
        virtual void OnGame_Packet(Packet *p_Packet) = 0;
        virtual void OnGame_Timeout(void) = 0;

        virtual void OnGame_ClientRelease(void) = 0;
    };

    class CMMOServer
    {
    private:
        enum MMO_SERVER_DEFINE
        {
            SessionMax = 20000,
            PACKET_POOL_CHUNK_SIZE = 200,
            CONNECT_POOL_CHUNK_SIZE = 100,

            WORKER_THREAD_MAX_COUNT = 10,
            //AUTH_THREAD_TIMEOUT = 10000,
            //GAME_THREAD_TIMEOUT = 10000

            AUTH_THREAD_TIMEOUT = 30000,
            GAME_THREAD_TIMEOUT = 30000,

            GAME_THREAD_HEARTBEAT_TICK = 1000,
            WORKER_HEARTBEAT_TICK = 1000
        };

        enum TEMP_DEFINE        // 설정 파일을 읽어와서 대체할 것들
        {
            AUTH_PACKET_PER_FRAME = 3,      // 한 프레임에 최대 2개 처리.
            GAME_PACKET_PER_FRAME = 3,

            //auth 10
            //game 8
            //send 2
            AUTH_THREAD_DELAY = 5,
            GAME_THREAD_DELAY = 1,
            SEND_THREAD_DELAY = 2
        };

    protected:
        

    public:
        CMMOServer(int MaxSession);
        virtual ~CMMOServer(void);

        bool Start(WCHAR *p_ListenIP, USHORT Port, int WorkerThread, bool EnableNagle, BYTE PacketCode, BYTE PacketKey1, BYTE PacketKey2);
        bool Stop(void);
        
    protected:
        void SetSessionArray(int ArrayIndex, NetSession *p_Session);

        bool CreateThread(void);
        bool CreateIOCP_Socket(SOCKET Socket, ULONG_PTR Key);

        //void SendPacket_GameAll(Packet *p_Packet, __int64 ExcludeSessionID = 0);
        //void SendPacket(Packet *p_Packet, __int64 SessionID);

    //public:
    private:
        void Error(int ErrorCode, WCHAR *p_FormatStr, ...);

        void ProcAuth_Accept(void);
        void ProcAuth_Packet(void);
        void ProcAuth_Logout(void);

        void ProcGame_AuthToGame(void);
        void ProcGame_Packet(void);
        void ProcGame_Logout(void);

        void ProcGame_Release(void);

    //private:
        virtual void OnAuth_Update(void) = 0;
        virtual void OnGame_Update(void) = 0;

        // 하트비트 부분
        //virtual void OnWorker_HeartBeat(void) = 0;
        //Send, Auth, Game
        virtual void OnWorker_HeartBeat(void) = 0;
        virtual void OnSend_HeartBeat(void) = 0;
        virtual void OnAuth_HeartBeat(void) = 0;
        virtual void OnGame_HeartBeat(void) = 0;

        virtual void OnError(int ErrorCode, WCHAR *p_Error) = 0;

        //void ClientShutdown(st_Session *p_Session);              // shutdown으로 안전한 종료 유도
        //void ClientDisconnect(st_Session *p_Session);            // socket 접속 강제 끊기
        void SocketClose(SOCKET SessionSock);                    // closesocket(내부용)

    public:
        

    protected:
        const int                               _MaxSession;
        TimeManager                             *_TimeManager;

    private:
        bool                                    _Shutdown;
        bool                                    _ShutdownListen;
        bool                                    _ShutdownAuthThread;
        bool                                    _ShutdownGameThread;
        bool                                    _ShutdownSendThread;

        //------------------------------------------------
        // Listen Socket
        //------------------------------------------------
        SOCKET                                  _ListenSocket;

        // 서버 설정값
        bool                                    _EnableNagle;
        int                                     _WorkerThread;

        WCHAR                                   _ListenIP[IP_V4_MAX_LEN + 1];
        USHORT                                  _ListenPort;

        //------------------------------------------------
        // Accept
        //------------------------------------------------
        HANDLE                                  _AcceptThread;

        LockfreeQueue<st_CLIENT_CONNECT_INFO *>    _AcceptSocketQueue;
        MemoryPoolTLS<st_CLIENT_CONNECT_INFO>      _MemoryPool_ConnectInfo;

        //------------------------------------------------
        // Auth
        //------------------------------------------------
        HANDLE                                  _AuthThread;
        LockfreeStack<__int64>                  _BlankSessionStack;

        //------------------------------------------------
        // GameUpdate
        //------------------------------------------------
        HANDLE                                  _GameUpdateThread;

        //------------------------------------------------
        // IOCP
        //------------------------------------------------
        HANDLE                                  _IOCPWorkerThread[WORKER_THREAD_MAX_COUNT];
        HANDLE                                  _IOCP;

        //------------------------------------------------
        // Send
        //------------------------------------------------
        HANDLE                                  _SendThread;

        //------------------------------------------------
        // Session
        //------------------------------------------------
        __int64                                 _SessionKeyIndex = 0;
        NetSession                              **_SessionArray;

        //------------------------------------------------
        // Thread Funcion
        //------------------------------------------------
        static unsigned __stdcall AcceptThread(void *p_Param);
        bool AcceptThread_Update(void);

        static unsigned __stdcall AuthThread(void *p_Param);
        bool AuthThread_Update(void);

        static unsigned __stdcall GameUpdateThread(void *p_Param);
        bool GameUpdateThread_Update(void);

        static unsigned __stdcall IOCPWorkerThread(void *p_Param);
        bool IOCPWorkerThread_Update(void);

        static unsigned __stdcall SendThread(void *p_Param);
        bool SendThread_Update(void);

    public:
        //------------------------------------------------
        // Monitoring
        //------------------------------------------------
        long _Monitor_AcceptSocket;
        long _Monitor_SessionAllMode;
        long _Monitor_SessionAuthMode;
        long _Monitor_SessionGameMode;

        long _Monitor_Counter_Accept;
        static long _Monitor_Counter_PacketSend;
        long _Monitor_Counter_PackerProc;
        long _Monitor_Counter_AuthUpdate;
        long _Monitor_Counter_GameUpdate;

        long _Monitor_Counter_DBWriteTPS;
        //long _Monitor_Counter_DBWriteMSG;
    };

    // 버전은 현재 1이다. 세션키는 무시할 것.
    // 어스 모드의 패킷 프로시져에서 로그인 패킷이 처리가 되어야 함.
    // 만약 게임모드에 있는데 Request login이 왔으면 지금은 크래시 낼것.

    // 어스모드인데 에코 패킷이 왔다면 지금은 크래시 낼 것.
    // 만약 이러한 문제가 발생하였다면 모드 변경에 문제가 있는 것.

    // Stauts : 현재는 무조건 성공 1 -> 2번은 캐릭터가 없을 때 내려주는 것임.(0번과 3번 같은 경우는 Send하고 완료통지가 오면 끊는다.)
    // 에코 패킷에서 SendTick은 더미가 준걸 그대로 되돌려 주면 된다.

    /*

    // 서버 로그 양식

    =====================
    AccpetSocket
    =====================
    Session

    Session_AuthMode
    Session_GameMode

    PacketPool : 0 use
    =====================
    Accept TPS
    PacketSend TPS
    PacketProc TPS

    AuthThread FPS
    GameThread FPS



    // 참고 : 더미에서 클라이언트 스레드가 1초이상 지연이 되면 로그를 찍어주고 있음.

    // iocount가 0이라면 logoutflag를 올려준다.

    // io count가 0인데 sendIO가 물려있는 애들이 생겨요.
    disconnect(true);
    0 == decrement iocount
    {
        logout flag = true;
    }
    exchange(sendio, 0)

    // CompleteRecv에서 패킷프록 호출하는 부분이 completeRecvQueue에 넣는 걸로 변경되었음.




    */

}

#endif