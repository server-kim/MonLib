#include "LibHeader.h"

namespace MonLib
{
    CNetServerEx3::CNetServerEx3(int SessionMax, bool Nagle)
    {
        if (SessionMax < 1)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"SessionMax Invalid");
            exit(0);
        }

        int Cnt;

        // 세션 키 인덱스 초기화
        _SessionKeyIndex = 0;

        // 타임 매니저 가져오기
        _TimeManager = TimeManager::GetInstance();

        // 패킷풀 초기화
        Packet::MemoryPool(PACKET_POOL_CHUNK_SIZE);

        // 프로파일러 초기화
        ProfileInitial();

        // member variable init
        _ServerOn                       = false;
        _ListenSock                     = INVALID_SOCKET;

        wmemset(_IP, 0, IP_V4_MAX_LEN + 1);
        _Port                           = 0;
        _WorkerThreadMax                = 0;
        _SessionMax                     = SessionMax;
        _Nagle                          = Nagle;

        _SessionCount                   = 0;
        _Session                        = nullptr;
        _SessionEmptyStack              = nullptr;

        _hIocp = INVALID_HANDLE_VALUE;
        for (Cnt = 0; Cnt < WORKER_THREAD_MAX_COUNT; ++Cnt)
        {
            _WorkerThread[Cnt] = INVALID_HANDLE_VALUE;
            _WorkerThreadId[Cnt] = 0;
        }

        //_AcceptThread = INVALID_HANDLE_VALUE;
        //_AcceptThreadId = 0;

        // debug
        _Monitor_AcceptTotal            = 0;

        // monitoring 변수 초기화
        _Monitor_AcceptTPS              = 0;
        _Monitor_RecvPacketTPS          = 0;
        _Monitor_SendPacketTPS          = 0;

        _Monitor_AcceptCounter          = 0;
        _Monitor_RecvPacketCounter      = 0;
        _Monitor_SendPacketCounter      = 0;

        // AcceptEx
        _lpfnAcceptEx = NULL;
        //_NetworkSocket = nullptr;

        // DisconnectEx
        _lpfnDisconnectEx = NULL;

        _SendThread = INVALID_HANDLE_VALUE;
        _SendThreadId = 0;

        // winsock init
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            CrashDump::Crash();
            return;
        }

        _SocketPool = new SocketPool(_SessionMax * 2);
    }
    CNetServerEx3::~CNetServerEx3(void)
    {
        stop();
        WSACleanup();
    }

    bool CNetServerEx3::start(WCHAR *p_IP, USHORT Port, int WorkerThreadCnt, BYTE PacketCode, BYTE XORCode1, BYTE XORCode2, bool SendThread)
    {
        if (true == _ServerOn)
            return false;
        _ServerOn = true;

        if (nullptr == p_IP)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"IP Address Invalid");
            return false;
        }

        int Cnt;

        // 서버 설정값 세팅
        wmemcpy_s(_IP, IP_V4_MAX_LEN + 1, p_IP, IP_V4_MAX_LEN + 1);
        _Port = Port;

        // determine worker thread count
        _WorkerThreadMax = WORKER_THREAD_MAX_COUNT;
        if (WorkerThreadCnt > 0 && WorkerThreadCnt < WORKER_THREAD_MAX_COUNT)
            _WorkerThreadMax = WorkerThreadCnt;
        
        // 프로토콜 및 암호화 패킷 변수 세팅
        Packet::_PacketCode = PacketCode;
        Packet::_XORCode1 = XORCode1;
        Packet::_XORCode2 = XORCode2;

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
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"CreateIoCompletionPort Failed with Error: %u", GetLastError());
            return false;
        }

        // 워커스레드 생성
        for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        {
            //_WorkerThread[Cnt] = (HANDLE)_beginthreadex(NULL, 0, WorkerThreadProc, this, 0, &_WorkerThreadId[Cnt]);
            _WorkerThread[Cnt] = (HANDLE)_beginthreadex(NULL, 0, WorkerExThreadProc, this, 0, &_WorkerThreadId[Cnt]);
            if (INVALID_HANDLE_VALUE == _WorkerThread[Cnt])
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"WorkerThread Creation Error");
                return false;
            }
        }

        // 워커스레드EX 로 옮긴다.
        //// AcceptEx Thread
        //_hIocpAccept = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, ACCEPT_EX_THREAD_MAX_COUNT);
        //if (NULL == _hIocpAccept)
        //{
        //    SYSLOG(L"SYSTEM", LOG_SYSTEM, L"CreateIoCompletionPort Failed with Error: %u", GetLastError());
        //    return false;
        //}
        //
        //for (Cnt = 0; Cnt < ACCEPT_EX_THREAD_MAX_COUNT; ++Cnt)
        //{
        //    _AcceptExThread[Cnt] = (HANDLE)_beginthreadex(NULL, 0, AcceptExThreadProc, this, 0, &_AcceptExThreadId[Cnt]);
        //    if (INVALID_HANDLE_VALUE == _AcceptExThread[Cnt])
        //    {
        //        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"AcceptThread Creation Error");
        //        return false;
        //    }
        //}

        // Network Init
        if (false == NetworkInit())
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Network Init Error");
            return false;
        }

        // DisconnectEx Init
        if (false == DisconnectExInit())
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"DisconnectEx Init Error");
            return false;
        }

        // AcceptEx Init
        if (false == AcceptExInit())
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"AcceptEx Init Error");
            return false;
        }

        // Prepare Accept Socket
        st_NETWORK_SOCKET *p_NetworkSocket;
        for (Cnt = 0; Cnt < _SessionMax; ++Cnt)
        {
            if (false == _SocketPool->AllocSocket(&p_NetworkSocket))
            {
                closesocket(_ListenSock);
                return false;
            }
            if (false == SetAcceptEx(&_ListenSock, p_NetworkSocket))
            {
                closesocket(_ListenSock);
                return false;
            }
        }
        
        // send thread 생성
        if (true == SendThread)
        {
            _SendThread = (HANDLE)_beginthreadex(NULL, 0, SendThreadProc, this, 0, &_SendThreadId);
            if (INVALID_HANDLE_VALUE == _SendThread)
            {
                CrashDump::Crash();
                return false;
            }
        }

        // monitoring thread 생성
        _MonitorTPSThread = (HANDLE)_beginthreadex(NULL, 0, MonitorTPS_Thread, this, 0, &_MonitorThreadId);
        if (INVALID_HANDLE_VALUE == _MonitorTPSThread)
        {
            CrashDump::Crash();
            return false;
        }

        return true;
    }
    bool CNetServerEx3::NetworkInit(void)
    {
        //----------------------------------------------------
        // network init
        //----------------------------------------------------

        HANDLE hResult;
        SOCKADDR_IN ServerAddr;
        int Ret;

        // socket 생성
        //_ListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        _ListenSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (INVALID_SOCKET == _ListenSock)
        {
            Ret = WSAGetLastError();
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Create of ListenSocket Failed with Error: %d", Ret);
            return false;
        }

        // 워커스레드EX 로 옮긴다.
        // AccpetEx : Associate the listening socket with the completion port
        //hResult = CreateIoCompletionPort((HANDLE)_ListenSock, _hIocpAccept, (ULONG_PTR)&_ListenSock, 0);
        //hResult = CreateIoCompletionPort((HANDLE)_ListenSock, _hIocp, (ULONG_PTR)&_ListenSock, 0);
        hResult = CreateIoCompletionPort((HANDLE)_ListenSock, _hIocp, (ULONG_PTR)COMPLETION_KEY_OVERLAPPED, 0);
        if (NULL == hResult)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Listen Socket IOCP Error: %u", GetLastError());
            closesocket(_ListenSock);
            return false;
        }

        // binding
        ServerAddr.sin_family = AF_INET;
        ServerAddr.sin_port = htons(_Port);
        InetPton(AF_INET, _IP, &(ServerAddr.sin_addr));
        Ret = bind(_ListenSock, (sockaddr *)&ServerAddr, sizeof(ServerAddr));
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Listen Socket Bind Error: %d", Ret);
            closesocket(_ListenSock);
            return false;
        }

        // AccpetEx : AcceptEx가 동작중일 때 받는다.
        BOOL OptVal = TRUE;
        Ret = setsockopt(_ListenSock, SOL_SOCKET, SO_CONDITIONAL_ACCEPT, (char *)&OptVal, sizeof(OptVal));
        if (Ret != 0)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Listen Socket setsockopt Error");
            closesocket(_ListenSock);
            return false;
        }

        // listen
        Ret = listen(_ListenSock, SOMAXCONN);
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Listen Socket Listen Error: %d", Ret);
            closesocket(_ListenSock);
            return false;
        }

        return true;
    }
    bool CNetServerEx3::AcceptExInit(void)
    {
        GUID GuidAcceptEx;
        DWORD dwBytes;
        int Ret;
        
        GuidAcceptEx = WSAID_ACCEPTEX;
        _lpfnAcceptEx = NULL;
        
        Ret = WSAIoctl(_ListenSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
            &GuidAcceptEx, sizeof(GuidAcceptEx),
            &_lpfnAcceptEx, sizeof(_lpfnAcceptEx),
            &dwBytes, nullptr, nullptr);
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"AcceptEx # [WSAIoctl Failed with Error: %d]", Ret);
            closesocket(_ListenSock);
            return false;
        }
        return true;
    }
    bool CNetServerEx3::DisconnectExInit(void)
    {
        GUID GuidDisconnectEx;
        DWORD dwBytes;
        int Ret;

        GuidDisconnectEx = WSAID_DISCONNECTEX;
        _lpfnDisconnectEx = NULL;

        Ret = WSAIoctl(_ListenSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
            &GuidDisconnectEx, sizeof(GuidDisconnectEx),
            &_lpfnDisconnectEx, sizeof(_lpfnDisconnectEx),
            &dwBytes, nullptr, nullptr);
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"DisconnectEx # [WSAIoctl Failed with Error: %d]", Ret);
            closesocket(_ListenSock);
            return false;
        }
        return true;
    }
    bool CNetServerEx3::SetAcceptEx(SOCKET *ListenSock, st_NETWORK_SOCKET *p_NetworkSocket)
    {
        int Ret;
        BOOL bRet;
        DWORD dwBytes;

        memset(p_NetworkSocket, 0, sizeof(OVERLAPPED));

        p_NetworkSocket->Type = OVERLAPPED_TYPE_ACCEPT;
        p_NetworkSocket->_p_Data = &_ListenSock;
        //p_NetworkSocket->_Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        //if (INVALID_SOCKET == p_NetworkSocket->_Sock)
        //{
        //    Ret = WSAGetLastError();
        //    SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Create accept socket Failed with Error: %d", Ret);
        //    //closesocket(ListenSock);
        //    return false;
        //}

        bRet = _lpfnAcceptEx(_ListenSock, p_NetworkSocket->_Sock, p_NetworkSocket->_Buf,
            0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
            &dwBytes, (LPOVERLAPPED)p_NetworkSocket);
        if (FALSE == bRet)
        {
            Ret = WSAGetLastError();
            if (Ret != ERROR_IO_PENDING)
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"AcceptEx Failed with Error: %d", Ret);
                closesocket(p_NetworkSocket->_Sock);
                //closesocket(ListenSock);
                return false;
            }
        }

        // p_AcceptOL->_Sock에 iocp 거는것은 accept 받은 이후로 한다.

        return true;
    }
    void CNetServerEx3::stop(void)
    {
        if (false == _ServerOn)
            return;

        int Cnt;
        bool SessionShutdown;
        int SessionShutdownTryCount;
        bool DisconnectFlag;
        int DisconnectTryCount;

        // 1. Accept 중지                 // close listen socket
        closesocket(_ListenSock);
        _ListenSock = INVALID_SOCKET;
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [1][Listen Socket Closed]");

        // 2. Accept Thread 중지 확인.    // accept thread 종료대기
        //if (WaitForSingleObject(_AcceptThread, 5000) != WAIT_OBJECT_0)
        //{
        //    SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [Error][Terminate Accept Thread]");
        //    TerminateThread(_AcceptThread, 0);
        //}
        //SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [2][Accept Thread Exit]");
        //CloseHandle(_AcceptThread);
        //_AcceptThread = INVALID_HANDLE_VALUE;
        //_AcceptThreadId = 0;
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [2][Accept Thread Exit]");

        // 3. Session Shutdown          // client graceful close(여기서 끊길 수 있는 상황 : 1. 네트워크 끊겼을 때, 2. 클라가 먹통이 되었을 때)
        // 샌드큐에 보낼게 있으면 트라이카운트 10(1000ms)까지 올리면서 기다려줌.
        for (Cnt = 0; Cnt < _SessionMax; ++Cnt)
        {
            //if (INVALID_SOCKET == _Session[Cnt].Sock)
            //    continue;
            if (_Session[Cnt].SessionID <= 0)
            {
                if (SESSION_ID_DEFAULT == _Session[Cnt].SessionID)
                    continue;
                else
                    CrashDump::Crash();
            }

            SessionShutdown = false;
            SessionShutdownTryCount = 10;
            while (SessionShutdownTryCount-- > 0)
            {
                if (_Session[Cnt].SendQ.GetUseSize() <= 0)
                {
                    ClientShutdown(&_Session[Cnt]);
                    SessionShutdown = true;
                    break;
                }
                Sleep(100);
            }

            if (false == SessionShutdown)
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [Error][Session Send Queue is Not Empty][SendQ Size:%d]", _Session[Cnt].SendQ.GetUseSize());
            }
        }
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [3][Session Shutdown]");
        
        // 4. 세션이 종료되었는지 검사.            // 모든 애들을 다 검사해서 Connect Count가 0이 될때까지 기다린다.
        for (Cnt = 0; Cnt < _SessionMax; ++Cnt)
        {
            if (SESSION_ID_DEFAULT == _Session[Cnt].SessionID && 1 == _Session[Cnt].p_IOCompare->ReleaseFlag)
                continue;

            DisconnectFlag = false;
            DisconnectTryCount = 10;
            while (DisconnectTryCount-- > 0)
            {
                if (SESSION_ID_DEFAULT == _Session[Cnt].SessionID && 1 == _Session[Cnt].p_IOCompare->ReleaseFlag)
                {
                    DisconnectFlag = true;
                    break;
                }
                Sleep(100);
            }

            if (false == DisconnectFlag)
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [Error][Session is Not Disconnected]");
            }
        }
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [4][Session Disconnected]");
        
        // 5. worker thread exit
        for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        {
            PostQueuedCompletionStatus(_hIocp, 1, (ULONG_PTR)COMPLETION_KEY_EXIT, NULL);
        }
        // 워커스레드 중단 확인(이게 생각보다 시간이 오래걸림.)
        for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        {
            if (WaitForSingleObject(_WorkerThread[Cnt], 10000) != WAIT_OBJECT_0)
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [Error][Terminate Worker Thread]");
                TerminateThread(_WorkerThread[Cnt], 0);
            }
        }
        // 워커스레드 핸들 반환
        for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        {
            CloseHandle(_WorkerThread[Cnt]);
            _WorkerThread[Cnt] = INVALID_HANDLE_VALUE;
            _WorkerThreadId[Cnt] = 0;
        }
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [5][Worker Thread Exit]");
        
        // 6. iocp 파괴           // iocp init
        CloseHandle(_hIocp);
        _hIocp = INVALID_HANDLE_VALUE;
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [6][Iocp Released]");

        // 7. 세션 메모리 반환     // Session 배열 해제
        delete[] _Session;
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [7][Session Released]");

        // 8. 멤버변수 초기화
        _Session = nullptr;
        _SessionCount = 0;

        // Session Stack 해제
        delete _SessionEmptyStack;
        _SessionEmptyStack = nullptr;

        // AcceptEx
        //delete[] _NetworkSocket;
        //_NetworkSocket = nullptr;
        delete _SocketPool;

        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [8][Final]");

        _ServerOn = false;
    }
    LONG64 CNetServerEx3::GetSessionCount(void)
    {
        return _SessionCount;
    }
    bool CNetServerEx3::SendPacketSendThread(__int64 SessionID, Packet *p_Packet, bool Disconnect)
    {
        st_SESSION *p_Session;

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

        if (1 == p_Session->p_IOCompare->ReleaseFlag)
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

        // 암호화
        p_Packet->Encode();

        // 패킷을 인큐한다.
        p_Packet->addRef();
        if (false == p_Session->SendQ.Enqueue(p_Packet))
        {
            CrashDump::Crash();
            return false;
        }

        if (true == Disconnect)
            p_Session->SendDisconnectFlag = true;

        if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
        {
            ClientRelease(p_Session);
            return false;
        }

        return true;
    }
    bool CNetServerEx3::GetServerOn(void)
    {
        return _ServerOn;
    }
    __int64 CNetServerEx3::GetSocketUseCount(void)
    {
        return _SocketPool->GetUseCount();
    }
    __int64 CNetServerEx3::GetSocketAllocCount(void)
    {
        return _SocketPool->GetAllocCount();
    }
    int CNetServerEx3::GetEmptyStackUseSize(void)
    {
        if (_SessionEmptyStack != nullptr)
            return _SessionEmptyStack->GetUseSize();
        else
            return 0;
    }

    bool CNetServerEx3::Disconnect(__int64 ClientID)
    {
        __int64 SessionID = ClientID;
        st_SESSION *p_Session = FindSession(SessionID);
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

        if (1 == p_Session->p_IOCompare->ReleaseFlag)
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

        //ClientShutdown(p_Session);
        ClientDisconnect(p_Session);

        if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
        {
            ClientRelease(p_Session);
            return false;
        }
        return true;
    }

    unsigned __int64 CNetServerEx3::FindEmptySession(void)
    {
        // lockfree stack ver.
        unsigned __int64 EmptySessionIndex = -100;
        if (false == _SessionEmptyStack->Pop(&EmptySessionIndex))
            return -1;
        return EmptySessionIndex;
    }
    CNetServerEx3::st_SESSION *CNetServerEx3::FindSession(__int64 SessionID)
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
    int CNetServerEx3::CompletePacket(st_SESSION *p_Session)
    {
        Packet *p_Packet;
        int RecvQSize;
        st_PACKET_HEADER Header;

        // header size check
        RecvQSize = p_Session->RecvQ.GetUseSize();
        if (RecvQSize < sizeof(st_PACKET_HEADER))
            return Packet_NotComplete;

        // payload size check
        p_Session->RecvQ.Peek((char *)&Header, sizeof(st_PACKET_HEADER));
        if (Header.Code != Packet::_PacketCode)
            return Packet_Error;
        if (Header.Len > Packet::BUFFER_SIZE_DEFAULT - Packet::HEADER_SIZE_MAX)
            return Packet_Error;

        if (Header.Len + sizeof(st_PACKET_HEADER) > RecvQSize)
            return Packet_NotComplete;
        if (false == p_Session->RecvQ.RemoveData(sizeof(st_PACKET_HEADER)))
            return Packet_Error;

        // get payload
        p_Packet = Packet::Alloc();
        if (nullptr == p_Packet)
        {
            CrashDump::Crash();
            return Packet_Error;
        }

        p_Packet->SetHeader((char *)&Header);
        if (Header.Len != p_Session->RecvQ.Get((char *)p_Packet->GetWriteBufferPtr(), Header.Len))
        {
            p_Packet->Free();
            return Packet_Error;
        }
        if (false == p_Packet->MoveWritePos(Header.Len))
        {
            p_Packet->Free();
            return Packet_Error;
        }

        //if (false == p_Packet->Decode(&Header))
        if (false == p_Packet->Decode())
        {
            //CrashDump::Crash();
            SYSLOG(L"NetServer", LOG_ERROR, L"Packet Decode # [SessionID: %lld]", p_Session->SessionID);
            p_Packet->Free();
            return Packet_Error;
        }

        InterlockedIncrement(&_Monitor_RecvPacketCounter);

        // packet proc
        try
        {
            p_Packet->addRef();
            OnRecv(p_Session->SessionID, p_Packet);
        }
        catch (Packet::Exception_PacketOut err)     // 여기서는 딱 내것만 캐치하도록 할 것.
        {
            // 로그 찍어준다.
            SYSLOG(L"NetServer", LOG_ERROR, L"OnRecv # [SessionID: %lld]", p_Session->SessionID);
            p_Packet->Free();
            return Packet_Error;
        }
        p_Packet->Free();
        return Packet_Complete;
    }
    int CNetServerEx3::CompleteRecvPacket(st_SESSION *p_Session)
    {
        int RecvQSize;
        st_PACKET_HEADER Header;
        Packet *p_Packet;

        //int CompletePacketResult;
        while (1)
        {
            // header size check
            RecvQSize = p_Session->RecvQ.GetUseSize();
            if (RecvQSize < sizeof(st_PACKET_HEADER))
            {
                //return Packet_NotComplete;
                break; 
            }

            // payload size check
            p_Session->RecvQ.Peek((char *)&Header, sizeof(st_PACKET_HEADER));
            if (Header.Code != Packet::_PacketCode)
                return Packet_Error;
            if (Header.Len > Packet::BUFFER_SIZE_DEFAULT - Packet::HEADER_SIZE_MAX)
                return Packet_Error;

            if (Header.Len + sizeof(st_PACKET_HEADER) > RecvQSize)
            {
                //return Packet_NotComplete;
                break;
            }
            if (false == p_Session->RecvQ.RemoveData(sizeof(st_PACKET_HEADER)))
                return Packet_Error;

            // get payload
            p_Packet = Packet::Alloc();
            if (nullptr == p_Packet)
            {
                CrashDump::Crash();
                return Packet_Error;
            }

            p_Packet->SetHeader((char *)&Header);
            if (Header.Len != p_Session->RecvQ.Get((char *)p_Packet->GetWriteBufferPtr(), Header.Len))
            {
                p_Packet->Free();
                return Packet_Error;
            }
            if (false == p_Packet->MoveWritePos(Header.Len))
            {
                p_Packet->Free();
                return Packet_Error;
            }

            //if (false == p_Packet->Decode(&Header))
            if (false == p_Packet->Decode())
            {
                //CrashDump::Crash();
                SYSLOG(L"NetServer", LOG_ERROR, L"Packet Decode # [SessionID: %lld]", p_Session->SessionID);
                p_Packet->Free();
                return Packet_Error;
            }

            InterlockedIncrement(&_Monitor_RecvPacketCounter);

            // packet proc
            try
            {
                OnRecv(p_Session->SessionID, p_Packet);
            }
            catch (Packet::Exception_PacketOut err)     // 여기서는 딱 내것만 캐치하도록 할 것.
            {
                // 로그 찍어준다.
                SYSLOG(L"NetServer", LOG_ERROR, L"OnRecv # [SessionID: %lld]", p_Session->SessionID);
                p_Packet->Free();
                return Packet_Error;
            }

            p_Packet->Free();
        }
        return Packet_Complete;
    }
    bool CNetServerEx3::RecvPost(st_SESSION *p_Session)
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
        Ret = WSARecv(p_Session->p_NetworkSocket->_Sock, (LPWSABUF)&RecvWsaBuf, BufCount, NULL, &flags, &p_Session->RecvOL, NULL);
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            if (Ret != ERROR_IO_PENDING)
            {
                // 이 때만 로그를 남긴다.
                if (Ret != WSAECONNABORTED && Ret != WSAECONNRESET && Ret != WSAESHUTDOWN)
                {
                    SYSLOG(L"SYSTEM", LOG_SYSTEM, L"WSARecv # [WSA Recv Error][WSAGetLastError:%d]", Ret);
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
    bool CNetServerEx3::SendPostIOCP(__int64 SessionID)
    {
        st_SESSION *p_Session;

        p_Session = FindSession(SessionID);
        if (nullptr == p_Session)
        {
            CrashDump::Crash();
            return false;
        }

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

                SendWsaBuf[Cnt].buf = p_Packet->GetHeaderBufferPtr();
                SendWsaBuf[Cnt].len = p_Packet->GetPacketSize();
                BufCount++;
                BufSize += p_Packet->GetPacketSize();
            }
        }

        if (0 == BufCount)
        {
            InterlockedExchange64(&p_Session->SendIO, 0);
            return false;
        }

        p_Session->SendPacketCnt = BufCount;
        p_Session->SendPacketSize = BufSize;

        memset(&p_Session->SendOL, 0, sizeof(OVERLAPPED));
        InterlockedIncrement64(&p_Session->p_IOCompare->IOCount);
        Ret = WSASend(p_Session->p_NetworkSocket->_Sock, (LPWSABUF)&SendWsaBuf, BufCount, NULL, 0, &p_Session->SendOL, NULL);
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            if (Ret != ERROR_IO_PENDING)
            {
                // 이 때만 로그를 남긴다.
                if (Ret != WSAECONNABORTED && Ret != WSAECONNRESET && Ret != WSAESHUTDOWN)
                {
                    SYSLOG(L"SYSTEM", LOG_SYSTEM, L"WSASend # [WSA Send Error][WSAGetLastError:%d]", Ret);
                    //SYSLOG(L"WSASend", LOG_SYSTEM, L"[%d]%s", Ret, L"WSA Send Error");
                }

                // 이미 끊긴 애 다시 디스커넥트(소켓 연결만 끊고 메모리는 그대로) 할 것임. -> 아름다운 종료 그딴건 없다.
                ClientShutdown(p_Session);

                // send io 0으로 초기화
                InterlockedExchange64(&p_Session->SendIO, 0);

                if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
                {
                    ClientRelease(p_Session);
                }
                return false;
            }
        }

        if (true == p_Session->SendDisconnectFlag)
        {
            ClientShutdown(p_Session);
        }

        return true;
    }
    void CNetServerEx3::CompleteRecv(st_SESSION *p_Session, DWORD cbTransferred)
    {
        // 문제가 있는 상황. 죽어랏!!
        if (0 == p_Session->RecvQ.MoveWritePtr(cbTransferred))
            CrashDump::Crash();

        // 패킷 처리
        int Ret = CompleteRecvPacket(p_Session);
        if (Ret != Packet_Complete)
        {
            if (Packet_Error == Ret)
            {
                ClientDisconnect(p_Session);        // 얘는 리시브도 막는다.
                return;
            }
            else
            {
                CrashDump::Crash();
            }
        }
        RecvPost(p_Session);
    }
    void CNetServerEx3::CompleteSend(st_SESSION *p_Session, DWORD cbTransferred)
    {
        if (cbTransferred != p_Session->SendPacketSize)
        {
            // 이 경우는 끊어야 하지만 일단 죽여보자.
            CrashDump::Crash();
            ClientShutdown(p_Session);
            return;
        }

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
            p_Packet->Free();
        }
        InterlockedAdd(&_Monitor_SendPacketCounter, p_Session->SendPacketCnt);
        OnSend(p_Session->SessionID, cbTransferred);
        InterlockedExchange64(&p_Session->SendIO, 0);
    }
    void CNetServerEx3::ClientShutdown(st_SESSION *p_Session)              // shutdown으로 안전한 종료 유도
    {
        shutdown(p_Session->p_NetworkSocket->_Sock, SD_SEND);
    }
    void CNetServerEx3::ClientDisconnect(st_SESSION *p_Session)            // socket 접속 강제 끊기
    {
        shutdown(p_Session->p_NetworkSocket->_Sock, SD_BOTH);              // shutdown both
    }
    void CNetServerEx3::SocketClose(SOCKET SessionSock)                    // closesocket(내부용)
    {
        //linger Ling;
        //Ling.l_onoff = 1;
        //Ling.l_linger = 0;
        //setsockopt(SessionSock, SOL_SOCKET, SO_LINGER, (char *)&Ling, sizeof(LINGER));
        closesocket(SessionSock);
        SessionSock = INVALID_SOCKET;
    }
    void CNetServerEx3::SocketReturn(st_NETWORK_SOCKET *p_NetworkSocket)
    {
        //p_NetworkSocket->Type = OVERLAPPED_TYPE_ACCEPT;
        //p_NetworkSocket->_p_Data = &_ListenSock;
        //if (false == _SocketPool->FreeSocket(p_NetworkSocket))
        //    CrashDump::Crash();

        BOOL ExRet;
        int WSALastError;

        p_NetworkSocket->Type = OVERLAPPED_TYPE_DISCONNECT;
        p_NetworkSocket->_p_Data = nullptr;
        ExRet = _lpfnDisconnectEx(p_NetworkSocket->_Sock, p_NetworkSocket, TF_REUSE_SOCKET, 0);
        if (FALSE == ExRet)
        {
            WSALastError = WSAGetLastError();
            if (WSALastError != ERROR_IO_PENDING)
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Disconnect # [Error][WSAGetLastError:%d]", WSALastError);
                CrashDump::Crash();
            }
        }

        //SOCKET *p_ListenSock;
        //
        //closesocket(p_NetworkSocket->_Sock);
        //p_NetworkSocket->_Sock = INVALID_SOCKET;
        //
        //p_ListenSock = (SOCKET *)p_NetworkSocket->_p_Data;
        //
        //p_NetworkSocket->Type = OVERLAPPED_TYPE_ACCEPT;
        //p_NetworkSocket->_p_Data = nullptr;
        //SetAcceptEx(p_ListenSock, p_NetworkSocket);
    }

    //void CNetServerEx3::ClientRelease(st_SESSION *p_Session)               // client session 해제
    //{
    //    st_IO_COMPARE ReleaseCompare;
    //    ReleaseCompare.IOCount = 0;
    //    ReleaseCompare.ReleaseFlag = 0;
    //
    //    if (InterlockedCompareExchange128((LONG64 *)p_Session->p_IOCompare, 1, 0, (LONG64 *)&ReleaseCompare) != 1)
    //        return;
    //
    //    // 최대한 빨리 소켓을 반환한다.
    //    SocketClose(p_Session->p_NetworkSocket->_Sock);
    //
    //    // ClientLeave를 여기서 호출한다.
    //    OnClientLeave(p_Session->SessionID);
    //
    //    Packet *p_Packet;
    //    int ReleaseSessionIndex;
    //    st_NETWORK_SOCKET *p_ReleaseNetworkSocket;
    //
    //    // Send Queue Clear
    //    while (1)
    //    {
    //        if (false == p_Session->SendQ.Dequeue(&p_Packet))
    //            break;
    //        p_Packet->Free();
    //    }
    //
    //    // Recv Queue Clear
    //    p_Session->RecvQ.Clear();
    //
    //    ReleaseSessionIndex = ClientID_Index(p_Session->SessionID);
    //    p_ReleaseNetworkSocket = p_Session->p_NetworkSocket;
    //
    //    p_Session->SendDisconnectFlag = false;
    //    p_Session->SessionID = SESSION_ID_DEFAULT;
    //    p_Session->p_NetworkSocket = nullptr;
    //
    //    InterlockedDecrement64(&_SessionCount);
    //    SetAcceptEx(&_ListenSock, p_ReleaseNetworkSocket);
    //    _SessionEmptyStack->Push(ReleaseSessionIndex);
    //}
    void CNetServerEx3::ClientRelease(st_SESSION *p_Session)
    {
        st_IO_COMPARE ReleaseCompare;
        ReleaseCompare.IOCount = 0;
        ReleaseCompare.ReleaseFlag = 0;

        if (InterlockedCompareExchange128((LONG64 *)p_Session->p_IOCompare, 1, 0, (LONG64 *)&ReleaseCompare) != 1)
            return;

        // ClientLeave를 여기서 호출한다.
        OnClientLeave(p_Session->SessionID);

        //p_Session->p_NetworkSocket->Type = OVERLAPPED_TYPE_DISCONNECT;
        //p_Session->p_NetworkSocket->_p_Data = p_Session;
        //
        //BOOL ExRet;
        //int WSALastError;
        //ExRet = _lpfnDisconnectEx(p_Session->p_NetworkSocket->_Sock, p_Session->p_NetworkSocket, TF_REUSE_SOCKET, 0);
        //if (FALSE == ExRet)
        //{
        //    WSALastError = WSAGetLastError();
        //    if (WSALastError != ERROR_IO_PENDING)
        //    {
        //        CrashDump::Crash();
        //    }
        //}

        SocketReturn(p_Session->p_NetworkSocket);
        p_Session->p_NetworkSocket = nullptr;

        Packet *p_Packet;
        int ReleaseSessionIndex;

        // Send Queue Clear
        while (1)
        {
            if (false == p_Session->SendQ.Dequeue(&p_Packet))
                break;
            p_Packet->Free();
        }

        // Recv Queue Clear
        p_Session->RecvQ.Clear();

        ReleaseSessionIndex = ClientID_Index(p_Session->SessionID);

        p_Session->SendDisconnectFlag = false;
        p_Session->SessionID = SESSION_ID_DEFAULT;
        //p_Session->p_NetworkSocket = nullptr;

        InterlockedDecrement64(&_SessionCount);
        //SetAcceptEx(&_ListenSock, p_ReleaseNetworkSocket);
        _SessionEmptyStack->Push(ReleaseSessionIndex);
    }
    //void CNetServerEx3::ClientReleaseEx(st_SESSION *p_Session)               // client session 해제
    //{
    //    Packet *p_Packet;
    //    int ReleaseSessionIndex;
    //    
    //    SocketReturn(p_Session->p_NetworkSocket);
    //    p_Session->p_NetworkSocket = nullptr;
    //
    //    // Send Queue Clear
    //    while (1)
    //    {
    //        if (false == p_Session->SendQ.Dequeue(&p_Packet))
    //            break;
    //        p_Packet->Free();
    //    }
    //
    //    // Recv Queue Clear
    //    p_Session->RecvQ.Clear();
    //
    //    ReleaseSessionIndex = ClientID_Index(p_Session->SessionID);
    //    
    //    p_Session->SendDisconnectFlag = false;
    //    p_Session->SessionID = SESSION_ID_DEFAULT;
    //    //p_Session->p_NetworkSocket = nullptr;
    //
    //    InterlockedDecrement64(&_SessionCount);
    //    //SetAcceptEx(&_ListenSock, p_ReleaseNetworkSocket);
    //    _SessionEmptyStack->Push(ReleaseSessionIndex);
    //}
    void CNetServerEx3::SocketRelease(st_NETWORK_SOCKET *p_NetworkSocket)
    {
        p_NetworkSocket->Type = OVERLAPPED_TYPE_ACCEPT;
        p_NetworkSocket->_p_Data = &_ListenSock;
        if (false == _SocketPool->FreeSocket(p_NetworkSocket))
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"SocketRelease # [Sock Pool Free Error]");
            CrashDump::Crash();
        }
    }

    unsigned __stdcall CNetServerEx3::MonitorTPS_Thread(LPVOID lpParam)
    {
        return ((CNetServerEx3 *)lpParam)->MonitorTPS_Thread_update();
    }
    unsigned CNetServerEx3::MonitorTPS_Thread_update(void)
    {
        //DWORD HeartbeatTick = _TimeManager->GetTickTime();
        ULONGLONG HeartbeatTick;
        ULONGLONG LoopTick;

        HeartbeatTick = GetTickCount64();
        while (1)
        {
            LoopTick = GetTickCount64();

            // 하트비트 체크
            //if (_WorkerthreadHeartbeatTick != 0 && HeartbeatTick + _WorkerthreadHeartbeatTick < _TimeManager->GetTickTime())
            if (_WorkerthreadHeartbeatTick != 0 && HeartbeatTick + _WorkerthreadHeartbeatTick < LoopTick)
            {
                PostQueuedCompletionStatus(_hIocp, 1, (ULONG_PTR)COMPLETION_KEY_HEARTBEAT, NULL);
                HeartbeatTick = LoopTick;
            }

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

    unsigned __stdcall CNetServerEx3::WorkerExThreadProc(LPVOID lpParam)
    {
        return ((CNetServerEx3 *)lpParam)->WorkerExThread_Update();
    }
    unsigned CNetServerEx3::WorkerExThread_Update(void)
    {
        // GQCS
        DWORD cbTransferred;
        //st_COMPLETION_KEY *p_CompletionKey;
        ULONG_PTR p_CompletionKey;
        //LPOVERLAPPED lpOverlapped;
        st_EXTEND_OVERLAPPED *lpOverlapped;
        BOOL GQCSRet;

        // Listen
        st_NETWORK_SOCKET *p_NetworkSocket;
        SOCKET *p_ListenSock;
        int SetSockOptRet;
        sockaddr_in *p_LocalAddr;
        sockaddr_in *p_RemoteAddr;
        int p_LocalAddrLen;
        int p_RemoteAddrLen;
        st_CLIENT_CONNECT_INFO ClientConnectInfo;
        LONG64 SessionIndex;
        //st_COMPLETION_KEY *p_ListenKey;
        HANDLE IOCPRet;

        // Error
        int WSAErrorCode;
        DWORD LastErrorCode;

        // Session
        st_SESSION *p_Session;
        
        // No Delay Setting
        BOOL NoDelayOptVal;

        // keep alive setting
        DWORD KeepAliveResult;
        tcp_keepalive tcpkl;

        // Linger setting
        linger Ling;

        // GetAcceptExSockaddrs
        GUID GuidGetAcceptExSockaddrs;
        LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs;
        DWORD dwBytes;
        int WSAIoctlRet;

        // No Delay Setting
        NoDelayOptVal = true;               // No Delay 켠다.

        // keep alive setting
        tcpkl.onoff = 1;
        tcpkl.keepalivetime = 30000;        // 30초마다 keepalive 신호를 보내겠다.(윈도우 기본은 2시간)
        tcpkl.keepaliveinterval = 2000;     // keepalive 신호를 보내고 응답이 없으면 2초마다 재전송. (ms tcp는 10회 재시도 한다.)

        // Linger setting
        Ling.l_onoff = 1;
        Ling.l_linger = 0;

        // GetAcceptExSockaddrs
        GuidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
        lpfnGetAcceptExSockaddrs = NULL;

        while (1)
        {
            cbTransferred = 0;
            p_CompletionKey = OVERLAPPED_TYPE_DEFAULT;
            lpOverlapped = nullptr;

            GQCSRet = GetQueuedCompletionStatus(_hIocp, &cbTransferred, (PULONG_PTR)&p_CompletionKey, (LPOVERLAPPED *)&lpOverlapped, INFINITE);

            //if (cbTransferred == 0 && lpOverlapped != nullptr)
            //{
            //    p_NetworkSocket = (st_NETWORK_SOCKET *)lpOverlapped;
            //    int i = 0;
            //}

            switch (p_CompletionKey)
            {
            case COMPLETION_KEY_EXIT:
                return 0;
                break;
            case COMPLETION_KEY_HEARTBEAT:
                OnWorkerThreadHeartBeat();
                break;
            //case COMPLETION_KEY_SEND_PACKET:
            //
            //    p_Session = (st_SESSION *)p_CompletionKey->_p_Data;
            //
            //    _CompletionKeyStack->Push(p_CompletionKey);
            //
            //    if (p_Session->SendQ.GetUseSize() > 0)
            //    {
            //        if (0 == InterlockedCompareExchange64(&p_Session->SendIO, 1, 0))        // 0이라면 1로 바꾼다.
            //            SendPostIOCP(p_Session->SessionID);
            //    }
            //
            //    if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
            //        ClientRelease(p_Session);
            //    break;
            case COMPLETION_KEY_OVERLAPPED:
                switch (lpOverlapped->Type)
                {
                case OVERLAPPED_TYPE_ACCEPT:
                    do
                    {
                        p_Session = nullptr;

                        // AcceptEx
                        p_NetworkSocket = (st_NETWORK_SOCKET *)lpOverlapped;
                        p_ListenSock = (SOCKET *)p_NetworkSocket->_p_Data;

                        // 1. Context Update
                        SetSockOptRet = setsockopt(p_NetworkSocket->_Sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)p_ListenSock, sizeof(SOCKET));
                        if (SetSockOptRet != 0)
                        {
                            // 접속이 끊어진 걸로 봐야할 듯.(10057, 10038)
                            WSAErrorCode = WSAGetLastError();
                            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Accept # [SO_UPDATE_ACCEPT_CONTEXT Failed with Error: %d]", WSAErrorCode);
                            //CrashDump::Crash();       // 여기서 크래시를 내면 시스템이 죽는다.
                            //SocketReturn(p_NetworkSocket);
                            SocketRelease(p_NetworkSocket);
                            break;
                        }

                        if (FALSE == GQCSRet)
                        {
                            LastErrorCode = GetLastError();
                            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Accept # [GQCS Failed with Error: %u]", LastErrorCode);
                            SocketReturn(p_NetworkSocket);
                            break;
                        }

                        //if ((DWORD)-1 == Ret || OptRet < 0)
                        //{
                        //    // 접속이 끊어지면 에러발생 가능.
                        //    int WSAError = WSAGetLastError();
                        //    //CrashDump::Crash();
                        //    continue;
                        //}

                        // 2. 새로운 소켓의 관리포인트 생성(걍 임의로 만든듯.)

                        // 3. GetAcceptExSockaddrs()로 클라 정보 얻어옴.
                        p_LocalAddr = nullptr;
                        p_RemoteAddr = nullptr;
                        p_LocalAddrLen = 0;
                        p_RemoteAddrLen = 0;

                        WSAIoctlRet = WSAIoctl(p_NetworkSocket->_Sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                            &GuidGetAcceptExSockaddrs, sizeof(GuidGetAcceptExSockaddrs),
                            &lpfnGetAcceptExSockaddrs, sizeof(lpfnGetAcceptExSockaddrs),
                            &dwBytes, nullptr, nullptr);
                        if (SOCKET_ERROR == WSAIoctlRet)
                        {
                            WSAErrorCode = WSAGetLastError();
                            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Accept # [WSAIoctl Failed with Error: %d]", WSAErrorCode);
                            CrashDump::Crash();
                            return 0;
                        }

                        lpfnGetAcceptExSockaddrs(p_NetworkSocket->_Buf, 0,
                            sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
                            (sockaddr **)&p_LocalAddr, &p_LocalAddrLen, (sockaddr **)&p_RemoteAddr, &p_RemoteAddrLen);

                        // 4. 연결된 소켓을 IOCP에 등록.


                        _Monitor_AcceptTotal++;
                        _Monitor_AcceptCounter++;

                        //wsprintf(p_AcceptOL->ClientConnectInfo._IP, L"%u.%u.%u.%u", p_RemoteAddr->sin_addr.s_net, p_RemoteAddr->sin_addr.s_host, p_RemoteAddr->sin_addr.s_lh, p_RemoteAddr->sin_addr.s_impno);
                        //p_AcceptOL->ClientConnectInfo._Port = p_RemoteAddr->sin_port;

                        if (false == _Nagle)
                        {
                            SetSockOptRet = setsockopt(p_NetworkSocket->_Sock, IPPROTO_TCP, TCP_NODELAY, (char *)&NoDelayOptVal, sizeof(BOOL));
                            if (SetSockOptRet != 0)
                            {
                                WSAErrorCode = WSAGetLastError();
                                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Accept # [TCP_NODELAY Failed with Error: %d]", WSAErrorCode);
                                CrashDump::Crash();
                                SocketReturn(p_NetworkSocket);
                                break;
                            }
                        }

                        // keep alive
                        SetSockOptRet = WSAIoctl(p_NetworkSocket->_Sock, SIO_KEEPALIVE_VALS, &tcpkl, sizeof(tcp_keepalive), 0, 0, &KeepAliveResult, NULL, NULL);
                        if (SetSockOptRet != 0)
                        {
                            WSAErrorCode = WSAGetLastError();
                            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Accept # [SIO_KEEPALIVE_VALS Failed with Error: %d]", WSAErrorCode);
                            CrashDump::Crash();
                            SocketReturn(p_NetworkSocket);
                            break;
                        }

                        // linger
                        SetSockOptRet = setsockopt(p_NetworkSocket->_Sock, SOL_SOCKET, SO_LINGER, (char *)&Ling, sizeof(LINGER));
                        if (SetSockOptRet != 0)
                        {
                            WSAErrorCode = WSAGetLastError();
                            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Accept # [SO_LINGER Failed with Error: %d]", WSAErrorCode);
                            CrashDump::Crash();
                            SocketReturn(p_NetworkSocket);
                            break;
                        }

                        //------------------------------------------------------
                        // 여기는 방법을 강구해 보자.
                        ClientConnectInfo._Socket = p_NetworkSocket->_Sock;
                        wsprintf(ClientConnectInfo._IP, L"%u.%u.%u.%u", p_RemoteAddr->sin_addr.s_net, p_RemoteAddr->sin_addr.s_host, p_RemoteAddr->sin_addr.s_lh, p_RemoteAddr->sin_addr.s_impno);
                        ClientConnectInfo._Port = p_RemoteAddr->sin_port;
                        if (false == OnConnectionRequest(&ClientConnectInfo))
                        {
                            SocketReturn(p_NetworkSocket);
                            break;
                        }
                        //------------------------------------------------------

                        SessionIndex = FindEmptySession();
                        if (-1 == SessionIndex)
                        {
                            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Accept # [Session Stack Full Error][SessionCnt:%d]", _SessionCount);
                            SocketReturn(p_NetworkSocket);
                            break;
                        }
                        p_Session = &_Session[SessionIndex];

                        //if (InterlockedIncrement64(&p_Session->p_IOCompare->IOCount) != 1)
                        //{
                        //    CrashDump::Crash();
                        //}
                        InterlockedIncrement64(&p_Session->p_IOCompare->IOCount);
                        InterlockedCompareExchange64(&p_Session->p_IOCompare->ReleaseFlag, 0, 1);

                        InterlockedIncrement64(&_SessionCount);

                        p_Session->SessionID = NewClientID(SessionIndex);
                        //p_Session->Sock = p_NetworkSocket->_Sock;
                        //p_Session->SessionAddr = *p_RemoteAddr;
                        p_Session->p_NetworkSocket = p_NetworkSocket;
                        p_Session->SendPacketCnt = 0;
                        p_Session->SendPacketSize = 0;
                        p_Session->SendIO = 0;

                        p_Session->p_NetworkSocket->Type = OVERLAPPED_TYPE_COMPLETE_NETWORK;
                        p_Session->p_NetworkSocket->_p_Data = p_Session;

                        if (false == p_NetworkSocket->_IOCPFlag)
                        {
                            IOCPRet = CreateIoCompletionPort((HANDLE)p_Session->p_NetworkSocket->_Sock, _hIocp, (ULONG_PTR)COMPLETION_KEY_OVERLAPPED, 0);
                            if (NULL == IOCPRet)
                            {
                                LastErrorCode = GetLastError();
                                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Accept # [Sock IOCP Error][ErrorCode:%u]", LastErrorCode);
                                CrashDump::Crash();
                                return -1;
                            }
                            p_NetworkSocket->_IOCPFlag = true;
                        }

                        OnClientJoin(p_Session->SessionID);    // Recv 거는 것보다 OnClientJoin이 먼저 들어가야함.
                        RecvPost(p_Session);

                        if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
                        {
                            ClientRelease(p_Session);
                            break;
                        }

                        st_NETWORK_SOCKET *p_NewNetworkSocket;
                        p_NewNetworkSocket = nullptr;
                        if (false == _SocketPool->AllocSocket(&p_NewNetworkSocket))
                        {
                            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Accept # [Sock Pool Alloc Error]");
                            CrashDump::Crash();
                        }
                        SetAcceptEx(&_ListenSock, p_NewNetworkSocket);

                    } while (0);
                    break;
                case OVERLAPPED_TYPE_DISCONNECT:
                    //p_Session = (st_SESSION *)lpOverlapped->_p_Data;
                    //ClientReleaseEx(p_Session);
                    p_NetworkSocket = (st_NETWORK_SOCKET *)lpOverlapped;
                    SocketRelease(p_NetworkSocket);
                    break;
                case OVERLAPPED_TYPE_COMPLETE_NETWORK:
                    p_Session = (st_SESSION *)lpOverlapped->_p_Data;

                    OnWorkerThreadBegin(p_Session->SessionID);

                    if (FALSE == GQCSRet)
                    {
                        LastErrorCode = GetLastError();
                        if (LastErrorCode != ERROR_NETNAME_DELETED && LastErrorCode != ERROR_SEM_TIMEOUT)
                            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"IOCP Complete # [GQCS Failed with Error: %u]", LastErrorCode);
                        //SYSLOG(L"GQCS", LOG_ERROR, L"[%u]%s", LastErrorCode, L"GQCS Error");
                        ClientDisconnect(p_Session);
                    }
                    else if (0 == cbTransferred)
                    {
                        //ClientShutdown(p_Session);
                        ClientDisconnect(p_Session);
                    }
                    else
                    {
                        if (&p_Session->RecvOL == lpOverlapped)
                            CompleteRecv(p_Session, cbTransferred);
                        else if (&p_Session->SendOL == lpOverlapped)
                            CompleteSend(p_Session, cbTransferred);
                        else
                        {
                            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"IOCP Complete # [Invalid Overlappped]");
                            CrashDump::Crash();
                            //SYSLOG(L"GQCS", LOG_ERROR, L"[%d]%s", 0, L"Invalid Overlappped");
                            //ClientShutdown(p_Session);
                            ClientDisconnect(p_Session);
                        }
                    }

                    if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
                        ClientRelease(p_Session);
                    OnWorkerThreadEnd(p_Session->SessionID);

                    break;
                default:
                    CrashDump::Crash();
                    break;
                }
                break;
            default:
                CrashDump::Crash();
                break;
            }
            
        }

        return 0;
    }
    unsigned __stdcall CNetServerEx3::SendThreadProc(LPVOID lpParam)
    {
        return ((CNetServerEx3 *)lpParam)->SendThread_Update();
    }
    unsigned CNetServerEx3::SendThread_Update(void)
    {
        int SessionIndex;
        st_SESSION *p_Session;
        __int64 SessionID;
        while (1)
        {
            for (SessionIndex = 0; SessionIndex < _SessionMax; ++SessionIndex)
            {
                p_Session = &_Session[SessionIndex];
                SessionID = p_Session->SessionID;

                if (SESSION_ID_DEFAULT == SessionID)
                    continue;

                // debug
                if (SessionID < 1)
                    CrashDump::Crash();

                if (InterlockedIncrement64(&p_Session->p_IOCompare->IOCount) == 1)
                {
                    if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
                        ClientRelease(p_Session);
                    continue;
                }

                if (1 == p_Session->p_IOCompare->ReleaseFlag)
                {
                    if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
                        ClientRelease(p_Session);
                    continue;
                }

                if (p_Session->SessionID != SessionID)
                {
                    if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
                        ClientRelease(p_Session);
                    continue;
                }

                if (0 == InterlockedCompareExchange64(&p_Session->SendIO, 1, 0))        // 0이라면 1로 바꾼다.
                    SendPostIOCP(p_Session->SessionID);

                //if (p_Session->SendQ.GetUseSize() > 0)
                //{
                //    if (0 == InterlockedCompareExchange64(&p_Session->SendIO, 1, 0))        // 0이라면 1로 바꾼다.
                //        SendPostIOCP(p_Session->SessionID);
                //}

                if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
                    ClientRelease(p_Session);

                //st_COMPLETION_KEY *p_Key;
                //if (false == _CompletionKeyStack->Pop(&p_Key))
                //    CrashDump::Crash();
                //p_Key->_Type = COMPLETION_KEY_SEND_PACKET;
                //p_Key->_p_Data = p_Session;
                //
                //PostQueuedCompletionStatus(_hIocp, 1, (ULONG_PTR)p_Key, (LPOVERLAPPED)NULL);

                //PostQueuedCompletionStatus(_hIocp, TYPE_SEND_PACKET, (ULONG_PTR)p_Session, (LPOVERLAPPED)NULL);
                //if (p_Session->SendQ.GetUseSize() > 0)
                //    PostQueuedCompletionStatus(_hIocp, TYPE_SEND_PACKET, (ULONG_PTR)p_Session, (LPOVERLAPPED)NULL);
            }
            Sleep(1);
        }
        return 0;
    }
}