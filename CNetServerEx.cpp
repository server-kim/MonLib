#include "LibHeader.h"

// SessionMax에 대한 최대값 enum으로 정의하고 생성자에서 체크할 것.
// 리팩토링 까지는 아니더라도 깔끔히 정리할 필요가 있다.

namespace MonLib
{
    CNetServerEx::CNetServerEx(int SessionMax, bool Nagle)
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
        //TimeManager TimeMgr = TimeManager::GetInstance();
        //_TimeManager = &TimeMgr;

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

        _AcceptThread = INVALID_HANDLE_VALUE;
        _AcceptThreadId = 0;

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
        _AcceptOL = nullptr;

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

        // 디버깅용 센드큐 카운터
        //_SendQueueCount = 0;
    }
    CNetServerEx::~CNetServerEx(void)
    {
        stop();
        WSACleanup();
    }

    bool CNetServerEx::start(WCHAR *p_IP, USHORT Port, int WorkerThreadCnt, BYTE PacketCode, BYTE XORCode1, BYTE XORCode2, bool SendThread)
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
        memcpy_s(_IP, IP_V4_MAX_LEN + 1, p_IP, IP_V4_MAX_LEN);
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

        // AcceptEx
        _AcceptOL = new st_ACCEPT_OVERLAPPED[_SessionMax];
        //_AcceptOL = new st_ACCEPT_OVERLAPPED[50];

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

        // AcceptEx Init
        if (false == AcceptExInit())
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"AcceptEx Init Error");
            return false;
        }

        // Accept Thread 생성
        //_AcceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThreadProc, this, 0, &_AcceptThreadId);
        //if (INVALID_HANDLE_VALUE == _AcceptThread)
        //{
        //    SYSLOG(L"SYSTEM", LOG_SYSTEM, L"AcceptThread Creation Error");
        //    return false;
        //}

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

        return true;
    }
    bool CNetServerEx::NetworkInit(void)
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
        hResult = CreateIoCompletionPort((HANDLE)_ListenSock, _hIocp, (ULONG_PTR)&_ListenSock, 0);
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
    bool CNetServerEx::AcceptExInit(void)
    {
        GUID GuidAcceptEx;
        //LPFN_ACCEPTEX lpfnAcceptEx;
        DWORD dwBytes;
        int Ret;
        int Cnt;
        
        GuidAcceptEx = WSAID_ACCEPTEX;
        _lpfnAcceptEx = NULL;
        
        Ret = WSAIoctl(_ListenSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
            &GuidAcceptEx, sizeof(GuidAcceptEx),
            &_lpfnAcceptEx, sizeof(_lpfnAcceptEx),
            &dwBytes, nullptr, nullptr);
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"WSAIoctl Failed with Error: %d", Ret);
            closesocket(_ListenSock);
            return false;
        }

        for (Cnt = 0; Cnt < _SessionMax; ++Cnt)
        //for (Cnt = 0; Cnt < 50; ++Cnt)
        {
            if (false == SetAcceptEx(&_ListenSock, &_AcceptOL[Cnt]))
            {
                closesocket(_ListenSock);
                return false;
            }
        }

        return true;

        // 돌아가는 버전
        //GUID GuidAcceptEx;
        //LPFN_ACCEPTEX lpfnAcceptEx;
        //DWORD dwBytes;
        //int Ret;
        //int Cnt;
        ////WSAOVERLAPPED olOverlap;
        //BOOL bRet = FALSE;
        //HANDLE hResult;
        //
        //GuidAcceptEx = WSAID_ACCEPTEX;
        //lpfnAcceptEx = NULL;
        //bRet = FALSE;
        //
        //Ret = WSAIoctl(_ListenSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
        //    &GuidAcceptEx, sizeof(GuidAcceptEx),
        //    &lpfnAcceptEx, sizeof(lpfnAcceptEx),
        //    &dwBytes, nullptr, nullptr);
        //if (SOCKET_ERROR == Ret)
        //{
        //    Ret = WSAGetLastError();
        //    SYSLOG(L"SYSTEM", LOG_SYSTEM, L"WSAIoctl Failed with Error: %d", Ret);
        //    closesocket(_ListenSock);
        //    return false;
        //}
        //
        //// 중간에 실패하면 그동안 만들어놓은 것도 해제해야 할듯.
        //for (Cnt = 0; Cnt < _SessionMax; ++Cnt)
        //{
        //    _p_AcceptBuf[Cnt]._p_ListenSock = &_ListenSock;
        //    _p_AcceptBuf[Cnt]._Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        //    if (INVALID_SOCKET == _p_AcceptBuf[Cnt]._Sock)
        //    {
        //        Ret = WSAGetLastError();
        //        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Create accept socket Failed with Error: %d", Ret);
        //        closesocket(_ListenSock);
        //        return false;
        //    }
        //
        //    memset(&_p_AcceptBuf[Cnt]._AcceptOL, 0, sizeof(_p_AcceptBuf[Cnt]._AcceptOL));
        //    _p_AcceptBuf[Cnt]._AcceptOL._p_AcceptBuf = &_p_AcceptBuf[Cnt];
        //    bRet = lpfnAcceptEx(_ListenSock, _p_AcceptBuf[Cnt]._Sock, _p_AcceptBuf[Cnt]._Buf,
        //        0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
        //        &dwBytes, &_p_AcceptBuf[Cnt]._AcceptOL);
        //    if (FALSE == bRet)
        //    {
        //        Ret = WSAGetLastError();
        //        if (Ret != ERROR_IO_PENDING)
        //        {
        //            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"AcceptEx Failed with Error: %d", Ret);
        //            closesocket(_p_AcceptBuf[Cnt]._Sock);
        //            closesocket(_ListenSock);
        //            return false;
        //        }
        //    }
        //
        //    //hResult = CreateIoCompletionPort((HANDLE)_p_AcceptBuf[Cnt]._Sock, _hIocpAccept, (ULONG_PTR)&_p_AcceptBuf[Cnt], 0);
        //    //if (NULL == hResult)
        //    //{
        //    //    SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Accept Socket IOCP Error: %u", GetLastError());
        //    //    closesocket(_p_AcceptBuf[Cnt]._Sock);
        //    //    closesocket(_ListenSock);
        //    //    return false;
        //    //}
        //}
        //
        //return true;

        // 연습용 수도코드
        //int Ret;
        //LPFN_ACCEPTEX lpfnAcceptEx = NULL;
        //GUID GuidAcceptEx = WSAID_ACCEPTEX;
        //WSAOVERLAPPED olOverlap;
        //DWORD dwBytes;
        ////SOCKET AcceptSock = INVALID_SOCKET;
        ////char lpOutputBuf[1024];
        ////int outBufLen = 1024;
        //st_ACCEPT_BUF AcceptStruct;
        //BOOL bRet = FALSE;
        //Ret = WSAIoctl(_ListenSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
        //    &GuidAcceptEx, sizeof(GuidAcceptEx),
        //    &lpfnAcceptEx, sizeof(lpfnAcceptEx),
        //    &dwBytes, nullptr, nullptr);
        //if (SOCKET_ERROR == Ret)
        //{
        //    Ret = WSAGetLastError();
        //    SYSLOG(L"SYSTEM", LOG_SYSTEM, L"WSAIoctl Failed with Error: %d", Ret);
        //    closesocket(_ListenSock);
        //    return false;
        //}
        //
        ////AcceptSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        //AcceptStruct._Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ////if (INVALID_SOCKET == AcceptSock)
        //if (INVALID_SOCKET == AcceptStruct._Sock)
        //{
        //    Ret = WSAGetLastError();
        //    SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Create accept socket Failed with Error: %d", Ret);
        //    closesocket(_ListenSock);
        //    return false;
        //}
        //
        //memset(&olOverlap, 0, sizeof(olOverlap));
        //
        ////bRet = lpfnAcceptEx(_ListenSock, AcceptSock, lpOutputBuf,
        ////    outBufLen - ((sizeof(sockaddr_in) + 16) * 2),
        ////    sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
        ////    &dwBytes, &olOverlap);
        //bRet = lpfnAcceptEx(_ListenSock, AcceptStruct._Sock, AcceptStruct.Buf,
        //    0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
        //    &dwBytes, &olOverlap);
        //if (FALSE == bRet)
        //{
        //    Ret = WSAGetLastError();
        //    if (Ret != ERROR_IO_PENDING)
        //    {
        //        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"AcceptEx Failed with Error: %d", Ret);
        //        //closesocket(AcceptSock);
        //        closesocket(AcceptStruct._Sock);
        //        closesocket(_ListenSock);
        //        return false;
        //    }
        //}
        //
        ////CreateIoCompletionPort((HANDLE)AcceptSock, _hIocp, NULL, NULL);
        //CreateIoCompletionPort((HANDLE)AcceptStruct._Sock, _hIocp, NULL, NULL);
        //
        //return true;
    }
    bool CNetServerEx::SetAcceptEx(SOCKET *ListenSock, st_ACCEPT_OVERLAPPED *p_AcceptOL)
    {
        int Ret;
        BOOL bRet;
        DWORD dwBytes;

        memset(p_AcceptOL, 0, sizeof(OVERLAPPED));

        p_AcceptOL->_p_ListenSock = &_ListenSock;
        p_AcceptOL->_Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (INVALID_SOCKET == p_AcceptOL->_Sock)
        {
            Ret = WSAGetLastError();
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Create accept socket Failed with Error: %d", Ret);
            //closesocket(ListenSock);
            return false;
        }

        bRet = _lpfnAcceptEx(_ListenSock, p_AcceptOL->_Sock, p_AcceptOL->_Buf,
            0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
            &dwBytes, (LPOVERLAPPED)p_AcceptOL);
        if (FALSE == bRet)
        {
            Ret = WSAGetLastError();
            if (Ret != ERROR_IO_PENDING)
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"AcceptEx Failed with Error: %d", Ret);
                closesocket(p_AcceptOL->_Sock);
                //closesocket(ListenSock);
                return false;
            }
        }

        // p_AcceptOL->_Sock에 iocp 거는것은 accept 받은 이후로 한다.

        return true;
    }
    void CNetServerEx::stop(void)
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
            PostQueuedCompletionStatus(_hIocp, TYPE_EXIT, NULL, NULL);
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

        // AcceptEx
        delete[] _AcceptOL;

        _ServerOn = false;
    }
    LONG64 CNetServerEx::GetSessionCount(void)
    {
        return _SessionCount;
    }
    bool CNetServerEx::SendPacket(__int64 SessionID, Packet *p_Packet, bool Disconnect)
    {
        bool Ret;
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
                //ClientReleaseDebug(p_Session, 5);
                ClientRelease(p_Session);
            return false;
        }

        if (p_Session->SessionID != SessionID)
        {
            if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
                //ClientReleaseDebug(p_Session, 6);
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
        //InterlockedIncrement(&p_Session->SendQueueCnt);

        // disconnect flag를 올려준다.
        if (true == Disconnect)
        {
            p_Session->SendDisconnectFlag = true;
        }

        Ret = SendPost(p_Session->SessionID);

        if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
        {
            //ClientReleaseDebug(p_Session, 7);
            ClientRelease(p_Session);
            return false;
        }

        return Ret;
    }
    //bool CNetServerEx::SendPacketRequest(__int64 SessionID, Packet *p_Packet, bool Disconnect)
    //{
    //    bool Ret;
    //    st_SESSION *p_Session;
    //
    //    p_Session = FindSession(SessionID);
    //    if (nullptr == p_Session)
    //    {
    //        // Session Index가 잘못되었으므로 정지한다.
    //        CrashDump::Crash();
    //        return false;
    //    }
    //
    //    if (InterlockedIncrement64(&p_Session->p_IOCompare->IOCount) == 1)
    //    {
    //        if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
    //            //ClientReleaseDebug(p_Session, 5);
    //            ClientRelease(p_Session);
    //        return false;
    //    }
    //
    //    if (p_Session->SessionID != SessionID)
    //    {
    //        if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
    //            //ClientReleaseDebug(p_Session, 6);
    //            ClientRelease(p_Session);
    //        return false;
    //    }
    //
    //    // 암호화
    //    p_Packet->Encode();
    //
    //    // 패킷을 인큐한다.
    //    p_Packet->addRef();
    //    if (false == p_Session->SendQ.Enqueue(p_Packet))
    //    {
    //        CrashDump::Crash();
    //        return false;
    //    }
    //    //InterlockedIncrement(&p_Session->SendQueueCnt);
    //
    //    // disconnect flag를 올려준다.
    //    if (true == Disconnect)
    //    {
    //        p_Session->SendDisconnectFlag = true;
    //    }
    //
    //    // 테스트용 버전. 워커스레드에 SendPost 호출을 요청한다.
    //    if (0 == InterlockedCompareExchange64(&p_Session->SendIO, 1, 0))        // 0이라면 1로 바꾼다.
    //        PostQueuedCompletionStatus(_hIocp, 1, (ULONG_PTR)p_Session, (LPOVERLAPPED)NULL);
    //    else
    //    {
    //        if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
    //            ClientRelease(p_Session);
    //    }
    //    Ret = true;
    //
    //    return Ret;
    //}
    bool CNetServerEx::SendPacketIOCP(__int64 SessionID, Packet *p_Packet, bool Disconnect)
    {
        //bool Ret;
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
                //ClientReleaseDebug(p_Session, 5);
                ClientRelease(p_Session);
            return false;
        }

        if (p_Session->SessionID != SessionID)
        {
            if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
                //ClientReleaseDebug(p_Session, 6);
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
        //InterlockedIncrement(&p_Session->SendQueueCnt);

        if (true == Disconnect)
            p_Session->SendDisconnectFlag = true;

        PostQueuedCompletionStatus(_hIocp, TYPE_SEND_PACKET, (ULONG_PTR)p_Session, (LPOVERLAPPED)NULL);

        return true;
    }
    bool CNetServerEx::SendPacketSendThread(__int64 SessionID, Packet *p_Packet, bool Disconnect)
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
        //InterlockedIncrement(&p_Session->SendQueueCnt);

        if (true == Disconnect)
            p_Session->SendDisconnectFlag = true;

        if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
        {
            ClientRelease(p_Session);
            return false;
        }

        return true;
    }
    bool CNetServerEx::GetServerOn(void)
    {
        return _ServerOn;
    }
    int CNetServerEx::GetEmptyStackUseSize(void)
    {
        if (_SessionEmptyStack != nullptr)
            return _SessionEmptyStack->GetUseSize();
        else
            return 0;
    }

    bool CNetServerEx::Disconnect(__int64 ClientID)
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
                //ClientReleaseDebug(p_Session, 21);
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
                //ClientReleaseDebug(p_Session, 22);
                ClientRelease(p_Session);
            return false;
        }

        //ClientShutdown(p_Session);
        ClientDisconnect(p_Session);

        if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
        {
            //ClientReleaseDebug(p_Session, 23);
            ClientRelease(p_Session);
            return false;
        }
        return true;
    }

    unsigned __int64 CNetServerEx::FindEmptySession(void)
    {
        // lockfree stack ver.
        unsigned __int64 EmptySessionIndex = -100;
        if (false == _SessionEmptyStack->Pop(&EmptySessionIndex))
            return -1;
        return EmptySessionIndex;
    }
    CNetServerEx::st_SESSION *CNetServerEx::FindSession(__int64 SessionID)
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
    int CNetServerEx::CompletePacket(st_SESSION *p_Session)
    {
        //if (true == DebugFlag)
        //{
        //    ProfileBegin(L"CompletePacket");
        //    ProfileBegin(L"CompletePacket 1");
        //}

        Packet *p_Packet;
        int RecvQSize;
        st_PACKET_HEADER Header;

        // header size check
        RecvQSize = p_Session->RecvQ.GetUseSize();
        if (RecvQSize < sizeof(st_PACKET_HEADER))
        {
            //if (true == DebugFlag)
            //{
            //    ProfileEnd(L"CompletePacket 1");
            //    ProfileEnd(L"CompletePacket");
            //}
            return Packet_NotComplete;
        }

        // payload size check
        p_Session->RecvQ.Peek((char *)&Header, sizeof(st_PACKET_HEADER));
        if (Header.Code != Packet::_PacketCode)
            return Packet_Error;
        if (Header.Len > Packet::BUFFER_SIZE_DEFAULT - Packet::HEADER_SIZE_MAX)
            return Packet_Error;

        if (Header.Len + sizeof(st_PACKET_HEADER) > RecvQSize)
        {
            //if (true == DebugFlag)
            //{
            //    ProfileEnd(L"CompletePacket 1");
            //    ProfileEnd(L"CompletePacket");
            //}
            return Packet_NotComplete;
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

        //p_Packet->SetHeader_CustomHeader((char *)&Header, sizeof(st_NetHeader));
        //p_Packet->SetHeader_CustomHeader_Short(Header.PayloadSize);
        p_Packet->SetHeader((char *)&Header);
        if (Header.Len != p_Session->RecvQ.Get((char *)p_Packet->GetWriteBufferPtr(), Header.Len))
        {
            p_Packet->Free();
            //if (true == DebugFlag)
            //{
            //    ProfileEnd(L"CompletePacket 1");
            //    ProfileEnd(L"CompletePacket");
            //}
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
            SYSLOG(L"NetServer", LOG_ERROR, L"Packet Decode [SessionID: %lld]", p_Session->SessionID);
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
            p_Packet->addRef();
            OnRecv(p_Session->SessionID, p_Packet);
        }
        catch (Packet::Exception_PacketOut err)     // 여기서는 딱 내것만 캐치하도록 할 것.
        {
            // 로그 찍어준다.
            SYSLOG(L"NetServer", LOG_ERROR, L"OnRecv [SessionID: %lld]", p_Session->SessionID);
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
    int CNetServerEx::CompleteRecvPacket(st_SESSION *p_Session)
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
                SYSLOG(L"NetServer", LOG_ERROR, L"Packet Decode [SessionID: %lld]", p_Session->SessionID);
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
                SYSLOG(L"NetServer", LOG_ERROR, L"OnRecv [SessionID: %lld]", p_Session->SessionID);
                p_Packet->Free();
                return Packet_Error;
            }

            p_Packet->Free();
        }
        return Packet_Complete;
    }
    bool CNetServerEx::RecvPost(st_SESSION *p_Session)
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
                    //ClientReleaseDebug(p_Session, 1);
                    ClientRelease(p_Session);
                }
                return false;
            }
        }

        return true;
    }
    //bool CNetServerEx::SendPost(st_Session *p_Session)
    bool CNetServerEx::SendPost(__int64 SessionID)
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

                SendWsaBuf[Cnt].buf = p_Packet->GetHeaderBufferPtr();
                SendWsaBuf[Cnt].len = p_Packet->GetPacketSize();
                BufCount++;
                BufSize += p_Packet->GetPacketSize();
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
                    //ClientReleaseDebug(p_Session, 2);
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

        if (true == p_Session->SendDisconnectFlag)
        {
            ClientShutdown(p_Session);
        }

        //if (true == DebugFlag)
        //{
        //    ProfileEnd(L"SendPost 2");
        //    ProfileEnd(L"SendPost");
        //}

        return true;
    }
    bool CNetServerEx::SendPostIOCP(__int64 SessionID)
    {
        st_SESSION *p_Session;

        p_Session = FindSession(SessionID);
        if (nullptr == p_Session)
        {
            CrashDump::Crash();
            return false;
        }

        //if (InterlockedCompareExchange64(&p_Session->SendIO, 0, 1) != 1)
        //    CrashDump::Crash();

    //RETRY:
        //if (InterlockedCompareExchange64(&p_Session->SendIO, 1, 0) != 0)        // 0이라면 1로 바꾼다.
        //    return false;

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
        //if (true == DebugFlag)
        //    ProfileEnd(L"SendPost 1");

        if (0 == BufCount)
        {
            InterlockedExchange64(&p_Session->SendIO, 0);

            //if (true == DebugFlag)
            //    ProfileEnd(L"SendPost");

            //if (p_Session->SendQ.GetUseSize() > 0)
            //    goto RETRY;

            return false;
        }

        //if (true == DebugFlag)
        //    ProfileBegin(L"SendPost 2");

        p_Session->SendPacketCnt = BufCount;
        p_Session->SendPacketSize = BufSize;

        memset(&p_Session->SendOL, 0, sizeof(OVERLAPPED));
        InterlockedIncrement64(&p_Session->p_IOCompare->IOCount);

        // sendpacket에서 호출할 경우 릴리즈 된 상태에서 들어오는 경우가 있다. -> 현재는 없어야 한다.
        //if (SessionID != p_Session->SessionID)
        //    CrashDump::Crash();

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
                    //ClientReleaseDebug(p_Session, 2);
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

        if (true == p_Session->SendDisconnectFlag)
        {
            ClientShutdown(p_Session);
        }

        //if (true == DebugFlag)
        //{
        //    ProfileEnd(L"SendPost 2");
        //    ProfileEnd(L"SendPost");
        //}

        return true;
    }
    void CNetServerEx::CompleteRecv(st_SESSION *p_Session, DWORD cbTransferred)
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
        //int CompletePacketResult;
        //while (1)
        //{
        //    CompletePacketResult = CompletePacket(p_Session);
        //    if (Packet_Complete == CompletePacketResult)
        //        continue;
        //    else if (Packet_NotComplete == CompletePacketResult)
        //        break;
        //    else
        //    {
        //        // 로그는 안에서 찍는다.
        //        //SYSLOG(L"NetServer", LOG_ERROR, L"[%d]%s", p_Session->SessionID);
        //        //OnError(CompletePacketResult, L"[Invalid CompletePacketResult]");
        //
        //        ClientDisconnect(p_Session);        // 얘는 리시브도 막는다.
        //        //ClientShutdown(p_Session);
        //        return;
        //    }
        //}

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
    void CNetServerEx::CompleteSend(st_SESSION *p_Session, DWORD cbTransferred)
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

        //SendPost(p_Session->SessionID);
        //if (p_Session->SendQ.GetUseSize() > 0)
        //    SendPost(p_Session->SessionID);
        //SendPost(p_Session);

        //if (true == DebugFlag)
        //    ProfileEnd(L"CompleteSend");
    }
    void CNetServerEx::ClientShutdown(st_SESSION *p_Session)              // shutdown으로 안전한 종료 유도
    {
        shutdown(p_Session->Sock, SD_SEND);
    }
    void CNetServerEx::ClientDisconnect(st_SESSION *p_Session)            // socket 접속 강제 끊기
    {
        shutdown(p_Session->Sock, SD_BOTH);     // shutdown both
    }
    void CNetServerEx::SocketClose(SOCKET SessionSock)                    // closesocket(내부용)
    {
        //linger Ling;
        //Ling.l_onoff = 1;
        //Ling.l_linger = 0;
        //setsockopt(SessionSock, SOL_SOCKET, SO_LINGER, (char *)&Ling, sizeof(LINGER));
        closesocket(SessionSock);
    }

    void CNetServerEx::ClientRelease(st_SESSION *p_Session)               // client session 해제
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

        // 최대한 빨리 소켓을 반환한다.
        SocketClose(p_Session->Sock);

        // debug
        //p_Session->ReleaseType = Type;
        //p_Session->JoinFlag--;

        // ClientLeave를 여기서 호출한다.
        OnClientLeave(p_Session->SessionID);

        //SocketClose(p_Session->Sock);

        Packet *p_Packet;
        int ReleaseSessionIndex;
        st_ACCEPT_OVERLAPPED *p_ReleaseAcceptOL;

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
        p_ReleaseAcceptOL = p_Session->p_AcceptOL;

        p_Session->SendDisconnectFlag = false;
        p_Session->SessionID = SESSION_ID_DEFAULT;
        p_Session->Sock = INVALID_SOCKET;
        //memset(&p_Session->SessionAddr, 0, sizeof(SOCKADDR_IN));
        p_Session->p_AcceptOL = nullptr;

        InterlockedDecrement64(&_SessionCount);
        SetAcceptEx(&_ListenSock, p_ReleaseAcceptOL);
        _SessionEmptyStack->Push(ReleaseSessionIndex);
    }

    unsigned __stdcall CNetServerEx::AcceptThreadProc(LPVOID lpParam)
    {
        return ((CNetServerEx *)lpParam)->AcceptThread_Update();
    }
    //unsigned __stdcall CNetServerEx::AcceptThreadProc(LPVOID lpParam)
    unsigned CNetServerEx::AcceptThread_Update(void)
    {
        //CNetServerEx *p_This;
        int SessionAddrLen;
        sockaddr_in SessionAddr;
        //SOCKET SessionSock;
        st_CLIENT_CONNECT_INFO ClientConnectInfo;
        LONG64 SessionIndex;
        HANDLE hResult;
        DWORD RecvFlags;
        st_SESSION *p_Session;
        int Ret;

        BOOL NoDelayOptVal;
        DWORD KeepAliveResult;
        tcp_keepalive tcpkl;

        // Recv Flag Init
        RecvFlags = 0;

        // No Delay Setting
        NoDelayOptVal = true;               // No Delay 켠다.

        // keep alive setting
        tcpkl.onoff = 1;
        tcpkl.keepalivetime = 30000;        // 30초마다 keepalive 신호를 보내겠다.(윈도우 기본은 2시간)
        tcpkl.keepaliveinterval = 2000;     // keepalive 신호를 보내고 응답이 없으면 2초마다 재전송. (ms tcp는 10회 재시도 한다.)

        SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", 0, L"Accept Start");

        //p_This = (CNetServerEx *)lpParam;
        while (1)
        {
            SessionAddrLen = sizeof(SessionAddr);
            //SessionSock = accept(p_This->_ListenSock, (sockaddr *)&SessionAddr, &SessionAddrLen);
            //SessionSock = WSAAccept(p_This->_ListenSock, (sockaddr *)&SessionAddr, &SessionAddrLen, NULL, NULL);
            //ClientConnectInfo._Socket = WSAAccept(p_This->_ListenSock, (sockaddr *)&SessionAddr, &SessionAddrLen, NULL, NULL);
            ClientConnectInfo._Socket = WSAAccept(_ListenSock, (sockaddr *)&SessionAddr, &SessionAddrLen, NULL, NULL);
            if (INVALID_SOCKET == ClientConnectInfo._Socket)
            {
                // 여기는 종료할 때 탄다.
                int ErrorCode = WSAGetLastError();
                SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", ErrorCode, L"Accept Exit");
                return 0;
            }
            wsprintf(ClientConnectInfo._IP, L"%u.%u.%u.%u", SessionAddr.sin_addr.s_net, SessionAddr.sin_addr.s_host, SessionAddr.sin_addr.s_lh, SessionAddr.sin_addr.s_impno);
            ClientConnectInfo._Port = SessionAddr.sin_port;

            // debug
            _Monitor_AcceptTotal++;

            // Accept Counter 증가
            //p_This->_Monitor_AcceptCounter++;
            _Monitor_AcceptCounter++;

            // NoDelay
            //if (false == p_This->_Nagle)
            if (false == _Nagle)
            {
                //Ret = setsockopt(SessionSock, IPPROTO_TCP, TCP_NODELAY, (char *)&NoDelayOptVal, sizeof(BOOL));
                Ret = setsockopt(ClientConnectInfo._Socket, IPPROTO_TCP, TCP_NODELAY, (char *)&NoDelayOptVal, sizeof(BOOL));
                if (Ret != 0)
                {
                    CrashDump::Crash();
                    //p_This->SocketClose(SessionSock);
                    //p_This->SocketClose(ClientConnectInfo._Socket);
                    SocketClose(ClientConnectInfo._Socket);
                    continue;
                }
            }

            // keep alive
            //Ret = WSAIoctl(SessionSock, SIO_KEEPALIVE_VALS, &tcpkl, sizeof(tcp_keepalive), 0, 0, &KeepAliveResult, NULL, NULL);
            Ret = WSAIoctl(ClientConnectInfo._Socket, SIO_KEEPALIVE_VALS, &tcpkl, sizeof(tcp_keepalive), 0, 0, &KeepAliveResult, NULL, NULL);
            if (Ret != 0)
            {
                CrashDump::Crash();
                SocketClose(ClientConnectInfo._Socket);
                continue;
            }

            // linger
            linger Ling;
            Ling.l_onoff = 1;
            Ling.l_linger = 0;
            Ret = setsockopt(ClientConnectInfo._Socket, SOL_SOCKET, SO_LINGER, (char *)&Ling, sizeof(LINGER));
            if (Ret != 0)
            {
                CrashDump::Crash();
                SocketClose(ClientConnectInfo._Socket);
                continue;
            }

            //if (false == p_This->OnConnectionRequest(SessionAddr.sin_addr, SessionAddr.sin_port))
            //if (false == p_This->OnConnectionRequest(&ClientConnectInfo))
            if (false == OnConnectionRequest(&ClientConnectInfo))
            {
                //p_This->SocketClose(SessionSock);
                //p_This->SocketClose(ClientConnectInfo._Socket);
                SocketClose(ClientConnectInfo._Socket);
                continue;
            }

            // debug -> 만약 이걸 통과하고 스택에서 못 뽑으면 문제가 있다.
            //if (p_This->_SessionMax < InterlockedIncrement64(&p_This->_SessionCount))
            if (_SessionMax < InterlockedIncrement64(&_SessionCount))
            {
                CrashDump::Crash();
                //InterlockedDecrement64(&p_This->_SessionCount);
                InterlockedDecrement64(&_SessionCount);
                //SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", p_This->_SessionCount, L"Server Full Count Error");
                SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", _SessionCount, L"Server Full Count Error");
                //p_This->OnError(p_This->_SessionCount, L"Server Full Error");
                //p_This->SocketClose(SessionSock);
                //p_This->SocketClose(ClientConnectInfo._Socket);
                SocketClose(ClientConnectInfo._Socket);
                continue;
            }

            //SessionIndex = p_This->FindEmptySession();
            SessionIndex = FindEmptySession();
            if (-1 == SessionIndex)
            {
                //SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", p_This->_SessionCount, L"Server Full Stack Error");
                SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", _SessionCount, L"Server Full Stack Error");
                //p_This->OnError(p_This->_SessionCount, L"Server Full Error");
                //p_This->SocketClose(SessionSock);
                //p_This->SocketClose(ClientConnectInfo._Socket);
                SocketClose(ClientConnectInfo._Socket);
                continue;
            }
            //p_Session = &p_This->_Session[SessionIndex];
            p_Session = &_Session[SessionIndex];

            // 먼저 올리고 세팅한다.
            InterlockedIncrement64(&p_Session->p_IOCompare->IOCount);

            // 지금 이순간에도 IOCount는 증가할 수 있다.(SendPacket 때문) -> 체크하면 안된다.
            //if (InterlockedIncrement64(&p_Session->p_IOCompare->IOCount) != 1)
            //{
            //    // debug
            //    CrashDump::Crash();
            //    return -1;
            //}

            InterlockedCompareExchange64(&p_Session->p_IOCompare->ReleaseFlag, 0, 1);
            //if (InterlockedCompareExchange64(&p_Session->p_IOCompare->ReleaseFlag, 0, 1) != 1)
            //{
            //    // debug
            //    CrashDump::Crash();
            //    return -1;
            //}

            // debug
            //if (SESSION_ID_DEFAULT != p_Session->SessionID)
            //{
            //    CrashDump::Crash();
            //    return -1;
            //}
            //if (INVALID_SOCKET != p_Session->Sock)
            //{
            //    CrashDump::Crash();
            //    return -1;
            //}

            p_Session->SessionID = NewClientID(SessionIndex);
            //p_Session->SessionAddr = SessionAddr;
            //p_Session->Sock = SessionSock;
            p_Session->Sock = ClientConnectInfo._Socket;
            //memset(&p_Session->RecvOL, 0, sizeof(OVERLAPPED));
            //memset(&p_Session->SendOL, 0, sizeof(OVERLAPPED));
            //p_Session->RecvQ.Clear();
            //p_Session->SendQ.Clear();
            //p_Session->SendPtrQ.Clear();

            // debug
            //if (p_Session->RecvQ.GetUseSize() > 0)
            //{
            //    CrashDump::Crash();
            //    return -1;
            //}
            //if (p_Session->SendQ.GetUseSize() > 0)
            //{
            //    CrashDump::Crash();
            //    return -1;
            //}

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

            //WSABUF RecvWsaBuf;
            //RecvWsaBuf.buf = p_Session->RecvQ.GetWriteBufferPtr();
            //RecvWsaBuf.len = p_Session->RecvQ.GetNotBrokenPutSize();

            // debug
            //if (p_Session->JoinFlag != 0)
            //{
            //    CrashDump::Crash();
            //    continue;
            //}
            //p_Session->JoinFlag++;

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
            
            //Ret = WSARecv(p_Session->Sock, (LPWSABUF)&RecvWsaBuf, 1, NULL, &flags, &p_Session->RecvOL, NULL);
            //if (SOCKET_ERROR == Ret)
            //{
            //    Ret = WSAGetLastError();
            //    if (Ret != ERROR_IO_PENDING)
            //    {
            //        p_This->OnError(Ret, L"WSA Recv Error");
            //        p_This->ClientShutdown(p_Session);
            //        if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
            //        {
            //            p_This->ClientReleaseDebug(p_Session, 3);
            //            //p_This->ClientRelease(p_Session);
            //            continue;
            //        }
            //    }
            //}
        }

        return 0;
    }
    //unsigned __stdcall CNetServerEx::WorkerThreadProc(LPVOID lpParam)
    //{
    //    return ((CNetServerEx *)lpParam)->WorkerThread_Update();
    //}
    //unsigned CNetServerEx::WorkerThread_Update(void)
    //{
    //    BOOL Ret;
    //    DWORD cbTransferred;
    //    st_SESSION *p_Session;
    //    LPOVERLAPPED lpOverlapped;
    //    DWORD ErrorCode;
    //
    //    // debug
    //    //int IoType;
    //
    //    while (1)
    //    {
    //        // debug
    //        //IoType = 0;
    //
    //        cbTransferred = 0;
    //        p_Session = nullptr;
    //        lpOverlapped = nullptr;
    //
    //        Ret = GetQueuedCompletionStatus(_hIocp, &cbTransferred, (PULONG_PTR)&p_Session, &lpOverlapped, INFINITE);
    //
    //        if (NULL == lpOverlapped)               // PostQueuedCompletionStatus 로 메세지를 보낸 경우
    //        {
    //            switch (cbTransferred)
    //            {
    //            case TYPE_EXIT:
    //                if (p_Session != NULL)
    //                    CrashDump::Crash();
    //                return 0;
    //                //break;
    //            case TYPE_HEARTBEAT:
    //                if (p_Session != NULL)
    //                    CrashDump::Crash();
    //                OnWorkerThreadHeartBeat();
    //                break;
    //            case TYPE_SEND_PACKET:
    //                if (0 == InterlockedCompareExchange64(&p_Session->SendIO, 1, 0))        // 0이라면 1로 바꾼다.
    //                    SendPostIOCP(p_Session->SessionID);
    //
    //                if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
    //                    ClientRelease(p_Session);
    //                break;
    //            default:
    //                CrashDump::Crash();
    //            }
    //            continue;
    //
    //            //// 하트비트 체크
    //            //if (1 == cbTransferred && 1 == (int)(p_Session))
    //            //{
    //            //    OnWorkerThreadHeartBeat();
    //            //    continue;
    //            //}
    //            //// SendPost 체크
    //            //else if (TYPE_SEND_PACKET == cbTransferred)
    //            //{
    //            //    if (0 == InterlockedCompareExchange64(&p_Session->SendIO, 1, 0))        // 0이라면 1로 바꾼다.
    //            //        SendPostIOCP(p_Session->SessionID);
    //            //
    //            //    if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
    //            //        ClientRelease(p_Session);
    //            //    continue;
    //            //}
    //            //// 종료
    //            //if (0 == cbTransferred && nullptr == p_Session && nullptr == lpOverlapped)
    //            //{
    //            //    return 0;
    //            //}
    //        }
    //
    //        OnWorkerThreadBegin(p_Session->SessionID);
    //
    //        if (FALSE == Ret)
    //        {
    //            //IoType = 4;
    //
    //            ErrorCode = GetLastError();
    //            if (ErrorCode != ERROR_NETNAME_DELETED && ErrorCode != ERROR_SEM_TIMEOUT)
    //            {
    //                SYSLOG(L"GQCS", LOG_ERROR, L"[%d]%s", ErrorCode, L"GQCS Error");
    //                //p_This->OnError(ErrorCode, L"GQCS Error");
    //            }
    //
    //            //if (0 == cbTransferred)
    //            //{
    //            //    // 클라가 비정상 끊김.
    //            //    p_Session->_Disconnect = true;
    //            //}
    //            
    //            //shutdown(p_Session->Sock, SD_SEND);
    //            //p_This->ClientShutdown(p_Session);
    //            //ClientShutdown(p_Session);
    //            ClientDisconnect(p_Session);
    //        }
    //        else if (0 == cbTransferred)
    //        {
    //            //IoType = 11;
    //            //p_This->ClientShutdown(p_Session);
    //            //ClientShutdown(p_Session);
    //            ClientDisconnect(p_Session);
    //        }
    //        else
    //        {
    //            if (&p_Session->RecvOL == lpOverlapped)
    //            {
    //                //IoType = 8;
    //                //p_This->CompleteRecv(p_Session, cbTransferred);
    //                CompleteRecv(p_Session, cbTransferred);
    //            }
    //            else if (&p_Session->SendOL == lpOverlapped)
    //            {
    //                //IoType = 9;
    //                //p_This->CompleteSend(p_Session, cbTransferred);
    //                CompleteSend(p_Session, cbTransferred);
    //            }
    //            else
    //            {
    //                CrashDump::Crash();
    //                SYSLOG(L"GQCS", LOG_ERROR, L"[%d]%s", 0, L"Invalid Overlappped");
    //                //p_This->OnError(1, L"GQCS Error [Invalid Overlappped]");
    //                //p_This->ClientShutdown(p_Session);
    //                //ClientShutdown(p_Session);
    //                ClientDisconnect(p_Session);
    //            }
    //        }
    //
    //        if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
    //        {
    //            //p_This->ClientReleaseDebug(p_Session, IoType);
    //            //p_This->ClientRelease(p_Session);
    //            ClientRelease(p_Session);
    //        }
    //        //p_This->OnWorkerThreadEnd(p_Session->SessionID);
    //        OnWorkerThreadEnd(p_Session->SessionID);
    //
    //        //if (true == DebugFlag)
    //        //    ProfileEnd(L"GQCS IOComplete");
    //    }
    //
    //    return 0;
    //}
    unsigned __stdcall CNetServerEx::MonitorTPS_Thread(LPVOID lpParam)
    {
        return ((CNetServerEx *)lpParam)->MonitorTPS_Thread_update();
    }
    unsigned CNetServerEx::MonitorTPS_Thread_update(void)
    {
        DWORD HeartbeatTick = _TimeManager->GetTickTime();
        DWORD MonitorTick = _TimeManager->GetTickTime();
        while (1)
        {
            // 하트비트 체크
            if (_WorkerthreadHeartbeatTick != 0 && HeartbeatTick + _WorkerthreadHeartbeatTick < _TimeManager->GetTickTime())
            {
                PostQueuedCompletionStatus(_hIocp, TYPE_HEARTBEAT, NULL, NULL);
                HeartbeatTick = _TimeManager->GetTickTime();
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

    //unsigned __stdcall CNetServerEx::AcceptExThreadProc(LPVOID lpParam)
    //{
    //    return ((CNetServerEx *)lpParam)->AcceptExThread_Update();
    //}
    //unsigned CNetServerEx::AcceptExThread_Update(void)
    //{
    //    BOOL Ret;
    //    int OptRet;
    //    DWORD cbTransferred;
    //    st_ACCEPT_OVERLAPPED *p_AcceptOL;
    //    SOCKET *p_ListenSock;
    //    LPOVERLAPPED lpOverlapped;
    //    //st_ACCEPT_OVERLAPPED lpOverlapped;
    //    //DWORD ErrorCode;
    //
    //    sockaddr_in *p_LocalAddr;
    //    sockaddr_in *p_RemoteAddr;
    //    int p_LocalAddrLen;
    //    int p_RemoteAddrLen;
    //    st_CLIENT_CONNECT_INFO ClientConnectInfo;
    //
    //
    //    DWORD RecvFlags;
    //    LONG64 SessionIndex;
    //    st_SESSION *p_Session;
    //    HANDLE hResult;
    //
    //    BOOL NoDelayOptVal;
    //    DWORD KeepAliveResult;
    //    tcp_keepalive tcpkl;
    //    linger Ling;
    //
    //    // Recv Flag Init
    //    RecvFlags = 0;
    //
    //    // No Delay Setting
    //    NoDelayOptVal = true;               // No Delay 켠다.
    //
    //    // keep alive setting
    //    tcpkl.onoff = 1;
    //    tcpkl.keepalivetime = 30000;        // 30초마다 keepalive 신호를 보내겠다.(윈도우 기본은 2시간)
    //    tcpkl.keepaliveinterval = 2000;     // keepalive 신호를 보내고 응답이 없으면 2초마다 재전송. (ms tcp는 10회 재시도 한다.)
    //
    //    // Linger setting
    //    Ling.l_onoff = 1;
    //    Ling.l_linger = 0;
    //
    //    GUID GuidGetAcceptExSockaddrs;
    //    LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs;
    //    DWORD dwBytes;
    //    int WSAIoctlRet;
    //
    //    GuidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
    //    lpfnGetAcceptExSockaddrs = NULL;
    //
    //    while (1)
    //    {
    //        cbTransferred = 0;
    //        p_ListenSock = nullptr;
    //        lpOverlapped = nullptr;
    //
    //        Ret = GetQueuedCompletionStatus(_hIocpAccept, &cbTransferred, (PULONG_PTR)&p_ListenSock, &lpOverlapped, INFINITE);
    //
    //        if (*p_ListenSock != _ListenSock)
    //            CrashDump::Crash();
    //
    //        if (nullptr == p_ListenSock || nullptr == lpOverlapped)
    //        {
    //            int Error = GetLastError();
    //            CrashDump::Crash();
    //        }
    //        if (1 == (__int64)p_ListenSock && 0 == (__int64)lpOverlapped)
    //        {
    //            int Error = GetLastError();
    //            CrashDump::Crash();
    //        }
    //
    //        p_AcceptOL = (st_ACCEPT_OVERLAPPED *)lpOverlapped;
    //
    //        // 1. Context Update
    //        OptRet = setsockopt(p_AcceptOL->_Sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)p_AcceptOL->_p_ListenSock, sizeof(SOCKET));
    //        if ((DWORD)-1 == Ret || OptRet < 0)
    //        {
    //            // 접속이 끊어지면 에러발생 가능.
    //            int WSAError = WSAGetLastError();
    //            //CrashDump::Crash();
    //            continue;
    //        }
    //
    //        // 2. 새로운 소켓의 관리포인트 생성(걍 임의로 만든듯.)
    //
    //        // 3. GetAcceptExSockaddrs()로 클라 정보 얻어옴.
    //        p_LocalAddr = nullptr;
    //        p_RemoteAddr = nullptr;
    //        p_LocalAddrLen = 0;
    //        p_RemoteAddrLen = 0;
    //
    //        WSAIoctlRet = WSAIoctl(p_AcceptOL->_Sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
    //            &GuidGetAcceptExSockaddrs, sizeof(GuidGetAcceptExSockaddrs),
    //            &lpfnGetAcceptExSockaddrs, sizeof(lpfnGetAcceptExSockaddrs),
    //            &dwBytes, nullptr, nullptr);
    //        if (SOCKET_ERROR == WSAIoctlRet)
    //        {
    //            Ret = WSAGetLastError();
    //            CrashDump::Crash();
    //            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"WSAIoctl Failed with Error: %d", Ret);
    //            return 0;
    //        }
    //
    //        lpfnGetAcceptExSockaddrs(p_AcceptOL->_Buf, 0,
    //            sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
    //            (sockaddr **)&p_LocalAddr, &p_LocalAddrLen, (sockaddr **)&p_RemoteAddr, &p_RemoteAddrLen);
    //
    //        // 4. 연결된 소켓을 IOCP에 등록.
    //
    //
    //        _Monitor_AcceptTotal++;
    //        _Monitor_AcceptCounter++;
    //
    //        //wsprintf(p_AcceptOL->ClientConnectInfo._IP, L"%u.%u.%u.%u", p_RemoteAddr->sin_addr.s_net, p_RemoteAddr->sin_addr.s_host, p_RemoteAddr->sin_addr.s_lh, p_RemoteAddr->sin_addr.s_impno);
    //        //p_AcceptOL->ClientConnectInfo._Port = p_RemoteAddr->sin_port;
    //
    //        if (false == _Nagle)
    //        {
    //            Ret = setsockopt(p_AcceptOL->_Sock, IPPROTO_TCP, TCP_NODELAY, (char *)&NoDelayOptVal, sizeof(BOOL));
    //            if (Ret != 0)
    //            {
    //                CrashDump::Crash();
    //                SocketClose(p_AcceptOL->_Sock);
    //                continue;
    //            }
    //        }
    //
    //        // keep alive
    //        Ret = WSAIoctl(p_AcceptOL->_Sock, SIO_KEEPALIVE_VALS, &tcpkl, sizeof(tcp_keepalive), 0, 0, &KeepAliveResult, NULL, NULL);
    //        if (Ret != 0)
    //        {
    //            CrashDump::Crash();
    //            SocketClose(p_AcceptOL->_Sock);
    //            continue;
    //        }
    //
    //        // linger
    //        Ret = setsockopt(p_AcceptOL->_Sock, SOL_SOCKET, SO_LINGER, (char *)&Ling, sizeof(LINGER));
    //        if (Ret != 0)
    //        {
    //            CrashDump::Crash();
    //            SocketClose(p_AcceptOL->_Sock);
    //            continue;
    //        }
    //
    //        //------------------------------------------------------
    //        // 여기는 방법을 강구해 보자.
    //        ClientConnectInfo._Socket = p_AcceptOL->_Sock;
    //        wsprintf(ClientConnectInfo._IP, L"%u.%u.%u.%u", p_RemoteAddr->sin_addr.s_net, p_RemoteAddr->sin_addr.s_host, p_RemoteAddr->sin_addr.s_lh, p_RemoteAddr->sin_addr.s_impno);
    //        ClientConnectInfo._Port = p_RemoteAddr->sin_port;
    //        if (false == OnConnectionRequest(&ClientConnectInfo))
    //        {
    //            SocketClose(p_AcceptOL->_Sock);
    //            continue;
    //        }
    //        //------------------------------------------------------
    //
    //        SessionIndex = FindEmptySession();
    //        if (-1 == SessionIndex)
    //        {
    //            SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", _SessionCount, L"Server Full Stack Error");
    //            SocketClose(p_AcceptOL->_Sock);
    //            continue;
    //        }
    //        p_Session = &_Session[SessionIndex];
    //
    //        InterlockedIncrement64(&p_Session->p_IOCompare->IOCount);
    //        InterlockedCompareExchange64(&p_Session->p_IOCompare->ReleaseFlag, 0, 1);
    //
    //        InterlockedIncrement64(&_SessionCount);
    //
    //        p_Session->SessionID = NewClientID(SessionIndex);
    //        p_Session->Sock = p_AcceptOL->_Sock;
    //        //p_Session->SessionAddr = *p_RemoteAddr;
    //        p_Session->p_AcceptOL = p_AcceptOL;
    //        p_Session->SendPacketCnt = 0;
    //        p_Session->SendPacketSize = 0;
    //        p_Session->SendIO = 0;
    //
    //        hResult = CreateIoCompletionPort((HANDLE)p_Session->Sock, _hIocp, (ULONG_PTR)p_Session, 0);
    //        if (NULL == hResult)
    //        {
    //            int ErrorCode = GetLastError();
    //            CrashDump::Crash();
    //            SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", ErrorCode, L"Sock IOCP Error");
    //            return -1;
    //        }
    //
    //        OnClientJoin(p_Session->SessionID);    // Recv 거는 것보다 OnClientJoin이 먼저 들어가야함.
    //        RecvPost(p_Session);
    //
    //        if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
    //        {
    //            ClientRelease(p_Session);
    //            continue;
    //        }
    //    }
    //
    //    return 0;
    //}

    unsigned __stdcall CNetServerEx::WorkerExThreadProc(LPVOID lpParam)
    {
        return ((CNetServerEx *)lpParam)->WorkerExThread_Update();
    }
    unsigned CNetServerEx::WorkerExThread_Update(void)
    {
        BOOL Ret;
        DWORD cbTransferred;
        st_SESSION *p_Session;
        LPOVERLAPPED lpOverlapped;
        DWORD ErrorCode;

        int OptRet;
        st_ACCEPT_OVERLAPPED *p_AcceptOL;
        
        sockaddr_in *p_LocalAddr;
        sockaddr_in *p_RemoteAddr;
        int p_LocalAddrLen;
        int p_RemoteAddrLen;
        st_CLIENT_CONNECT_INFO ClientConnectInfo;

        DWORD RecvFlags;
        LONG64 SessionIndex;
        //st_SESSION *p_Session;
        HANDLE hResult;

        BOOL NoDelayOptVal;
        DWORD KeepAliveResult;
        tcp_keepalive tcpkl;
        linger Ling;

        // Recv Flag Init
        RecvFlags = 0;

        // No Delay Setting
        NoDelayOptVal = true;               // No Delay 켠다.

        // keep alive setting
        tcpkl.onoff = 1;
        tcpkl.keepalivetime = 30000;        // 30초마다 keepalive 신호를 보내겠다.(윈도우 기본은 2시간)
        tcpkl.keepaliveinterval = 2000;     // keepalive 신호를 보내고 응답이 없으면 2초마다 재전송. (ms tcp는 10회 재시도 한다.)

        // Linger setting
        Ling.l_onoff = 1;
        Ling.l_linger = 0;

        GUID GuidGetAcceptExSockaddrs;
        LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs;
        DWORD dwBytes;
        int WSAIoctlRet;

        GuidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
        lpfnGetAcceptExSockaddrs = NULL;

        while (1)
        {
            cbTransferred = 0;
            p_Session = nullptr;
            lpOverlapped = nullptr;

            Ret = GetQueuedCompletionStatus(_hIocp, &cbTransferred, (PULONG_PTR)&p_Session, &lpOverlapped, INFINITE);

            if ((SOCKET *)p_Session == &_ListenSock)
            {
                // AcceptEx
                p_AcceptOL = (st_ACCEPT_OVERLAPPED *)lpOverlapped;

                // 1. Context Update
                OptRet = setsockopt(p_AcceptOL->_Sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)p_AcceptOL->_p_ListenSock, sizeof(SOCKET));
                if ((DWORD)-1 == Ret || OptRet < 0)
                {
                    // 접속이 끊어지면 에러발생 가능.
                    int WSAError = WSAGetLastError();
                    //CrashDump::Crash();
                    continue;
                }

                // 2. 새로운 소켓의 관리포인트 생성(걍 임의로 만든듯.)

                // 3. GetAcceptExSockaddrs()로 클라 정보 얻어옴.
                p_LocalAddr = nullptr;
                p_RemoteAddr = nullptr;
                p_LocalAddrLen = 0;
                p_RemoteAddrLen = 0;

                WSAIoctlRet = WSAIoctl(p_AcceptOL->_Sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &GuidGetAcceptExSockaddrs, sizeof(GuidGetAcceptExSockaddrs),
                    &lpfnGetAcceptExSockaddrs, sizeof(lpfnGetAcceptExSockaddrs),
                    &dwBytes, nullptr, nullptr);
                if (SOCKET_ERROR == WSAIoctlRet)
                {
                    Ret = WSAGetLastError();
                    CrashDump::Crash();
                    SYSLOG(L"SYSTEM", LOG_SYSTEM, L"WSAIoctl Failed with Error: %d", Ret);
                    return 0;
                }

                lpfnGetAcceptExSockaddrs(p_AcceptOL->_Buf, 0,
                    sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
                    (sockaddr **)&p_LocalAddr, &p_LocalAddrLen, (sockaddr **)&p_RemoteAddr, &p_RemoteAddrLen);

                // 4. 연결된 소켓을 IOCP에 등록.


                _Monitor_AcceptTotal++;
                _Monitor_AcceptCounter++;

                //wsprintf(p_AcceptOL->ClientConnectInfo._IP, L"%u.%u.%u.%u", p_RemoteAddr->sin_addr.s_net, p_RemoteAddr->sin_addr.s_host, p_RemoteAddr->sin_addr.s_lh, p_RemoteAddr->sin_addr.s_impno);
                //p_AcceptOL->ClientConnectInfo._Port = p_RemoteAddr->sin_port;

                if (false == _Nagle)
                {
                    Ret = setsockopt(p_AcceptOL->_Sock, IPPROTO_TCP, TCP_NODELAY, (char *)&NoDelayOptVal, sizeof(BOOL));
                    if (Ret != 0)
                    {
                        CrashDump::Crash();
                        SocketClose(p_AcceptOL->_Sock);
                        continue;
                    }
                }

                // keep alive
                Ret = WSAIoctl(p_AcceptOL->_Sock, SIO_KEEPALIVE_VALS, &tcpkl, sizeof(tcp_keepalive), 0, 0, &KeepAliveResult, NULL, NULL);
                if (Ret != 0)
                {
                    CrashDump::Crash();
                    SocketClose(p_AcceptOL->_Sock);
                    continue;
                }

                // linger
                Ret = setsockopt(p_AcceptOL->_Sock, SOL_SOCKET, SO_LINGER, (char *)&Ling, sizeof(LINGER));
                if (Ret != 0)
                {
                    CrashDump::Crash();
                    SocketClose(p_AcceptOL->_Sock);
                    continue;
                }

                //------------------------------------------------------
                // 여기는 방법을 강구해 보자.
                ClientConnectInfo._Socket = p_AcceptOL->_Sock;
                wsprintf(ClientConnectInfo._IP, L"%u.%u.%u.%u", p_RemoteAddr->sin_addr.s_net, p_RemoteAddr->sin_addr.s_host, p_RemoteAddr->sin_addr.s_lh, p_RemoteAddr->sin_addr.s_impno);
                ClientConnectInfo._Port = p_RemoteAddr->sin_port;
                if (false == OnConnectionRequest(&ClientConnectInfo))
                {
                    SocketClose(p_AcceptOL->_Sock);
                    continue;
                }
                //------------------------------------------------------

                SessionIndex = FindEmptySession();
                if (-1 == SessionIndex)
                {
                    SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", _SessionCount, L"Server Full Stack Error");
                    SocketClose(p_AcceptOL->_Sock);
                    continue;
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
                p_Session->Sock = p_AcceptOL->_Sock;
                //p_Session->SessionAddr = *p_RemoteAddr;
                p_Session->p_AcceptOL = p_AcceptOL;
                p_Session->SendPacketCnt = 0;
                p_Session->SendPacketSize = 0;
                p_Session->SendIO = 0;

                hResult = CreateIoCompletionPort((HANDLE)p_Session->Sock, _hIocp, (ULONG_PTR)p_Session, 0);
                if (NULL == hResult)
                {
                    int ErrorCode = GetLastError();
                    CrashDump::Crash();
                    SYSLOG(L"AcceptThread", LOG_SYSTEM, L"[%d]%s", ErrorCode, L"Sock IOCP Error");
                    return -1;
                }

                OnClientJoin(p_Session->SessionID);    // Recv 거는 것보다 OnClientJoin이 먼저 들어가야함.
                RecvPost(p_Session);

                if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
                {
                    ClientRelease(p_Session);
                    continue;
                }

                continue;
            }

            if (NULL == lpOverlapped)               // PostQueuedCompletionStatus 로 메세지를 보낸 경우
            {
                switch (cbTransferred)
                {
                case TYPE_EXIT:
                    if (p_Session != NULL)
                        CrashDump::Crash();
                    return 0;
                    //break;
                case TYPE_HEARTBEAT:
                    if (p_Session != NULL)
                        CrashDump::Crash();
                    OnWorkerThreadHeartBeat();
                    break;
                case TYPE_SEND_PACKET:
                    if (p_Session->SendQ.GetUseSize() > 0)
                    {
                        if (0 == InterlockedCompareExchange64(&p_Session->SendIO, 1, 0))        // 0이라면 1로 바꾼다.
                            SendPostIOCP(p_Session->SessionID);
                    }

                    if (InterlockedDecrement64(&p_Session->p_IOCompare->IOCount) == 0)
                        ClientRelease(p_Session);
                    break;
                default:
                    CrashDump::Crash();
                }
                continue;
            }

            OnWorkerThreadBegin(p_Session->SessionID);

            if (FALSE == Ret)
            {
                ErrorCode = GetLastError();
                if (ErrorCode != ERROR_NETNAME_DELETED && ErrorCode != ERROR_SEM_TIMEOUT)
                    SYSLOG(L"GQCS", LOG_ERROR, L"[%d]%s", ErrorCode, L"GQCS Error");
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
                    CrashDump::Crash();
                    SYSLOG(L"GQCS", LOG_ERROR, L"[%d]%s", 0, L"Invalid Overlappped");
                    //ClientShutdown(p_Session);
                    ClientDisconnect(p_Session);
                }
            }

            if (0 == InterlockedDecrement64(&p_Session->p_IOCompare->IOCount))
                ClientRelease(p_Session);
            OnWorkerThreadEnd(p_Session->SessionID);
        }

        return 0;
    }
    unsigned __stdcall CNetServerEx::SendThreadProc(LPVOID lpParam)
    {
        return ((CNetServerEx *)lpParam)->SendThread_Update();
    }
    unsigned CNetServerEx::SendThread_Update(void)
    {
        int SessionIndex;
        st_SESSION *p_Session;
        __int64 SessionID;
        while (1)
        {
            for (SessionIndex = 0; SessionIndex < _SessionMax; ++SessionIndex)
            {
                if (INVALID_SOCKET == _Session[SessionIndex].Sock)
                    continue;

                p_Session = &_Session[SessionIndex];
                SessionID = p_Session->SessionID;

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
                
                PostQueuedCompletionStatus(_hIocp, TYPE_SEND_PACKET, (ULONG_PTR)p_Session, (LPOVERLAPPED)NULL);
                //if (p_Session->SendQ.GetUseSize() > 0)
                //    PostQueuedCompletionStatus(_hIocp, TYPE_SEND_PACKET, (ULONG_PTR)p_Session, (LPOVERLAPPED)NULL);
            }
            Sleep(1);
        }
        return 0;
    }
}