#include "LibHeader.h"

namespace MonLib
{
    CLanServer::CLanServer(void)
    {
        int Cnt;

        // 세션 키 인덱스 초기화
        _SessionKeyIndex = 0;

        // 타임 매니저 가져오기
        _TimeManager = TimeManager::GetInstance();
        //TimeManager TimeMgr = TimeManager::GetInstance();
        //_TimeManager = &TimeMgr;

        // 패킷풀 초기화
        Packet::MemoryPool(PACKET_POOL_CHUNK_SIZE);

        // 프로파일러 초기화
        ProfileInitial();

        // member variable init
        _ServerOn = false;
        _ListenSock = INVALID_SOCKET;

        wmemset(_IP, 0, IP_V4_MAX_LEN + 1);
        _Port = 0;
        _WorkerThreadMax = 0;
        //_SessionMax = SessionMax;
        _SessionMax                     = 0;
        //_Nagle = Nagle;
        _Nagle                          = false;

        _SessionCount = 0;
        _Session = nullptr;
        _SessionEmptyStack = nullptr;

        _hIocp = INVALID_HANDLE_VALUE;
        for (Cnt = 0; Cnt < WORKER_THREAD_MAX_COUNT; ++Cnt)
        {
            _WorkerThread[Cnt] = INVALID_HANDLE_VALUE;
            _WorkerThreadId[Cnt] = 0;
        }

        _AcceptThread = INVALID_HANDLE_VALUE;
        _AcceptThreadId = 0;

        // monitoring 변수 초기화
        _Monitor_AcceptTPS = 0;
        _Monitor_RecvPacketTPS = 0;
        _Monitor_SendPacketTPS = 0;

        _Monitor_AcceptCounter = 0;
        _Monitor_RecvPacketCounter = 0;
        _Monitor_SendPacketCounter = 0;

        // monitoring thread 생성
        _MonitorTPSThread = (HANDLE)_beginthreadex(NULL, 0, MonitorTPS_Thread, this, 0, &_MonitorThreadId);
        if (INVALID_HANDLE_VALUE == _MonitorTPSThread)
        {
            CrashDump::Crash();
            return;
        }

        // winsock init
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            CrashDump::Crash();
            return;
        }
    }
    CLanServer::~CLanServer(void)
    {
        stop();
        WSACleanup();
    }

    bool CLanServer::start(WCHAR *p_IP, USHORT Port, int WorkerThreadCnt, int SessionMax, bool Nagle)
    {
        if (true == _ServerOn)
            return false;
        _ServerOn = true;

        if (nullptr == p_IP)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"IP Address Invalid");
            return false;
        }

        if (SessionMax < 1)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"SessionMax Invalid");
            return false;
        }

        int Cnt;

        // 서버 설정값 세팅
        memcpy_s(_IP, IP_V4_MAX_LEN + 1, p_IP, IP_V4_MAX_LEN);
        _Port = Port;
        _SessionMax = SessionMax;
        _Nagle = Nagle;

        // determine worker thread count
        _WorkerThreadMax = WORKER_THREAD_MAX_COUNT;
        if (WorkerThreadCnt > 0 && WorkerThreadCnt < WORKER_THREAD_MAX_COUNT)
            _WorkerThreadMax = WorkerThreadCnt;

        // Session Key Init
        _SessionKeyIndex = 0;

        // Session 정보 초기화
        _SessionCount = 0;
        _Session = new st_SESSION[_SessionMax];
        _SessionEmptyStack = new LockfreeStack<unsigned __int64>;

        // SessionStack에 넣는다.
        for (Cnt = 0; Cnt < _SessionMax; ++Cnt)
        {
            _SessionEmptyStack->Push(Cnt);
        }

        // Iocp 생성
        _hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, _WorkerThreadMax);
        if (NULL == _hIocp)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"IOCP Creation Error");
            return false;
        }

        // 워커스레드 생성
        for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        {
            _WorkerThread[Cnt] = (HANDLE)_beginthreadex(NULL, 0, WorkerThreadProc, this, 0, &_WorkerThreadId[Cnt]);
            if (INVALID_HANDLE_VALUE == _WorkerThread[Cnt])
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"WorkerThread Creation Error");
                return false;
            }
        }

        // Network Init
        if (false == NetworkInit())
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Network Init Error");
            return false;
        }

        // Accept Thread 생성
        _AcceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThreadProc, this, 0, &_AcceptThreadId);
        if (INVALID_HANDLE_VALUE == _AcceptThread)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"AcceptThread Creation Error");
            return false;
        }

        return true;
    }
    bool CLanServer::NetworkInit(void)
    {
        //----------------------------------------------------
        // network init
        //----------------------------------------------------

        SOCKADDR_IN ServerAddr;
        int Ret;

        // socket 생성
        //_ListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        _ListenSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (INVALID_SOCKET == _ListenSock)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Listen Socket Creation Error");
            return false;
        }

        // binding
        ServerAddr.sin_family = AF_INET;
        ServerAddr.sin_port = htons(_Port);
        InetPton(AF_INET, _IP, &(ServerAddr.sin_addr));
        Ret = bind(_ListenSock, (sockaddr *)&ServerAddr, sizeof(ServerAddr));
        if (SOCKET_ERROR == Ret)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Listen Socket Bind Error");
            closesocket(_ListenSock);
            return false;
        }

        // listen
        Ret = listen(_ListenSock, SOMAXCONN);
        if (SOCKET_ERROR == Ret)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Listen Socket Listen Error");
            closesocket(_ListenSock);
            return false;
        }
        return true;
    }
    void CLanServer::stop(void)
    {
        if (false == _ServerOn)
            return;

        int Cnt;

        // close listen socket
        closesocket(_ListenSock);
        _ListenSock = INVALID_SOCKET;

        // accept thread 종료대기
        WaitForSingleObject(_AcceptThread, INFINITE);

        _AcceptThread = INVALID_HANDLE_VALUE;
        _AcceptThreadId = 0;

        // client graceful close(여기서 끊길 수 있는 상황 : 1. 네트워크 끊겼을 때, 2. 클라가 먹통이 되었을 때)
        for (Cnt = 0; Cnt < _SessionMax; ++Cnt)
        {
            if (_Session[Cnt].Sock != INVALID_SOCKET)
            {
                if (false == _Session[Cnt].ShutdownFlag)
                {
                    ClientShutdown(&_Session[Cnt]);
                    _Session[Cnt].ShutdownFlag = true;
                }
            }
        }

        // 모든 애들을 다 검사해서 Connect Count가 0이 될때까지 기다린다.
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Waiting For Client Disconnect");
        while (1)
        {
            bool DisconnectFlag = true;
            for (Cnt = 0; Cnt < _SessionMax; ++Cnt)
            {
                if (_Session[Cnt].Sock != INVALID_SOCKET && _Session[Cnt].p_IOCompare->ReleaseFlag != 1)
                    DisconnectFlag = false;
            }

            if (true == DisconnectFlag)
                break;

            Sleep(500);
        }
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"End For Client Disconnect");

        // worker thread
        for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        {
            PostQueuedCompletionStatus(_hIocp, 0, NULL, NULL);
        }
        WaitForMultipleObjects(_WorkerThreadMax, _WorkerThread, TRUE, INFINITE);

        // worker thread init
        _WorkerThreadMax = 0;
        for (Cnt = 0; Cnt < WORKER_THREAD_MAX_COUNT; ++Cnt)
        {
            _WorkerThread[Cnt] = INVALID_HANDLE_VALUE;
            _WorkerThreadId[Cnt] = 0;
        }

        // iocp init
        CloseHandle(_hIocp);
        _hIocp = INVALID_HANDLE_VALUE;

        // Session 배열 해제
        delete[] _Session;
        _Session = nullptr;
        _SessionCount = 0;

        // Session Stack 해제
        delete _SessionEmptyStack;
        _SessionEmptyStack = nullptr;

        _ServerOn = false;
    }
    LONG64 CLanServer::GetSessionCount(void)
    {
        return _SessionCount;
    }
    bool CLanServer::SendPacket(__int64 SessionID, Packet *p_Packet)
    {
        bool Ret;
        st_SESSION *p_Session;
        st_LAN_HEADER Header;

        p_Session = FindSession(SessionID);
        if (nullptr == p_Session)
        {
            // Session Index가 잘못되었으므로 정지한다.
            CrashDump::Crash();
            return false;
        }

        if (InterlockedIncrement64(&p_Session->p_IOCompare->IOCount) == 1)
        {
            if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
                ClientRelease(p_Session);
            return false;
        }

        if (p_Session->SessionID != SessionID)
        {
            if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
                ClientRelease(p_Session);
            return false;
        }

        // 헤더 설정
        Header.PayloadSize = p_Packet->GetDataSize();
        //p_Packet->SetHeader_CustomHeader((char *)&Header, sizeof(st_Header));
        p_Packet->SetHeader_CustomHeader_Short(Header.PayloadSize);

        // 패킷을 인큐한다.
        p_Packet->addRef();
        if (false == p_Session->SendQ.Enqueue(p_Packet))
        {
            CrashDump::Crash();
            return false;
        }
        //InterlockedIncrement(&p_Session->SendQueueCnt);

        Ret = SendPost(p_Session->SessionID);

        if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
        {
            ClientRelease(p_Session);
            return false;
        }
        return Ret;
    }
    bool CLanServer::GetServerOn(void)
    {
        return _ServerOn;
    }
    int CLanServer::GetEmptyStackUseSize(void)
    {
        if (_SessionEmptyStack != nullptr)
            return _SessionEmptyStack->GetUseSize();
        else
            return 0;
    }

    unsigned __int64 CLanServer::FindEmptySession(void)
    {
        // lockfree stack ver.
        unsigned __int64 EmptySessionIndex = -100;
        if (false == _SessionEmptyStack->Pop(&EmptySessionIndex))
            return -1;
        return EmptySessionIndex;
    }
    CLanServer::st_SESSION *CLanServer::FindSession(__int64 SessionID)
    {
        __int64 SessionIndex = ClientID_Index(SessionID);
        if (0 > SessionIndex || SessionIndex > _SessionMax - 1)
        {
            // Session Index가 잘못되었으므로 정지한다.             // 이건 바깥에서 체크한다.
            //CrashDump::Crash();
            return nullptr;
        }
        //if (_Session[SessionIndex].SessionID != SessionID)        // 이건 바깥에서 체크한다.
        //    return nullptr;
        return &_Session[SessionIndex];
    }
    int CLanServer::CompletePacket(st_SESSION *p_Session)
    {
        //if (true == DebugFlag)
        //{
        //    ProfileBegin(L"CompletePacket");
        //    ProfileBegin(L"CompletePacket 1");
        //}

        Packet *p_Packet;
        int RecvQSize;
        st_LAN_HEADER Header;

        // header size check
        RecvQSize = p_Session->RecvQ.GetUseSize();
        if (RecvQSize < sizeof(st_LAN_HEADER))
        {
            //if (true == DebugFlag)
            //{
            //    ProfileEnd(L"CompletePacket 1");
            //    ProfileEnd(L"CompletePacket");
            //}
            return Packet_NotComplete;
        }

        // payload size check
        p_Session->RecvQ.Peek((char *)&Header, sizeof(st_LAN_HEADER));
        if (Header.PayloadSize > Packet::BUFFER_SIZE_DEFAULT - Packet::HEADER_SIZE_MAX)
            return Packet_Error;

        if (Header.PayloadSize + sizeof(st_LAN_HEADER) > RecvQSize)
        {
            //if (true == DebugFlag)
            //{
            //    ProfileEnd(L"CompletePacket 1");
            //    ProfileEnd(L"CompletePacket");
            //}
            return Packet_NotComplete;
        }
        if (false == p_Session->RecvQ.RemoveData(sizeof(st_LAN_HEADER)))
            return Packet_Error;

        // get payload
        p_Packet = Packet::Alloc();
        if (nullptr == p_Packet)
        {
            CrashDump::Crash();
            return Packet_Error;
        }

        //p_Packet->SetHeader_CustomHeader((char *)&Header, sizeof(st_Header));
        p_Packet->SetHeader_CustomHeader_Short(Header.PayloadSize);
        if (Header.PayloadSize != p_Session->RecvQ.Get((char *)p_Packet->GetWriteBufferPtr(), Header.PayloadSize))
        {
            p_Packet->Free();
            //if (true == DebugFlag)
            //{
            //    ProfileEnd(L"CompletePacket 1");
            //    ProfileEnd(L"CompletePacket");
            //}
            return Packet_Error;
        }
        if (false == p_Packet->MoveWritePos(Header.PayloadSize))
        {
            p_Packet->Free();
            return Packet_Error;
        }

        //if (true == DebugFlag)
        //{
        //    ProfileEnd(L"CompletePacket 1");
        //
        //    ProfileBegin(L"CompletePacket 2");
        //}

        InterlockedIncrement(&_Monitor_RecvPacketCounter);

        //if (true == DebugFlag)
        //{
        //    ProfileEnd(L"CompletePacket 2");
        //
        //
        //    ProfileBegin(L"CompletePacket 3");
        //}

        // packet proc
        try
        {
            OnRecv(p_Session->SessionID, p_Packet);
        }
        catch (Packet::Exception_PacketOut err)     // 여기서는 딱 내것만 캐치하도록 할 것.
        {
            // 로그 찍어준다.
            SYSLOG(L"LanServer", LOG_ERROR, L"OnRecv [SessionID: %lld]", p_Session->SessionID);
            p_Packet->Free();
            return Packet_Error;
        }

        //if (true == DebugFlag)
        //    ProfileEnd(L"CompletePacket 3");

        p_Packet->Free();

        //if (true == DebugFlag)
        //    ProfileEnd(L"CompletePacket");
        return Packet_Complete;
    }
    bool CLanServer::RecvPost(st_SESSION *p_Session)
    {
        int BufCount;
        WSABUF RecvWsaBuf[2];
        DWORD flags;
        int Ret;

        flags = 0;

        BufCount = 1;
        RecvWsaBuf[0].buf = p_Session->RecvQ.GetWriteBufferPtr();
        RecvWsaBuf[0].len = p_Session->RecvQ.GetNotBrokenPutSize();

        // 리시브 큐가 꽉찼을 때.
        if (p_Session->RecvQ.GetFreeSize() <= 0)
            CrashDump::Crash();
        if (nullptr == RecvWsaBuf[0].buf || RecvWsaBuf[0].len <= 0)
            CrashDump::Crash();

        if (p_Session->RecvQ.GetNotBrokenPutSize() < p_Session->RecvQ.GetFreeSize())
        {
            RecvWsaBuf[1].buf = p_Session->RecvQ.GetBufferPtr();
            RecvWsaBuf[1].len = p_Session->RecvQ.GetFreeSize() - p_Session->RecvQ.GetNotBrokenPutSize();
            BufCount++;
        }

        memset(&p_Session->RecvOL, 0, sizeof(OVERLAPPED));
        InterlockedIncrement64(&p_Session->p_IOCompare->IOCount);
        Ret = WSARecv(p_Session->Sock, (LPWSABUF)&RecvWsaBuf, BufCount, NULL, &flags, &p_Session->RecvOL, NULL);
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            if (Ret != ERROR_IO_PENDING)
            {
                // 이 때만 로그를 남긴다.
                if (Ret != WSAECONNABORTED && Ret != WSAECONNRESET && Ret != WSAESHUTDOWN)
                {
                    SYSLOG(L"WSARecv", LOG_SYSTEM, L"[%d]%s", Ret, L"WSA Recv Error");
                }

                ClientShutdown(p_Session);
                if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
                {
                    ClientRelease(p_Session);
                }
                return false;
            }
        }

        return true;
    }
    bool CLanServer::SendPost(__int64 SessionID)
    {
        st_SESSION *p_Session;

        p_Session = FindSession(SessionID);
        if (nullptr == p_Session)
        {
            CrashDump::Crash();
            return false;
        }

    RETRY:
        if (InterlockedCompareExchange64(&p_Session->SendIO, 1, 0) != 0)        // 0이라면 1로 바꾼다.
            return false;

        //if (true == DebugFlag)
        //{
        //    ProfileBegin(L"SendPost");
        //    ProfileBegin(L"SendPost 1");
        //}

        int Cnt;
        int BufCount;
        int BufSize;
        WSABUF SendWsaBuf[SEND_WSA_BUF_MAX];
        Packet *p_Packet;
        int Ret;

        BufCount = 0;
        BufSize = 0;
        if (p_Session->SendQ.GetUseSize() > 0)
        {
            for (Cnt = 0; Cnt < SEND_WSA_BUF_MAX; ++Cnt)
            {
                if (false == p_Session->SendQ.Peek(&p_Packet, Cnt))
                    break;

                SendWsaBuf[Cnt].buf = p_Packet->GetHeaderBufferPtr_CustomHeader(sizeof(st_LAN_HEADER));
                SendWsaBuf[Cnt].len = p_Packet->GetPacketSize_CustomHeader(sizeof(st_LAN_HEADER));
                BufCount++;
                BufSize += p_Packet->GetPacketSize_CustomHeader(sizeof(st_LAN_HEADER));
            }
        }
        //if (true == DebugFlag)
        //    ProfileEnd(L"SendPost 1");

        if (0 == BufCount)
        {
            InterlockedExchange64(&p_Session->SendIO, 0);

            //if (true == DebugFlag)
            //    ProfileEnd(L"SendPost");

            if (p_Session->SendQ.GetUseSize() > 0)
                goto RETRY;

            return false;
        }

        //if (true == DebugFlag)
        //    ProfileBegin(L"SendPost 2");

        p_Session->SendPacketCnt = BufCount;
        p_Session->SendPacketSize = BufSize;

        memset(&p_Session->SendOL, 0, sizeof(OVERLAPPED));
        InterlockedIncrement64(&p_Session->p_IOCompare->IOCount);

        // sendpacket에서 호출할 경우 릴리즈 된 상태에서 들어오는 경우가 있다. -> 현재는 없어야 한다.
        if (SessionID != p_Session->SessionID)
            CrashDump::Crash();

        Ret = WSASend(p_Session->Sock, (LPWSABUF)&SendWsaBuf, BufCount, NULL, 0, &p_Session->SendOL, NULL);
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            if (Ret != ERROR_IO_PENDING)
            {
                // 이 때만 로그를 남긴다.
                if (Ret != WSAECONNABORTED && Ret != WSAECONNRESET && Ret != WSAESHUTDOWN)
                {
                    SYSLOG(L"WSASend", LOG_SYSTEM, L"[%d]%s", Ret, L"WSA Send Error");
                    //OnError(Ret, L"WSA Send Error");
                }

                // 이미 끊긴 애 다시 디스커넥트(소켓 연결만 끊고 메모리는 그대로) 할 것임. -> 아름다운 종료 그딴건 없다.
                ClientShutdown(p_Session);

                // send io 0으로 초기화
                InterlockedExchange64(&p_Session->SendIO, 0);

                if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
                {
                    ClientRelease(p_Session);
                }

                //if (true == DebugFlag)
                //{
                //    ProfileEnd(L"SendPost 2");
                //    ProfileEnd(L"SendPost");
                //}
                return false;
            }
        }

        //if (true == DebugFlag)
        //{
        //    ProfileEnd(L"SendPost 2");
        //    ProfileEnd(L"SendPost");
        //}

        return true;
    }
    void CLanServer::CompleteRecv(st_SESSION *p_Session, DWORD cbTransferred)
    {
        //if (true == DebugFlag)
        //{
        //    ProfileBegin(L"CompleteRecv");
        //    ProfileBegin(L"CompleteRecv 1");
        //}

        // 문제가 있는 상황. 죽어랏!!
        if (0 == p_Session->RecvQ.MoveWritePtr(cbTransferred))
            CrashDump::Crash();

        //if (true == DebugFlag)
        //{
        //    ProfileEnd(L"CompleteRecv 1");
        //
        //    ProfileBegin(L"CompleteRecv 2");
        //}

        // 패킷 처리
        int CompletePacketResult;
        while (1)
        {
            CompletePacketResult = CompletePacket(p_Session);
            if (Packet_Complete == CompletePacketResult)
                continue;
            else if (Packet_NotComplete == CompletePacketResult)
                break;
            else
            {
                // 로그는 안에서 찍는다.
                //SYSLOG(L"LandServer", LOG_ERROR, L"[%d]%s", p_Session->SessionID);
                //OnError(CompletePacketResult, L"[Invalid CompletePacketResult]");

                ClientDisconnect(p_Session);        // 얘는 리시브도 막는다.
                //ClientShutdown(p_Session);
                return;
            }
        }

        //if (true == DebugFlag)
        //{
        //    ProfileEnd(L"CompleteRecv 2");
        //
        //    ProfileBegin(L"CompleteRecv 3");
        //}

        RecvPost(p_Session);

        //if (true == DebugFlag)
        //{
        //    ProfileEnd(L"CompleteRecv 3");
        //
        //    ProfileEnd(L"CompleteRecv");
        //}
    }
    void CLanServer::CompleteSend(st_SESSION *p_Session, DWORD cbTransferred)
    {
        if (cbTransferred != p_Session->SendPacketSize)
        {
            // 이 경우는 끊어야 하지만 일단 죽여보자.
            CrashDump::Crash();
            ClientShutdown(p_Session);
            return;
        }

        //if (true == DebugFlag)
        //    ProfileBegin(L"CompleteSend");

        int Cnt;
        Packet *p_Packet;
        for (Cnt = 0; Cnt < p_Session->SendPacketCnt; ++Cnt)
        {
            if (false == p_Session->SendQ.Dequeue(&p_Packet))
            {
                // 나와서는 안되는 상황. 죽어랏!
                CrashDump::Crash();
                return;
            }
            //InterlockedDecrement(&p_Session->SendQueueCnt);

            p_Packet->Free();

            // debug
            //if (p_Packet->GetRefCount() != 0)
            //    int i = 0;

            //InterlockedDecrement(&_SendQueueCount);
        }
        InterlockedAdd(&_Monitor_SendPacketCounter, p_Session->SendPacketCnt);
        OnSend(p_Session->SessionID, cbTransferred);
        InterlockedExchange64(&p_Session->SendIO, 0);

        SendPost(p_Session->SessionID);
        //if (p_Session->SendQ.GetUseSize() > 0)
        //    SendPost(p_Session->SessionID);
        //SendPost(p_Session);

        //if (true == DebugFlag)
        //    ProfileEnd(L"CompleteSend");
    }
    void CLanServer::ClientShutdown(st_SESSION *p_Session)              // shutdown으로 안전한 종료 유도
    {
        shutdown(p_Session->Sock, SD_SEND);
    }
    void CLanServer::ClientDisconnect(st_SESSION *p_Session)            // socket 접속 강제 끊기
    {
        shutdown(p_Session->Sock, SD_BOTH);     // shutdown both
    }
    void CLanServer::SocketClose(SOCKET SessionSock)                    // closesocket(내부용)
    {
        linger Ling;
        Ling.l_onoff = 1;
        Ling.l_linger = 0;
        setsockopt(SessionSock, SOL_SOCKET, SO_LINGER, (char *)&Ling, sizeof(LINGER));
        closesocket(SessionSock);
    }
    void CLanServer::ClientRelease(st_SESSION *p_Session)               // client session 해제
    {
        st_IO_COMPARE ReleaseCompare;
        ReleaseCompare.IOCount = 0;
        ReleaseCompare.ReleaseFlag = 0;

        //st_Session *p_Session;
        //p_Session = FindSession(SessionID);
        //if (nullptr == p_Session)
        //    return;

        if (InterlockedCompareExchange128((LONG64 *)p_Session->p_IOCompare, 1, 0, (LONG64 *)&ReleaseCompare) != 1)
            return;

        // debug
        //p_Session->ReleaseType = Type;
        //p_Session->JoinFlag--;

        // ClientLeave를 여기서 호출한다.
        OnClientLeave(p_Session->SessionID);

        SocketClose(p_Session->Sock);

        Packet *p_Packet;
        int ReleaseSessionIndex;

        // Send Queue Clear
        while (1)
        {
            if (false == p_Session->SendQ.Dequeue(&p_Packet))
                break;
            //InterlockedDecrement(&p_Session->SendQueueCnt);
            p_Packet->Free();
            //InterlockedDecrement(&_SendQueueCount);
        }

        // Recv Queue Clear
        p_Session->RecvQ.Clear();

        ReleaseSessionIndex = ClientID_Index(p_Session->SessionID);

        p_Session->SessionID = SESSION_ID_DEFAULT;
        p_Session->Sock = INVALID_SOCKET;
        memset(&p_Session->SessionAddr, 0, sizeof(SOCKADDR_IN));

        InterlockedDecrement64(&_SessionCount);
        _SessionEmptyStack->Push(ReleaseSessionIndex);
    }

    unsigned __stdcall CLanServer::AcceptThreadProc(LPVOID lpParam)
    {
        return ((CLanServer *)lpParam)->AccpetThread_Update();
    }
    //unsigned __stdcall CLanServer::AcceptThreadProc(LPVOID lpParam)
    unsigned CLanServer::AccpetThread_Update(void)
    {
        //CLanServer *p_This;
        int SessionAddrLen;
        sockaddr_in SessionAddr;
        SOCKET SessionSock;
        LONG64 SessionIndex;
        HANDLE hResult;
        DWORD RecvFlags;
        st_SESSION *p_Session;
        int Ret;

        BOOL NoDelayOptVal;
        DWORD KeepAliveResult;
        tcp_keepalive Tcpkl;

        // Recv Flag Init
        RecvFlags = 0;

        // No Delay Setting
        NoDelayOptVal = true;               // No Delay 켠다.

        // keep alive setting
        Tcpkl.onoff = 1;
        Tcpkl.keepalivetime = 30000;        // 30초마다 keepalive 신호를 보내겠다.(윈도우 기본은 2시간)
        Tcpkl.keepaliveinterval = 2000;     // keepalive 신호를 보내고 응답이 없으면 2초마다 재전송. (ms tcp는 10회 재시도 한다.)

        //SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", 0, L"Accept Start");

        //p_This = (CLanServer *)lpParam;
        while (1)
        {
            SessionAddrLen = sizeof(SessionAddr);
            //SessionSock = accept(p_This->_ListenSock, (sockaddr *)&SessionAddr, &SessionAddrLen);
            //SessionSock = WSAAccept(p_This->_ListenSock, (sockaddr *)&SessionAddr, &SessionAddrLen, NULL, NULL);
            SessionSock = WSAAccept(_ListenSock, (sockaddr *)&SessionAddr, &SessionAddrLen, NULL, NULL);
            if (INVALID_SOCKET == SessionSock)
            {
                // 여기는 종료할 때 탄다.
                int ErrorCode = WSAGetLastError();
                SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", ErrorCode, L"Accept Exit");
                return 0;
            }

            // Accept Counter 증가
            //p_This->_Monitor_AcceptCounter++;
            _Monitor_AcceptCounter++;

            // NoDelay
            //if (false == p_This->_Nagle)
            if (false == _Nagle)
            {
                Ret = setsockopt(SessionSock, IPPROTO_TCP, TCP_NODELAY, (char *)&NoDelayOptVal, sizeof(BOOL));
                if (Ret != 0)
                {
                    CrashDump::Crash();
                    //p_This->SocketClose(SessionSock);
                    SocketClose(SessionSock);
                    continue;
                }
            }

            // keep alive
            Ret = WSAIoctl(SessionSock, SIO_KEEPALIVE_VALS, &Tcpkl, sizeof(tcp_keepalive), 0, 0, &KeepAliveResult, NULL, NULL);
            if (Ret != 0)
            {
                CrashDump::Crash();
                continue;
            }

            //if (false == p_This->OnConnectionRequest(SessionAddr.sin_addr, SessionAddr.sin_port))
            if (false == OnConnectionRequest(SessionAddr.sin_addr, SessionAddr.sin_port))
            {
                //p_This->SocketClose(SessionSock);
                SocketClose(SessionSock);
                continue;
            }

            //SessionIndex = p_This->FindEmptySession();
            SessionIndex = FindEmptySession();
            if (-1 == SessionIndex)
            {
                //SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", p_This->_SessionCount, L"Server Full Error");
                SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", _SessionCount, L"Server Full Error");
                //p_This->OnError(p_This->_SessionCount, L"Server Full Error");
                //p_This->SocketClose(SessionSock);
                SocketClose(SessionSock);
                continue;
            }
            //p_Session = &p_This->_Session[SessionIndex];
            p_Session = &_Session[SessionIndex];

            // 먼저 올리고 세팅한다.
            InterlockedIncrement64(&p_Session->p_IOCompare->IOCount);
            InterlockedCompareExchange64(&p_Session->p_IOCompare->ReleaseFlag, 0, 1);

            p_Session->SessionID = NewClientID(SessionIndex);
            p_Session->SessionAddr = SessionAddr;
            p_Session->Sock = SessionSock;
            memset(&p_Session->RecvOL, 0, sizeof(OVERLAPPED));
            memset(&p_Session->SendOL, 0, sizeof(OVERLAPPED));
            //p_Session->p_IOCompare->IOCount = 0;      // 일단 주석처리한다.
            //p_Session->p_IOCompare->ReleaseFlag = 0;
            p_Session->SendPacketCnt = 0;
            p_Session->SendPacketSize = 0;
            p_Session->SendIO = 0;

            //hResult = CreateIoCompletionPort((HANDLE)p_Session->Sock, p_This->_hIocp, (ULONG_PTR)p_Session, 0);
            hResult = CreateIoCompletionPort((HANDLE)p_Session->Sock, _hIocp, (ULONG_PTR)p_Session, 0);
            if (NULL == hResult)
            {
                CrashDump::Crash();
                int ErrorCode = GetLastError();
                SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", ErrorCode, L"Sock IOCP Error");
                //p_This->OnError(1, L"Sock IOCP Error");
                return -1;
            }

            //InterlockedIncrement64(&p_This->_SessionCount);         // recvpost 걸기 전에 걸면 clientrelease로 빠져서 카운트가 마이너스가 된다.
            InterlockedIncrement64(&_SessionCount);         // recvpost 걸기 전에 걸면 clientrelease로 빠져서 카운트가 마이너스가 된다.

            //p_This->OnClientJoin(p_Session->SessionID);    // Recv 거는 것보다 OnClientJoin이 먼저 들어가야함.
            //p_This->RecvPost(p_Session);
            OnClientJoin(p_Session->SessionID);    // Recv 거는 것보다 OnClientJoin이 먼저 들어가야함.
            RecvPost(p_Session);
            if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
            {
                //p_This->ClientReleaseDebug(p_Session, 3);
                //p_This->ClientRelease(p_Session);
                ClientRelease(p_Session);
                continue;
            }
        }

        return 0;
    }
    unsigned __stdcall CLanServer::WorkerThreadProc(LPVOID lpParam)
    {
        return ((CLanServer *)lpParam)->WorkerThread_Update();
    }
    //unsigned __stdcall CLanServer::WorkerThreadProc(LPVOID lpParam)
    unsigned CLanServer::WorkerThread_Update(void)
    {
        //CLanServer *p_This;
        BOOL Ret;
        DWORD cbTransferred;
        st_SESSION *p_Session;
        LPOVERLAPPED lpOverlapped;
        DWORD ErrorCode;

        //p_This = (CLanServer *)lpParam;
        while (1)
        {
            cbTransferred = 0;
            p_Session = nullptr;
            lpOverlapped = nullptr;

            //Ret = GetQueuedCompletionStatus(p_This->_hIocp, &cbTransferred, (PULONG_PTR)&p_Session, &lpOverlapped, INFINITE);
            Ret = GetQueuedCompletionStatus(_hIocp, &cbTransferred, (PULONG_PTR)&p_Session, &lpOverlapped, INFINITE);

            // 종료
            if (0 == cbTransferred && nullptr == p_Session && nullptr == lpOverlapped)
            {
                return 0;
            }

            //if (true == DebugFlag)
            //    ProfileBegin(L"GQCS IOComplete");

            //p_This->OnWorkerThreadBegin(p_Session->SessionID);
            OnWorkerThreadBegin(p_Session->SessionID);

            if (FALSE == Ret)
            {
                ErrorCode = GetLastError();
                if (ErrorCode != ERROR_NETNAME_DELETED && ErrorCode != ERROR_SEM_TIMEOUT)
                {
                    SYSLOG(L"GQCS", LOG_ERROR, L"[%d]%s", ErrorCode, L"GQCS Error");
                    //p_This->OnError(ErrorCode, L"GQCS Error");
                }
                //p_This->ClientShutdown(p_Session);
                ClientShutdown(p_Session);
            }
            else if (0 == cbTransferred)
            {
                //p_This->ClientShutdown(p_Session);
                ClientShutdown(p_Session);
            }
            else
            {
                if (&p_Session->RecvOL == lpOverlapped)
                {
                    //p_This->CompleteRecv(p_Session, cbTransferred);
                    CompleteRecv(p_Session, cbTransferred);
                }
                else if (&p_Session->SendOL == lpOverlapped)
                {
                    //p_This->CompleteSend(p_Session, cbTransferred);
                    CompleteSend(p_Session, cbTransferred);
                }
                else
                {
                    CrashDump::Crash();
                    SYSLOG(L"GQCS", LOG_ERROR, L"[%d]%s", 0, L"Invalid Overlappped");
                    //p_This->OnError(1, L"GQCS Error [Invalid Overlappped]");
                    //p_This->ClientShutdown(p_Session);
                    ClientShutdown(p_Session);
                }
            }

            if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
            {
                //p_This->ClientRelease(p_Session);
                ClientRelease(p_Session);
            }
            //p_This->OnWorkerThreadEnd(p_Session->SessionID);
            OnWorkerThreadEnd(p_Session->SessionID);

            //if (true == DebugFlag)
            //    ProfileEnd(L"GQCS IOComplete");
        }

        return 0;
    }
    unsigned __stdcall CLanServer::MonitorTPS_Thread(LPVOID lpParam)
    {
        return ((CLanServer *)lpParam)->MonitorTPS_Thread_Update();
    }
    unsigned CLanServer::MonitorTPS_Thread_Update(void)
    {
        while (1)
        {
            _Monitor_AcceptTPS = _Monitor_AcceptCounter;
            _Monitor_RecvPacketTPS = _Monitor_RecvPacketCounter;
            _Monitor_SendPacketTPS = _Monitor_SendPacketCounter;

            _Monitor_AcceptCounter = 0;
            _Monitor_RecvPacketCounter = 0;
            _Monitor_SendPacketCounter = 0;

            Sleep(999);
        }
        return 0;
    }
}