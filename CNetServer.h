#ifndef __NET_SERVER_HEADER__
#define __NET_SERVER_HEADER__

namespace MonLib
{
    class CNetServer
    {
    private:
        enum NET_SERVER_DEFINE
        {
            PACKET_POOL_CHUNK_SIZE = 200,
            WORKER_THREAD_MAX_COUNT = 10
        };

        struct st_SESSION
        {
            st_SESSION(void)
            {
                ShutdownFlag = false;
                SendDisconnectFlag = false;

                SessionID = SESSION_ID_DEFAULT;
                Sock = INVALID_SOCKET;
                //memset(&_Session[Cnt].RecvOL, 0, sizeof(OVERLAPPED));
                //memset(&_Session[Cnt].SendOL, 0, sizeof(OVERLAPPED));

                p_IOCompare = (st_IO_COMPARE *)_aligned_malloc(sizeof(st_IO_COMPARE), 16);
                p_IOCompare->IOCount = 0;
                //p_IOCompare->ReleaseFlag = 0;
                p_IOCompare->ReleaseFlag = 1;       // 초기값은 1

                SendIO = 0;
                SendPacketCnt = 0;
                SendPacketSize = 0;

                // debug
                //JoinFlag = 0;
                //ReleaseType = 0;
                //SendQueueCnt = 0;
            }
            ~st_SESSION(void)
            {
                _aligned_free(p_IOCompare);
            }

            bool                        ShutdownFlag;           // 서버 종료용 플래그
            bool                        SendDisconnectFlag;     // Send하고 끊을지 플래그

            __int64                     SessionID;
            SOCKADDR_IN                 SessionAddr;

            SOCKET                      Sock;
            StreamingQueue              RecvQ;
            LockfreeQueue<Packet *>     SendQ;
            OVERLAPPED                  RecvOL;
            OVERLAPPED                  SendOL;

            st_IO_COMPARE               *p_IOCompare;       // IOCount, ReleaseFlag

            __int64                     SendIO;             // Send 작업 플래그 : 0, 1
            int                         SendPacketCnt;      // 내가 이번에 보낸 패킷의 개수
            int                         SendPacketSize;     // 내가 이번에 보낸 패킷의 사이즈

            // debug
            //long                        JoinFlag;           // default: 0, join : +1, leave : -1
            //long                        ReleaseType;
            // default: 0, 1: RecvPost, 2: SendPost, 3: AcceptThread, 4: IOCP Worker Error
            // 5: Sendpacket io, 6: Sendpacket SessionID Miss, 7: Sendpacket Last io
            // 8: CompleteRecv, 9:CompleteSend, 10:SendPost SessionID Miss
            // 11: cbTransferred 0
            // 21: Disconnect io, 22: Disconnect SessionID Miss, 23: Disconnect Last io
            //long                        SendQueueCnt;
        };

    public:
        CNetServer(int SessionMax, bool Nagle = false);
        virtual ~CNetServer(void);

        bool start(WCHAR *p_IP, USHORT Port, int WorkerThreadCnt, BYTE PacketCode, BYTE XORCode1, BYTE XORCode2);
        void stop(void);

        LONG64 GetSessionCount(void);
        bool SendPacket(__int64 SessionID, Packet *p_Packet, bool Disconnect = false);
        bool SendPacketRequest(__int64 SessionID, Packet *p_Packet, bool Disconnect = false);
        bool SendPacketIOCP(__int64 SessionID, Packet *p_Packet, bool Disconnect = false);
        bool GetServerOn(void);
        int GetEmptyStackUseSize(void);

    protected:
        virtual void OnClientJoin(__int64 SessionID) = 0;
        virtual void OnClientLeave(__int64 SessionID) = 0;

        virtual bool OnConnectionRequest(st_CLIENT_CONNECT_INFO *p_ClientConnectInfo) = 0;

        virtual void OnRecv(__int64 SessionID, Packet *p_Packet) = 0;
        virtual void OnSend(__int64 SessionID, int SendSize) = 0;

        virtual void OnWorkerThreadBegin(__int64 SessionID) = 0;
        virtual void OnWorkerThreadEnd(__int64 SessionID) = 0;

        virtual void OnError(int ErrorCode, WCHAR *ErrorMessage) = 0;

        virtual void OnWorkerThreadHeartBeat(void) = 0;

        bool Disconnect(__int64 ClientID);
        void SetWorkerThreadHeartBeatTick(int Tick) { _WorkerthreadHeartbeatTick = Tick; };

    private:
        bool NetworkInit(void);

        unsigned __int64 FindEmptySession(void);
        st_SESSION * FindSession(__int64 SessionID);
        int CompletePacket(st_SESSION *p_Session);
        int CompleteRecvPacket(st_SESSION *p_Session);

        bool RecvPost(st_SESSION *p_Session);
        //bool SendPost(st_Session *p_Session);
        bool SendPost(__int64 SessionID);
        bool SendPostIOCP(__int64 SessionID);

        void CompleteRecv(st_SESSION *p_Session, DWORD cbTransferred);
        void CompleteSend(st_SESSION *p_Session, DWORD cbTransferred);

        void ClientShutdown(st_SESSION *p_Session);              // shutdown으로 안전한 종료 유도
        void ClientDisconnect(st_SESSION *p_Session);            // socket 접속 강제 끊기
        void SocketClose(SOCKET SessionSock);                    // closesocket(내부용)
        void ClientRelease(st_SESSION *p_Session);               // client session 해제

        //void ClientReleaseDebug(st_Session *p_Session, int Type);   // 디버그용
        //void ClientReleaseDebug(__int64 SessionID, int Type);   // 디버그용

        static unsigned __stdcall AcceptThreadProc(LPVOID lpParam);
        unsigned AcceptThread_Update(void);
        static unsigned __stdcall WorkerThreadProc(LPVOID lpParam);
        unsigned WorkerThread_Update(void);
        static unsigned __stdcall MonitorTPS_Thread(LPVOID lpParam);
        unsigned MonitorTPS_Thread_update(void);

    public:
        // debug
        __int64                             _Monitor_AcceptTotal;

        // 모니터링용 외부참고 변수
        long                                _Monitor_AcceptTPS;
        long                                _Monitor_RecvPacketTPS;
        long                                _Monitor_SendPacketTPS;

        // 모니터링 데이터 계산용
        long                                _Monitor_AcceptCounter;
        long                                _Monitor_RecvPacketCounter;
        long                                _Monitor_SendPacketCounter;

    protected:
        TimeManager                         *_TimeManager;

    private:
        bool                                _ServerOn;
        SOCKET                              _ListenSock;

        // 서버 설정값
        WCHAR                               _IP[IP_V4_MAX_LEN + 1]; // binding ip
        USHORT                              _Port;                  // port
        int                                 _WorkerThreadMax;
        int                                 _SessionMax;            // 넷서버에서는 생성자로 받음.
        bool                                _Nagle;                 // 넷서버에서는 생성자로 받음.

        // Session
        __int64                             _SessionKeyIndex;
        LONG64                              _SessionCount;          // 서버의 세션 수
        st_SESSION                          *_Session;              // 실제 세션 배열
        LockfreeStack<unsigned __int64>     *_SessionEmptyStack;    // 비어있는 세션 인덱스 스택

        // iocp
        HANDLE                              _hIocp;
        HANDLE                              _WorkerThread[WORKER_THREAD_MAX_COUNT];
        unsigned int                        _WorkerThreadId[WORKER_THREAD_MAX_COUNT];

        // accept
        HANDLE                              _AcceptThread;
        unsigned int                        _AcceptThreadId;

        // monitoring
        HANDLE                              _MonitorTPSThread;
        unsigned int                        _MonitorThreadId;

        // HeartBeat
        int                                 _WorkerthreadHeartbeatTick;

    //public:
    //    // 샌드큐 카운트 외부참고 변수
    //    //long _SendQueueCount;
    //
    //    // 모니터링용 외부참고 변수
    //    long _Monitor_AcceptTPS;
    //    long _Monitor_RecvPacketTPS;
    //    long _Monitor_SendPacketTPS;
    //
    //    // 모니터링 데이터 계산용
    //    long _Monitor_AcceptCounter;
    //    long _Monitor_RecvPacketCounter;
    //    long _Monitor_SendPacketCounter;
    //
    //    unsigned int _MonitorThreadId;
    //    HANDLE _MonitorTPSThread;
    //    static unsigned __stdcall MonitorTPS_Thread(LPVOID lpParam);
    //    bool MonitorTPS_Thread_update(void);
    };
}

#endif