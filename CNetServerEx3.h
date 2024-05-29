#ifndef __NET_SERVER_EX3_HEADER__
#define __NET_SERVER_EX3_HEADER__

namespace MonLib
{
    class SocketPool;

    class CNetServerEx3
    {
    private:
        enum NET_SERVER_EX_DEFINE
        {
            PACKET_POOL_CHUNK_SIZE = 200,
            WORKER_THREAD_MAX_COUNT = 10,
            ACCEPT_EX_THREAD_MAX_COUNT = 3
        };

        enum COMPLETION_KEY_DEFINE
        {
            COMPLETION_KEY_EXIT = 0,
            COMPLETION_KEY_HEARTBEAT,
            COMPLETION_KEY_OVERLAPPED,
            //COMPLETION_KEY_LISTEN_SOCKET,
            
            //COMPLETION_KEY_LISTEN,
            //COMPLETION_KEY_SESSION,
            
            //COMPLETION_KEY_SEND_PACKET
            COMPLETION_KEY_DEFAULT,
        };

        enum OVERLAPPED_TYPE_DEFINE
        {
            OVERLAPPED_TYPE_ACCEPT = 0,
            OVERLAPPED_TYPE_DISCONNECT,
            OVERLAPPED_TYPE_COMPLETE_NETWORK,
            OVERLAPPED_TYPE_DEFAULT,
            //OVERLAPPED_TYPE_RECV,
            //OVERLAPPED_TYPE_SEND
        };

        struct st_EXTEND_OVERLAPPED : public OVERLAPPED
        {
            __int64 Type;
            void *_p_Data;
        };

        struct st_NETWORK_SOCKET : public st_EXTEND_OVERLAPPED
        {
            bool _IOCPFlag;
            SOCKET _Sock = INVALID_SOCKET;
            BYTE _Buf[(sizeof(sockaddr_in) + 16) * 2];
        };

        //struct st_COMPLETION_KEY
        //{
        //    int _Type;
        //    void *_p_Data;
        //};

        struct st_SESSION
        {
            st_SESSION(void)
            {
                SendDisconnectFlag = false;

                SessionID = SESSION_ID_DEFAULT;
                p_NetworkSocket = nullptr;
                //p_CompletionKey = nullptr;

                RecvOL.Type = OVERLAPPED_TYPE_COMPLETE_NETWORK;
                RecvOL._p_Data = this;
                SendOL.Type = OVERLAPPED_TYPE_COMPLETE_NETWORK;
                SendOL._p_Data = this;

                p_IOCompare = (st_IO_COMPARE *)_aligned_malloc(sizeof(st_IO_COMPARE), 16);
                p_IOCompare->IOCount = 0;
                p_IOCompare->ReleaseFlag = 1;       // 초기값은 1

                SendIO = 0;
                SendPacketCnt = 0;
                SendPacketSize = 0;
            }
            ~st_SESSION(void)
            {
                _aligned_free(p_IOCompare);
            }

            bool                        SendDisconnectFlag;     // Send하고 끊을지 플래그

            __int64                     SessionID;
            st_NETWORK_SOCKET           *p_NetworkSocket;
            //st_COMPLETION_KEY           *p_CompletionKey;

            StreamingQueue              RecvQ;
            LockfreeQueue<Packet *>     SendQ;
            st_EXTEND_OVERLAPPED        RecvOL;
            st_EXTEND_OVERLAPPED        SendOL;

            st_IO_COMPARE               *p_IOCompare;       // IOCount, ReleaseFlag

            __int64                     SendIO;             // Send 작업 플래그 : 0, 1
            int                         SendPacketCnt;      // 내가 이번에 보낸 패킷의 개수
            int                         SendPacketSize;     // 내가 이번에 보낸 패킷의 사이즈
        };

    public:
        CNetServerEx3(int SessionMax, bool Nagle = false);
        virtual ~CNetServerEx3(void);

        bool start(WCHAR *p_IP, USHORT Port, int WorkerThreadCnt, BYTE PacketCode, BYTE XORCode1, BYTE XORCode2, bool SendThread = true);
        void stop(void);

        LONG64 GetSessionCount(void);
        bool SendPacketSendThread(__int64 SessionID, Packet *p_Packet, bool Disconnect = false);
        bool GetServerOn(void);
        __int64 GetSocketUseCount(void);
        __int64 GetSocketAllocCount(void);
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
        bool AcceptExInit(void);
        bool DisconnectExInit(void);
        bool SetAcceptEx(SOCKET *ListenSock, st_NETWORK_SOCKET *p_NetworkSocket);

        unsigned __int64 FindEmptySession(void);
        st_SESSION * FindSession(__int64 SessionID);
        int CompletePacket(st_SESSION *p_Session);
        int CompleteRecvPacket(st_SESSION *p_Session);

        bool RecvPost(st_SESSION *p_Session);
        bool SendPostIOCP(__int64 SessionID);

        void CompleteRecv(st_SESSION *p_Session, DWORD cbTransferred);
        void CompleteSend(st_SESSION *p_Session, DWORD cbTransferred);

        void ClientShutdown(st_SESSION *p_Session);              // shutdown으로 안전한 종료 유도
        void ClientDisconnect(st_SESSION *p_Session);            // socket 접속 강제 끊기
        void SocketClose(SOCKET SessionSock);                    // closesocket(내부용)
        void SocketReturn(st_NETWORK_SOCKET *p_NetworkSocket);     // 소켓풀로 반환

        void ClientRelease(st_SESSION *p_Session);
        //void ClientReleaseEx(st_SESSION *p_Session);
        void SocketRelease(st_NETWORK_SOCKET *p_NetworkSocket);

        static unsigned __stdcall MonitorTPS_Thread(LPVOID lpParam);
        unsigned MonitorTPS_Thread_update(void);

        //static unsigned __stdcall AcceptExThreadProc(LPVOID lpParam);
        //unsigned AcceptExThread_Update(void);
        static unsigned __stdcall WorkerExThreadProc(LPVOID lpParam);
        unsigned WorkerExThread_Update(void);
        static unsigned __stdcall SendThreadProc(LPVOID lpParam);
        unsigned SendThread_Update(void);

        //bool AcceptProc(void);

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

        //// CompletionKey
        //LockfreeStack<st_COMPLETION_KEY *>  *_CompletionKeyStack;

        // iocp
        HANDLE                              _hIocp;
        HANDLE                              _WorkerThread[WORKER_THREAD_MAX_COUNT];
        unsigned int                        _WorkerThreadId[WORKER_THREAD_MAX_COUNT];

        // accept
        //HANDLE                              _AcceptThread;
        //unsigned int                        _AcceptThreadId;

        // monitoring
        HANDLE                              _MonitorTPSThread;
        unsigned int                        _MonitorThreadId;

        // HeartBeat
        int                                 _WorkerthreadHeartbeatTick;

        // AcceptEx
        LPFN_ACCEPTEX                       _lpfnAcceptEx;          // AcceptEx 함수 포인터
        //st_NETWORK_SOCKET                   *_NetworkSocket;        // Accept 오버랩 구조체 배열

        // DisconnectEx
        LPFN_DISCONNECTEX                   _lpfnDisconnectEx;     // DisconnectEx 함수 포인터

        //HANDLE                              _hIocpAccept;
        //HANDLE                              _AcceptExThread[ACCEPT_EX_THREAD_MAX_COUNT];
        //unsigned int                        _AcceptExThreadId[ACCEPT_EX_THREAD_MAX_COUNT];

        // Send
        HANDLE                              _SendThread;
        unsigned int                        _SendThreadId;

        friend SocketPool;
        SocketPool                          *_SocketPool;
    };

    class SocketPool
    {
    private:
        __int64                                             _SocketAllocCount;
        __int64                                             _SocketUseCount;
        LockfreeQueue<CNetServerEx3::st_NETWORK_SOCKET *>   _SocketPool;

    public:
        SocketPool(int SocketPoolSize)
        {
            int Cnt;
            CNetServerEx3::st_NETWORK_SOCKET *p_NetworkSocket;

            _SocketAllocCount = SocketPoolSize;
            _SocketUseCount = 0;
            for (Cnt = 0; Cnt < _SocketAllocCount; ++Cnt)
            {
                p_NetworkSocket = new CNetServerEx3::st_NETWORK_SOCKET;
                p_NetworkSocket->_IOCPFlag = false;
                p_NetworkSocket->_Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (INVALID_SOCKET == p_NetworkSocket->_Sock)
                    CrashDump::Crash();
                if (false == _SocketPool.Enqueue(p_NetworkSocket))
                    CrashDump::Crash();
            }
        }
        virtual ~SocketPool(void)
        {

        }

        bool AllocSocket(CNetServerEx3::st_NETWORK_SOCKET **pp_NetworkSocket)
        {
            __int64 UseCount;

            UseCount = InterlockedIncrement64(&_SocketUseCount);
            if (UseCount > _SocketAllocCount)
            {
                InterlockedDecrement64(&_SocketUseCount);
                return false;
            }
            if (false == _SocketPool.Dequeue(pp_NetworkSocket))
            {
                CrashDump::Crash();
            }
            return true;
        }
        bool FreeSocket(CNetServerEx3::st_NETWORK_SOCKET *p_NetworkSocket)
        {
            if (nullptr == p_NetworkSocket)
            {
                CrashDump::Crash();
                return false;
            }

            if (false == _SocketPool.Enqueue(p_NetworkSocket))
            {
                CrashDump::Crash();
            }
            InterlockedDecrement64(&_SocketUseCount);
            return true;
        }
        __int64 GetUseCount(void)
        {
            return _SocketUseCount;
        }
        __int64 GetAllocCount(void)
        {
            return _SocketAllocCount;
        }
    };
}

#endif