#include "LibHeader.h"

namespace MonLib
{
    long CMMOServer::_Monitor_Counter_PacketSend = 0;

    CMMOServer::CMMOServer(int MaxSession)
        : _MaxSession(MaxSession), _MemoryPool_ConnectInfo(CONNECT_POOL_CHUNK_SIZE, false)
    {
        int Cnt;

        // 타임 매니저 가져오기
        _TimeManager = TimeManager::GetInstance();

        // 패킷풀 초기화
        Packet::MemoryPool(PACKET_POOL_CHUNK_SIZE);

        // 프로파일러 초기화
        ProfileInitial();

        // 멤버변수 초기화
        if (_MaxSession > SessionMax || _MaxSession < 1)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"SessionMax Invalid # [SessionMax:%d][Input:%d]", (int)SessionMax, MaxSession);
            const_cast<int&>(_MaxSession) = SessionMax;
        }

        _Shutdown = false;
        _ShutdownListen = false;
        _ShutdownAuthThread = false;
        _ShutdownGameThread = false;
        _ShutdownSendThread = false;

        _ListenSocket = INVALID_SOCKET;

        _EnableNagle = false;       // 일단 NoDelay 켠다.
        _WorkerThread = 0;

        _ListenPort = 0;

        _AcceptThread       = INVALID_HANDLE_VALUE;
        _AuthThread         = INVALID_HANDLE_VALUE;
        _GameUpdateThread   = INVALID_HANDLE_VALUE;
        for (Cnt = 0; Cnt < WORKER_THREAD_MAX_COUNT; ++Cnt)
            _IOCPWorkerThread[Cnt] = INVALID_HANDLE_VALUE;
        _IOCP               = INVALID_HANDLE_VALUE;
        _SendThread         = INVALID_HANDLE_VALUE;

        _SessionKeyIndex = 0;
        _SessionArray = new NetSession *[_MaxSession];
        for (Cnt = 0; Cnt < _MaxSession; ++Cnt)
            _SessionArray[Cnt] = nullptr;

        // 모니터링 변수 초기화
        _Monitor_AcceptSocket       = 0;
        _Monitor_SessionAllMode     = 0;
        _Monitor_SessionAuthMode    = 0;
        _Monitor_SessionGameMode    = 0;

        _Monitor_Counter_Accept     = 0;
        _Monitor_Counter_PacketSend = 0;
        _Monitor_Counter_PackerProc = 0;
        _Monitor_Counter_AuthUpdate = 0;
        _Monitor_Counter_GameUpdate = 0;

        _Monitor_Counter_DBWriteTPS = 0;
        //_Monitor_Counter_DBWriteMSG = 0;

        // winsock init
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            CrashDump::Crash();
            return;
        }
    }
    CMMOServer::~CMMOServer(void)
    {
        if (false == _Shutdown)
            Stop();
        WSACleanup();

        delete[] _SessionArray;         // 이건 언제가 적절할까?
    }

    bool CMMOServer::Start(WCHAR *p_ListenIP, USHORT Port, int WorkerThread, bool EnableNagle, BYTE PacketCode, BYTE PacketKey1, BYTE PacketKey2)
    {
        // 서버 이미 켜졌는지 체크할 것.

        if (nullptr == p_ListenIP)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"IP Address Invalid");
            return false;
        }

        __int64 SessionIndex;
        int Cnt;
        unsigned int ThreadId;
        __int64 ReverseSessionIndex;

        // 서버 설정값 세팅
        wmemcpy_s(_ListenIP, IP_V4_MAX_LEN + 1, p_ListenIP, IP_V4_MAX_LEN + 1);
        _ListenPort = Port;

        // worker thread count
        _WorkerThread = WORKER_THREAD_MAX_COUNT;
        if (WorkerThread > 0 && WorkerThread < WORKER_THREAD_MAX_COUNT)
            _WorkerThread = WorkerThread;

        // nagle setting
        _EnableNagle = EnableNagle;

        // packet code setting
        Packet::_PacketCode = PacketCode;
        Packet::_XORCode1 = PacketKey1;             // 변수명을 다시 생각해 보자.
        Packet::_XORCode2 = PacketKey2;

        // Session Key Init
        _SessionKeyIndex = 0;

        // Session Init(여러 사정에 의해서 역순으로 넣는다. -> 뺄때는 정방향)
        for (SessionIndex = 0; SessionIndex < _MaxSession; ++SessionIndex)
        {
            ReverseSessionIndex = _MaxSession - 1 - SessionIndex;
            _SessionArray[ReverseSessionIndex]->SessionInit();
            if (false == _BlankSessionStack.Push(ReverseSessionIndex))
                CrashDump::Crash();
        }
        //for (SessionIndex = 0; SessionIndex < _MaxSession; ++SessionIndex)
        //{
        //    _SessionArray[SessionIndex]->SessionInit();
        //    if (false == _BlankSessionStack.Push(SessionIndex))
        //        CrashDump::Crash();
        //}

        // IOCP
        _IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, _WorkerThread);       // 아래에서 에러날 시 IOCP 반환 고려할 것.
        if (NULL == _IOCP)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"CreateIoCompletionPort Failed with Error: %u", GetLastError());
            return false;
        }

        // WorkerThread
        for (Cnt = 0; Cnt < _WorkerThread; ++Cnt)
        {
            _IOCPWorkerThread[Cnt] = (HANDLE)_beginthreadex(NULL, 0, IOCPWorkerThread, this, 0, &ThreadId);
            if (INVALID_HANDLE_VALUE == _IOCPWorkerThread[Cnt])
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"WorkerThread Creation Error");
                return false;
            }
        }

        //----------------------------------------
        // Network Init
        //----------------------------------------
        SOCKADDR_IN ServerAddr;
        int Ret;

        _ListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (INVALID_SOCKET == _ListenSocket)
        {
            Ret = WSAGetLastError();
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Create of ListenSocket Failed with Error: %d", Ret);
            return false;
        }

        ServerAddr.sin_family = AF_INET;
        ServerAddr.sin_port = htons(_ListenPort);
        InetPton(AF_INET, _ListenIP, &(ServerAddr.sin_addr));
        Ret = bind(_ListenSocket, (sockaddr *)&ServerAddr, sizeof(ServerAddr));
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Listen Socket Bind Error: %d", Ret);
            closesocket(_ListenSocket);
            return false;
        }

        Ret = listen(_ListenSocket, SOMAXCONN);
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Listen Socket Listen Error: %d", Ret);
            closesocket(_ListenSocket);
            return false;
        }

        // 스레드 생성
        if (false == CreateThread())
            return false;

        return true;
    }
    bool CMMOServer::Stop(void)
    {
        /*
        1. 억셉트 중지.(넷서버와 동일)

2. Auth 스레드 중지.

3. 클라이언트가 모드가 none이 아닐 때 디스커넥트시도.

4. 다 나갔는지 체크.

5. 컨텐츠(게임스레드) 중지.

6. SendThread 중지.

7. 워커스레드 중지.
        */

        int Cnt;
        bool SessionShutdownFlag;
        int SessionShutdownTryCount;
        bool SessionDisconnectFlag;
        int SessionDisconnectTryCount;

        _Shutdown = true;

        // 1. 억셉트 중지.(넷서버와 동일)
        _ShutdownListen = true;

        closesocket(_ListenSocket);
        _ListenSocket = INVALID_SOCKET;
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [1][Listen Socket Closed]");

        // 2. Auth 스레드 중지.
        _ShutdownAuthThread = true;
        if (WaitForSingleObject(_AuthThread, 5000) != WAIT_OBJECT_0)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [Error][Terminate Auth Thread]");
            TerminateThread(_AuthThread, 0);
        }
        CloseHandle(_AuthThread);
        _AuthThread = INVALID_HANDLE_VALUE;
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [2][Auth Thread Exit]");

        // 3. 클라이언트가 모드가 none이 아닐 때 디스커넥트시도.
        for (Cnt = 0; Cnt < _MaxSession; ++Cnt)
        {
            if (SESSION_ID_DEFAULT == _SessionArray[Cnt]->_SessionID)
                continue;

            if (MODE_NONE == _SessionArray[Cnt]->_Mode)
                continue;

            SessionShutdownFlag = false;
            SessionShutdownTryCount = 10;
            while (SessionShutdownTryCount-- > 0)
            {
                if (_SessionArray[Cnt]->_SendQ.GetUseSize() <= 0)
                {
                    _SessionArray[Cnt]->Disconnect(false);
                    SessionShutdownFlag = true;
                    break;
                }
                Sleep(100);
            }

            if (false == SessionShutdownFlag)
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [Error][Session Send Queue is Not Empty][SendQ Size:%d]", _SessionArray[Cnt]->_SendQ.GetUseSize());
            }
        }
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [3][Session Shutdown]");

        // 4. 다 나갔는지 체크.
        for (Cnt = 0; Cnt < _MaxSession; ++Cnt)
        {
            if (SESSION_ID_DEFAULT == _SessionArray[Cnt]->_SessionID)
                continue;

            SessionDisconnectFlag = false;
            SessionDisconnectTryCount = 10;
            while (SessionDisconnectTryCount-- > 0)
            {
                if (SESSION_ID_DEFAULT == _SessionArray[Cnt]->_SessionID)
                {
                    SessionDisconnectFlag = true;
                    break;
                }
                Sleep(100);
            }

            if (false == SessionDisconnectFlag)
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [Error][Session is Not Disconnected]");
            }
        }
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [4][Session Disconnected]");

        // 5. 컨텐츠(게임스레드) 중지.
        _ShutdownGameThread = true;
        if (WaitForSingleObject(_GameUpdateThread, 5000) != WAIT_OBJECT_0)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [Error][Terminate Game Thread]");
            TerminateThread(_GameUpdateThread, 0);
        }
        CloseHandle(_GameUpdateThread);
        _GameUpdateThread = INVALID_HANDLE_VALUE;
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [5][Game Thread Exit]");

        // 6. SendThread 중지.
        _ShutdownSendThread = true;
        if (WaitForSingleObject(_SendThread, 5000) != WAIT_OBJECT_0)
        {
            SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [Error][Terminate Send Thread]");
            TerminateThread(_SendThread, 0);
        }
        CloseHandle(_SendThread);
        _SendThread = INVALID_HANDLE_VALUE;
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [6][Send Thread Exit]");

        // 7. 워커스레드 중지.
        for (Cnt = 0; Cnt < _WorkerThread; ++Cnt)
        {
            PostQueuedCompletionStatus(_IOCP, 0, 0, NULL);
        }
        for (Cnt = 0; Cnt < _WorkerThread; ++Cnt)
        {
            if (WaitForSingleObject(_IOCPWorkerThread[Cnt], 10000) != WAIT_OBJECT_0)
            {
                SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [Error][Terminate Worker Thread]");
                TerminateThread(_IOCPWorkerThread[Cnt], 0);
            }
        }
        for (Cnt = 0; Cnt < _WorkerThread; ++Cnt)
        {
            CloseHandle(_IOCPWorkerThread[Cnt]);
            _IOCPWorkerThread[Cnt] = INVALID_HANDLE_VALUE;
        }
        SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Stop # [7][Worker Thread Exit]");



        return true;
    }

    void CMMOServer::SetSessionArray(int ArrayIndex, NetSession *p_Session)
    {
        _SessionArray[ArrayIndex] = p_Session;
    }

    bool CMMOServer::CreateThread(void)
    {
        //int Cnt;
        unsigned int ThreadId;

        // Send Thread
        _SendThread = (HANDLE)_beginthreadex(NULL, 0, SendThread, this, 0, &ThreadId);
        if (INVALID_HANDLE_VALUE == _SendThread)
            return false;

        // WorkerThread
        //for (Cnt = 0; Cnt < _WorkerThread; ++Cnt)
        //{
        //    _IOCPWorkerThread[Cnt] = (HANDLE)_beginthreadex(NULL, 0, IOCPWorkerThread, this, 0, &ThreadId);
        //    if (INVALID_HANDLE_VALUE == _IOCPWorkerThread[Cnt])
        //        return false;
        //}

        // GameUpdateThread
        _GameUpdateThread = (HANDLE)_beginthreadex(NULL, 0, GameUpdateThread, this, 0, &ThreadId);
        if (INVALID_HANDLE_VALUE == _GameUpdateThread)
            return false;

        // AuthThread
        _AuthThread = (HANDLE)_beginthreadex(NULL, 0, AuthThread, this, 0, &ThreadId);
        if (INVALID_HANDLE_VALUE == _AuthThread)
            return false;

        // AccpetThread
        _AcceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, 0, &ThreadId);
        if (INVALID_HANDLE_VALUE == _AcceptThread)
            return false;

        return true;
    }
    bool CMMOServer::CreateIOCP_Socket(SOCKET Socket, ULONG_PTR Key)
    {
        HANDLE IOCPResult = CreateIoCompletionPort((HANDLE)Socket, _IOCP, Key, 0);
        if (NULL == IOCPResult)
            return false;
        return true;
    }

    //void CMMOServer::SendPacket_GameAll(Packet *p_Packet, __int64 ExcludeSessionID)
    //{
    //    // 이건 끊어서 보내야 할듯.
    //}
    //void CMMOServer::SendPacket(Packet *p_Packet, __int64 SessionID)
    //{
    //    if (SESSION_ID_DEFAULT == SessionID)
    //        CrashDump::Crash();
    //
    //    int SessionIndex;
    //    for (SessionIndex = 0; SessionIndex < _MaxSession; ++SessionIndex)
    //    {
    //        if (SessionID == _SessionArray[SessionIndex]->_SesionID)
    //        {
    //            if (false == _SessionArray[SessionIndex]->SendPacket(p_Packet))
    //                CrashDump::Crash();
    //            break;
    //        }
    //    }
    //}

    void CMMOServer::Error(int ErrorCode, WCHAR *p_FormatStr, ...)
    {

    }

    void CMMOServer::ProcAuth_Accept(void)
    {
        __int64 NewSessionID;
        __int64 NewSessionIndex;
        NetSession *p_NewSession;
        st_CLIENT_CONNECT_INFO *p_ClientConnectInfo;
        
        // 1. AcceptSocketQueue 뽑음.
        while (true == _AcceptSocketQueue.Dequeue(&p_ClientConnectInfo))
        {
            // 2. 세션 고유 번호 CLIENT_ID 할당
            //NewSessionID = InterlockedIncrement64(&_SessionKeyIndex);

            // 3. 빈 세션을 찾아 등록
            if (false == _BlankSessionStack.Pop(&NewSessionIndex))
            {
                //CrashDump::Crash();
                SocketClose(p_ClientConnectInfo->_Socket);
                if (false == _MemoryPool_ConnectInfo.Free(p_ClientConnectInfo))
                {
                    // 일어나서는 안되는 상황.
                    CrashDump::Crash();
                    SocketClose(p_ClientConnectInfo->_Socket);
                    return;
                }
                continue;
            }
            if (_SessionArray[NewSessionIndex]->_Mode != MODE_NONE)
            {
                // 일어나서는 안되는 상황.(초기화에 문제가 있다.)
                CrashDump::Crash();
                return;
            }

            // 배열 인덱스를 알아야 SessionID를 할당할 수 있기 때문에 여기서 진행한다.
            NewSessionID = ((InterlockedIncrement64(&_SessionKeyIndex) & 0x00ffffffffffffff) | (NewSessionIndex << 48));       // 새로운 클라이언트 아이디

            // 나머지는 Release할 때 초기화 되었다는 전제이다.
            p_NewSession = _SessionArray[NewSessionIndex];

            p_NewSession->_SessionID = NewSessionID;
            p_NewSession->_ArrayIndex = NewSessionIndex;
            p_NewSession->_ClientInfo._Socket = p_ClientConnectInfo->_Socket;
            wmemcpy_s(p_NewSession->_ClientInfo._IP, IP_V4_MAX_LEN + 1, p_ClientConnectInfo->_IP, IP_V4_MAX_LEN + 1);
            p_NewSession->_ClientInfo._Port = p_ClientConnectInfo->_Port;

            if (false == _MemoryPool_ConnectInfo.Free(p_ClientConnectInfo))
            {
                // 일어나서는 안되는 상황.
                CrashDump::Crash();
                SocketClose(p_ClientConnectInfo->_Socket);
                return;
            }

            // 4. 소켓 IOCP 등록
            InterlockedIncrement64(&p_NewSession->_IOCount);
            if (false == CreateIOCP_Socket(p_NewSession->_ClientInfo._Socket, (ULONG_PTR)p_NewSession))
            {
                CrashDump::Crash();
                SocketClose(p_NewSession->_ClientInfo._Socket);
                return;
            }

            // 5. OnAuth_ClientJoin 호출
            p_NewSession->OnAuth_ClientJoin();

            // 6. WSARecv 등록
            if (false == p_NewSession->RecvPost())
            {
                // 여기까지 온 이상 게임스레드에서 해제한다. 여기서 해줄 건 없다.

                //CrashDump::Crash();
                //continue;       // 여기까지 온 이상 io count가 0이라면 접속해제가 될 것이다.
            }
            if (0 == InterlockedDecrement64(&p_NewSession->_IOCount))
            {
                p_NewSession->_LogoutFlag = true;
                //continue;
            }

            // 7. MODE_AUTH로 전환
            p_NewSession->_Mode = MODE_AUTH;
            p_NewSession->_LastRecvPacketTime = _TimeManager->GetTickTime();

            InterlockedIncrement(&_Monitor_SessionAuthMode);
            InterlockedIncrement(&_Monitor_SessionAllMode);
        }
    }
    void CMMOServer::ProcAuth_Packet(void)
    {
        int SessionIndex;
        int PacketCnt;
        Packet *p_Packet;

        for (SessionIndex = 0; SessionIndex < _MaxSession; ++SessionIndex)
        {
            // 1. 모든 세션에대해서 MODE_AUTH 모드 확인.
            if (_SessionArray[SessionIndex]->_Mode != MODE_AUTH)
                continue;

            // 2. 각 세션의 CompletePacket (수신,복호화 완료 패킷) 뽑기
            PacketCnt = AUTH_PACKET_PER_FRAME;
            while (PacketCnt-- > 0)
            {
                if (true == _SessionArray[SessionIndex]->_AuthToGameFlag)
                    break;

                if (false == _SessionArray[SessionIndex]->_CompletePacketQ.Dequeue(&p_Packet))
                    break;

                //3. pSession->OnAuth_Packet 호출
                //
                //   패킷 로직 처리(컨텐츠 부분)
                //    - Login 패킷처리
                //    - 기타 로비 패킷 처리
                //    - 게임모드 로의 전환(MODE_GAME)
                _SessionArray[SessionIndex]->OnAuth_Packet(p_Packet);
                p_Packet->Free();
            }

            // 4. _bLogoutFlag true 인 경우 MODE_LOGOUT_IN_AUTH 로 변경
            if (true == _SessionArray[SessionIndex]->_LogoutFlag)
                _SessionArray[SessionIndex]->_Mode = MODE_LOGOUT_IN_AUTH;

            // 일정 시간 통신이 없다면 끊음.
            // Last시간 저장 후 OnAuth_Timeout() 호출(디스커넥트 트루)
            if (_SessionArray[SessionIndex]->_LastRecvPacketTime + AUTH_THREAD_TIMEOUT < _TimeManager->GetTickTime())
            {
                // 로그 남긴다.
                //_SessionArray[SessionIndex]->_LastRecvPacketTime = _TimeManager->GetTickTime();
                _SessionArray[SessionIndex]->OnAuth_Timeout();
            }
        }
    }
    void CMMOServer::ProcAuth_Logout(void)
    {
        int SessionIndex;
        for (SessionIndex = 0; SessionIndex < _MaxSession; ++SessionIndex)
        {
            // 1. 모든 세션에 대해서 MODE_LOGOUT_IN_AUTH 모드 세션 확인...
            if (MODE_LOGOUT_IN_AUTH == _SessionArray[SessionIndex]->_Mode && 0 == _SessionArray[SessionIndex]->_SendIO)
            {
                // 2. OnAuth_ClientLeave 호출
                _SessionArray[SessionIndex]->OnAuth_ClientLeave(false);

                // 3. MODE_WAIT_LOGOUT 으로 전환.
                _SessionArray[SessionIndex]->_Mode = MODE_WAIT_LOGOUT;

                InterlockedDecrement(&_Monitor_SessionAuthMode);
            }

            // 4. _bAuthToGameFlag (AUTH 에서 GAME 으로 전환희망) 인 세션에 대해서 MODE_AUTH_TO_GAME 로 전환.
            //사실 이 부분은 굳이 2단계를 거칠 필요는 없으나 차후 해당 모드의 
            //추가적인 릴리즈 과정이 필요하다거나 완벽한 단계 분리를 위해서 2단계로 진행 시켜 봄
            else if (true == _SessionArray[SessionIndex]->_AuthToGameFlag && MODE_AUTH == _SessionArray[SessionIndex]->_Mode)
            {
                //_SessionArray[SessionIndex]->_AuthToGameFlag = false;       // 안내려도 상관 없을듯?

                _SessionArray[SessionIndex]->OnAuth_ClientLeave(true);
                _SessionArray[SessionIndex]->_Mode = MODE_AUTH_TO_GAME;

                InterlockedDecrement(&_Monitor_SessionAuthMode);
            }
        }
    }

    void CMMOServer::ProcGame_AuthToGame(void)
    {
        // 1. 모든 세션을 돌면서 MODE_AUTH_TO_GAME > MODE_GAME 으로 전환
        int SessionIndex;
        for (SessionIndex = 0; SessionIndex < _MaxSession; ++SessionIndex)
        {
            if (MODE_AUTH_TO_GAME == _SessionArray[SessionIndex]->_Mode)
            {
                _SessionArray[SessionIndex]->OnGame_ClientJoin();
                _SessionArray[SessionIndex]->_Mode = MODE_GAME;
                InterlockedIncrement(&_Monitor_SessionGameMode);

                // 시간을 갱신해줘야 안끊긴다.
                _SessionArray[SessionIndex]->_LastRecvPacketTime = _TimeManager->GetTickTime();
            }
        }
    }
    void CMMOServer::ProcGame_Packet(void)
    {
        // 2. 모든 세션을 돌면서 MODE_GAME 중인 세션에 대해 패킷 프로세싱
        int SessionIndex;
        int PacketCnt;
        Packet *p_Packet;

        for (SessionIndex = 0; SessionIndex < _MaxSession; ++SessionIndex)
        {
            // 1. 모든 세션에대해서 MODE_GAME 모드 확인.
            if (_SessionArray[SessionIndex]->_Mode != MODE_GAME)
                continue;

            // 2. 각 세션의 CompletePacket (수신,복호화 완료 패킷) 뽑기
            PacketCnt = GAME_PACKET_PER_FRAME;
            while (PacketCnt-- > 0)
            {
                if (false == _SessionArray[SessionIndex]->_CompletePacketQ.Dequeue(&p_Packet))
                    break;

                //3. pSession->OnGame_Packet 호출
                _SessionArray[SessionIndex]->OnGame_Packet(p_Packet);
                p_Packet->Free();
            }

            // 4. _bLogoutFlag true 인 경우 MODE_LOGOUT_IN_GAME 으로 변경
            if (true == _SessionArray[SessionIndex]->_LogoutFlag)
                _SessionArray[SessionIndex]->_Mode = MODE_LOGOUT_IN_GAME;

            // 일정 시간 통신이 없다면 끊음.
            // Last시간 저장 후 OnGame_Timeout() 호출(디스커넥트 트루)
            if (_SessionArray[SessionIndex]->_LastRecvPacketTime + GAME_THREAD_TIMEOUT < _TimeManager->GetTickTime())
            {
                // 로그 남긴다.
                //_SessionArray[SessionIndex]->_LastRecvPacketTime = _TimeManager->GetTickTime();
                _SessionArray[SessionIndex]->OnGame_Timeout();
            }
        }
    }
    void CMMOServer::ProcGame_Logout(void)
    {
        // 5. 모든 세션을 돌면서 MODE_LOGOUT_IN_GAME 모드의 세션을 종료처리 > MODE_WAIT_LOGOUT 전환
        int SessionIndex;
        for (SessionIndex = 0; SessionIndex < _MaxSession; ++SessionIndex)
        {
            if (MODE_LOGOUT_IN_GAME == _SessionArray[SessionIndex]->_Mode && 0 == _SessionArray[SessionIndex]->_SendIO)
            {
                // 2. OnGame_ClientLeave 호출
                _SessionArray[SessionIndex]->OnGame_ClientLeave();

                // 3. MODE_WAIT_LOGOUT 으로 전환.
                _SessionArray[SessionIndex]->_Mode = MODE_WAIT_LOGOUT;

                InterlockedDecrement(&_Monitor_SessionGameMode);
            }
        }
    }

    void CMMOServer::ProcGame_Release(void)
    {
        // 6. 모든 세션을 돌면서 MODE_WAIT_LOGOUT 모드의 실제 세션 릴리즈 처리
        __int64 SessionIndex;
        for (SessionIndex = 0; SessionIndex < _MaxSession; ++SessionIndex)
        {
            if (MODE_WAIT_LOGOUT == _SessionArray[SessionIndex]->_Mode && 0 == _SessionArray[SessionIndex]->_SendIO)
            {
                SocketClose(_SessionArray[SessionIndex]->_ClientInfo._Socket);

                _SessionArray[SessionIndex]->OnGame_ClientRelease();

                InterlockedDecrement(&_Monitor_SessionAllMode);
                _SessionArray[SessionIndex]->SessionInit();
                if (false == _BlankSessionStack.Push(SessionIndex))
                {
                    CrashDump::Crash();
                }
            }
        }
    }

    void CMMOServer::SocketClose(SOCKET SessionSock)                    // closesocket(내부용)
    {
        linger Ling;
        Ling.l_onoff = 1;
        Ling.l_linger = 0;
        setsockopt(SessionSock, SOL_SOCKET, SO_LINGER, (char *)&Ling, sizeof(LINGER));
        closesocket(SessionSock);

        InterlockedDecrement(&_Monitor_AcceptSocket);
    }
    
    unsigned __stdcall CMMOServer::AcceptThread(void *p_Param)
    {
        return ((CMMOServer *)p_Param)->AcceptThread_Update();
    }
    bool CMMOServer::AcceptThread_Update(void)
    {
        int SessionAddrLen;
        sockaddr_in SessionAddr;
        SOCKET SessionSock;
        //LONG64 SessionIndex;
        //HANDLE hResult;
        //DWORD flags = 0;
        st_CLIENT_CONNECT_INFO *p_ClientConnectInfo;
        int Ret;

        BOOL NoDelayOptVal;
        NoDelayOptVal = true;

        DWORD KeepAliveResult;
        tcp_keepalive TcpKL;
        TcpKL.onoff = 1;
        TcpKL.keepalivetime = 30000;
        TcpKL.keepaliveinterval = 2000;

        while (!_Shutdown)
        {
            //ProfileBegin(L"AccpetThread");

            // 1. WSAAccept 함수 호출로 Socket 연결 수립작업.
            SessionAddrLen = sizeof(SessionAddr);
            SessionSock = WSAAccept(_ListenSocket, (sockaddr *)&SessionAddr, &SessionAddrLen, NULL, NULL);
            if (INVALID_SOCKET == SessionSock)
                return 0;       // 종료처리 부분

            InterlockedIncrement(&_Monitor_AcceptSocket);

            // 소켓 받아온 다음 PC방 ip에 대한 과금처리 및 기타 ip처리(가상함수로 핸들러 처리할 것)

            // 2. 최대사용자 도달시 접속 거부에 대한 처리.

            // 3. Keepalive 작업

            // NoDelay
            if (false == _EnableNagle)
            {
                Ret = setsockopt(SessionSock, IPPROTO_TCP, TCP_NODELAY, (char *)&NoDelayOptVal, sizeof(BOOL));
                if (Ret != 0)
                {
                    CrashDump::Crash();
                    SocketClose(SessionSock);
                }
            }

            // Keep Alive
            Ret = WSAIoctl(SessionSock, SIO_KEEPALIVE_VALS, &TcpKL, sizeof(tcp_keepalive), 0, 0, &KeepAliveResult, NULL, NULL);
            if (Ret != 0)
            {
                CrashDump::Crash();
                SocketClose(SessionSock);
            }

            // 4. 접속이 완료된 Socket 을 AcceptSocketQueue 에 등록
            p_ClientConnectInfo = _MemoryPool_ConnectInfo.Alloc();
            if (nullptr == p_ClientConnectInfo)
            {
                CrashDump::Crash();
                SocketClose(SessionSock);
            }
            else
            {
                p_ClientConnectInfo->_Socket = SessionSock;
                wsprintf(p_ClientConnectInfo->_IP, L"%u.%u.%u.%u", SessionAddr.sin_addr.s_net, SessionAddr.sin_addr.s_host, SessionAddr.sin_addr.s_lh, SessionAddr.sin_addr.s_impno);
                p_ClientConnectInfo->_Port = SessionAddr.sin_port;

                if (false == _AcceptSocketQueue.Enqueue(p_ClientConnectInfo))
                {
                    CrashDump::Crash();
                    SocketClose(SessionSock);
                    if (false == _MemoryPool_ConnectInfo.Free(p_ClientConnectInfo))
                        CrashDump::Crash();
                }
                InterlockedIncrement(&_Monitor_Counter_Accept);
            }

            //ProfileEnd(L"AccpetThread");
        }

        return true;
    }

    unsigned __stdcall CMMOServer::AuthThread(void *p_Param)
    {
        return ((CMMOServer *)p_Param)->AuthThread_Update();
    }
    bool CMMOServer::AuthThread_Update(void)
    {
        while (!_ShutdownAuthThread)
        {
            //ProfileBegin(L"AuthThread");

            // 1. 신규 접속자 처리
            ProcAuth_Accept();

            // 2. Auth 모드 세션들 패킷 처리
            ProcAuth_Packet();

            // 3. Auth 모드의 Update 처리
            OnAuth_Update();

            // 4. Auth 모드의 Logout 처리
            ProcAuth_Logout();

            InterlockedIncrement(&_Monitor_Counter_AuthUpdate);

            //ProfileEnd(L"AuthThread");

            Sleep(AUTH_THREAD_DELAY);
        }

        return true;
    }

    unsigned __stdcall CMMOServer::GameUpdateThread(void *p_Param)
    {
        return ((CMMOServer *)p_Param)->GameUpdateThread_Update();
    }
    bool CMMOServer::GameUpdateThread_Update(void)
    {
        ULONGLONG HeartbeatTick;
        ULONGLONG CurTick;

        HeartbeatTick = 0;
        while (!_ShutdownGameThread)
        {
            //ProfileBegin(L"GameThread");

            ProfileBegin(L"Heartbeat");
            // 일단 임시로 하트비트를 여기에 넣는다.
            CurTick = GetTickCount64();
            if (HeartbeatTick + GAME_THREAD_HEARTBEAT_TICK < CurTick)
            {
                OnGame_HeartBeat();
                HeartbeatTick = CurTick;
            }
            ProfileEnd(L"Heartbeat");

            ProfileBegin(L"AuthToGame");
            // 1. 모든 세션을 돌면서 MODE_AUTH_TO_GAME > MODE_GAME 으로 전환
            ProcGame_AuthToGame();
            ProfileEnd(L"AuthToGame");

            ProfileBegin(L"Packet");
            // 2. 모든 세션을 돌면서 MODE_GAME 중인 세션에 대해 패킷 프로세싱
            ProcGame_Packet();
            ProfileEnd(L"Packet");

            //ProfileBegin(L"Update");
            // 3. OnGame_Update() 호출 컨텐츠 처리.
            // 4. LogoutFlag 세션을 MODE_LOGOUT_IN_GAME 모드로 전환.
            OnGame_Update();
            //ProfileEnd(L"Update");

            ProfileBegin(L"Logout");
            // 5. 모든 세션을 돌면서 MODE_LOGOUT_IN_GAME 모드의 세션을 종료처리 > MODE_WAIT_LOGOUT 전환
            ProcGame_Logout();
            ProfileEnd(L"Logout");

            ProfileBegin(L"Release");
            // 6. 모든 세션을 돌면서 MODE_WAIT_LOGOUT 모드의 실제 세션 릴리즈 처리
            ProcGame_Release();
            ProfileEnd(L"Release");

            //InterlockedIncrement(&_Monitor_Counter_GameUpdate);
            _Monitor_Counter_GameUpdate++;

            //ProfileEnd(L"GameThread");

            //Sleep(GAME_THREAD_DELAY);
            //Sleep(1);
            Sleep(GAME_THREAD_DELAY);
        }
        return true;
    }

    unsigned __stdcall CMMOServer::IOCPWorkerThread(void *p_Param)
    {
        return ((CMMOServer *)p_Param)->IOCPWorkerThread_Update();
    }
    bool CMMOServer::IOCPWorkerThread_Update(void)
    {
        BOOL Ret;
        DWORD Transferred;
        NetSession *p_Session;
        LPOVERLAPPED lpOverlapped;
        DWORD ErrorCode;

        while (!_Shutdown)
        {
            Transferred = 0;
            p_Session = nullptr;
            lpOverlapped = nullptr;

            Ret = GetQueuedCompletionStatus(_IOCP, &Transferred, (PULONG_PTR)&p_Session, &lpOverlapped, INFINITE);

            // 종료
            if (0 == Transferred && nullptr == p_Session && nullptr == lpOverlapped)
            {
                return 0;
            }

            if (1 == Transferred && 1 == (int)p_Session)
            {
                OnWorker_HeartBeat();
                continue;
            }

            if (FALSE == Ret)
            {
                ErrorCode = GetLastError();
                //if (ErrorCode != ERROR_NETNAME_DELETED && ErrorCode != ERROR_SEM_TIMEOUT)
                //    p_This->OnError(ErrorCode, L"GQCS Error");
                p_Session->Disconnect(true);
                //p_This->ClientShutdown(p_Session);
            }
            else if (0 == Transferred)
            {
                p_Session->Disconnect(true);
                //p_This->ClientShutdown(p_Session);
            }
            else
            {
                if (&p_Session->_RecvOL == lpOverlapped)
                {
                    p_Session->CompleteRecv(Transferred);
                }
                else if (&p_Session->_SendOL == lpOverlapped)
                {
                    p_Session->CompleteSend(Transferred);
                }
                else
                {
                    CrashDump::Crash();
                    //p_This->OnError(1, L"GQCS Error [Invalid Overlappped]");
                    //p_This->ClientShutdown(p_Session);
                    p_Session->Disconnect(true);
                }
            }

            if (0 == InterlockedDecrement64(&p_Session->_IOCount))
            {
                p_Session->_LogoutFlag = true;
                //p_This->ClientReleaseDebug(p_Session, IoType);
                //p_This->ClientReleaseDebug(p_Session, 4);
                //p_This->ClientRelease(p_Session);
            }
        }
        return true;
    }

    unsigned __stdcall CMMOServer::SendThread(void *p_Param)
    {
        return ((CMMOServer *)p_Param)->SendThread_Update();
    }
    bool CMMOServer::SendThread_Update(void)
    {
        int SessionIndex;

        ULONGLONG HeartbeatTick;
        ULONGLONG CurTick;

        HeartbeatTick = 0;
        while (!_ShutdownSendThread)  // service down?
        {
            //ProfileBegin(L"SendThread");

            for (SessionIndex = 0; SessionIndex < _MaxSession; ++SessionIndex)
            {
                _SessionArray[SessionIndex]->SendPost();
            }

            // 그 외 하트비트 처리가 여기 들어간다.
            // 워커 스레드 하트비트
            CurTick = GetTickCount64();
            if (HeartbeatTick + WORKER_HEARTBEAT_TICK < CurTick)
            {
                PostQueuedCompletionStatus(_IOCP, 1, (ULONG_PTR)1, NULL);
                HeartbeatTick = CurTick;
            }

            // 샌드 스레드 하트비트(현재는 하는 일 없음.)

            //ProfileEnd(L"SendThread");

            Sleep(SEND_THREAD_DELAY);
        }
        return true;
    }








    NetSession::NetSession(void)
    {
        SessionInit();
    }
    NetSession::~NetSession(void)
    {

    }

    void NetSession::SessionInit(void)
    {
        Packet *p_Packet;

        _SessionID = SESSION_ID_DEFAULT;

        _Mode = MODE_NONE;
        _ArrayIndex = -1;           // define 으로 뺄 것.

        _ClientInfo._Socket = INVALID_SOCKET;
        wmemset(_ClientInfo._IP, 0, IP_V4_MAX_LEN + 1);
        _ClientInfo._Port = 0;

        _RecvQ.Clear();
        while (true == _SendQ.Dequeue(&p_Packet))
        {
            p_Packet->Free();
        }
        while (true == _CompletePacketQ.Dequeue(&p_Packet))
        {
            p_Packet->Free();
        }
        
        _SendPacketCnt = 0;
        _SendPacketSize = 0;

        _SendIO = 0;
        _IOCount = 0;

        _LogoutFlag = false;
        _AuthToGameFlag = false;

        _DisconnectFlag = false;

        _LastRecvPacketTime = 0;
    }

    void NetSession::SetMode_Game(void)
    {
        _AuthToGameFlag = true;
    }
    bool NetSession::SendPacket(Packet *p_Packet)
    {
        if (nullptr == p_Packet)
        {
            CrashDump::Crash();
            return false;
        }
        //ProfileBegin(L"SendPacket1");
        p_Packet->Encode();
        //ProfileEnd(L"SendPacket1");

        //ProfileBegin(L"SendPacket2");
        p_Packet->addRef();
        //ProfileEnd(L"SendPacket2");

        //ProfileBegin(L"SendPacket3");
        if (false == _SendQ.Enqueue(p_Packet))
        {
            CrashDump::Crash();
            p_Packet->Free();
            return false;
        }
        //ProfileEnd(L"SendPacket3");

        return true;
    }
    void NetSession::Disconnect(bool Force)
    {
        if (true == Force)
        {
            // 링거 옵션을 걸어주는 용도는?
            linger Ling;
            Ling.l_onoff = 1;
            Ling.l_linger = 0;
            setsockopt(_ClientInfo._Socket, SOL_SOCKET, SO_LINGER, (char *)&Ling, sizeof(LINGER));

            // cancelex 걸어줄 것인가?
        }
        shutdown(_ClientInfo._Socket, SD_BOTH);
    }

    void NetSession::CompleteRecv(DWORD Transferred)
    {
        _RecvQ.MoveWritePtr(Transferred);

        int RecvQSize;
        st_PACKET_HEADER Header;
        Packet *p_Packet;

        while (_RecvQ.GetUseSize() > 0)
        {
            //--------------------------------
            // Header Check
            //--------------------------------
            RecvQSize = _RecvQ.GetUseSize();
            if (RecvQSize < sizeof(st_PACKET_HEADER))
                break;
                //return Packet_NotComplete;

            // code & size check
            _RecvQ.Peek((char *)&Header, sizeof(st_PACKET_HEADER));
            if (Header.Code != Packet::_PacketCode)
            {
                Disconnect(true);
                return;
                //return Packet_Error;
            }   
            if (Header.Len > Packet::BUFFER_SIZE_DEFAULT - Packet::HEADER_SIZE_MAX)
            {
                Disconnect(true);
                return;
                //return Packet_Error;
            }   

            if (RecvQSize < Header.Len + sizeof(st_PACKET_HEADER))
                break;
                //return Packet_NotComplete;

            // remove header
            if (false == _RecvQ.RemoveData(sizeof(st_PACKET_HEADER)))
            {
                Disconnect(true);
                return;
                //return Packet_Error;
            }

            //--------------------------------
            // Payload Check
            //--------------------------------
            p_Packet = Packet::Alloc();
            if (nullptr == p_Packet)
            {
                CrashDump::Crash();
                Disconnect(true);
                return;
                //return Packet_Error;
            }

            p_Packet->SetHeader((char *)&Header);
            if (Header.Len != _RecvQ.Get((char *)p_Packet->GetWriteBufferPtr(), Header.Len))
            {
                p_Packet->Free();
                Disconnect(true);
                return;
                //return Packet_Error;
            }
            if (false == p_Packet->MoveWritePos(Header.Len))
            {
                p_Packet->Free();
                Disconnect(true);
                return;
                //return Packet_Error;
            }

            if (false == p_Packet->Decode())
            {
                p_Packet->Free();
                Disconnect(true);
                return;
                //return Packet_Error;
            }

            try
            {
                p_Packet->addRef();
                if (false == _CompletePacketQ.Enqueue(p_Packet))
                {
                    CrashDump::Crash();
                    p_Packet->Free();
                    p_Packet->Free();
                    Disconnect(true);
                    return;
                    //return Packet_Error;
                }
            }
            catch (Packet::Exception_PacketOut ExOut)
            {
                SYSLOG(L"MMOServer", LOG_ERROR, L"CompletePacket # Exception_PacketOut [Addr: %s:%u][OutSize:%d]", _ClientInfo._IP, _ClientInfo._Port, ExOut._RequestOutSize);
                p_Packet->Free();
                p_Packet->Free();
                Disconnect(true);
                return;
                //return Packet_Error;
            }

            p_Packet->Free();

            //return Packet_Complete;
        }

        RecvPost();
    }
    void NetSession::CompleteSend(DWORD Transferred)
    {
        if (Transferred != _SendPacketSize)
        {
            // 나와서는 안되는 상황. 죽어랏!
            CrashDump::Crash();
            Disconnect(true);
            return;
        }

        int Cnt;
        Packet *p_Packet;
        for (Cnt = 0; Cnt < _SendPacketCnt; ++Cnt)
        {
            if (false == _SendQ.Dequeue(&p_Packet))
            {
                // 나와서는 안되는 상황. 죽어랏!
                CrashDump::Crash();
                return;
            }
            p_Packet->Free();
        }
        //InterlockedAdd(&_Monitor_SendPacketCounter, p_Session->SendPacketCnt);
        //OnSend(p_Session->SessionID, cbTransferred);
        InterlockedExchange(&_SendIO, 0);

        // SendPost는 이제 SendThread에서 호출한다.
        //SendPost();
        //if (_SendQ.GetUseSize() > 0)
        //    SendPost();
    }

    bool NetSession::RecvPost(void)
    {
        int BufCount;
        WSABUF RecvWsaBuf[2];
        DWORD flags = 0;
        int Ret;

        BufCount = 1;
        RecvWsaBuf[0].buf = _RecvQ.GetWriteBufferPtr();
        RecvWsaBuf[0].len = _RecvQ.GetNotBrokenPutSize();

        if (_RecvQ.GetNotBrokenPutSize() < _RecvQ.GetFreeSize())
        {
            RecvWsaBuf[1].buf = _RecvQ.GetBufferPtr();
            RecvWsaBuf[1].len = _RecvQ.GetFreeSize() - _RecvQ.GetNotBrokenPutSize();
            BufCount++;
        }

        memset(&_RecvOL, 0, sizeof(OVERLAPPED));
        InterlockedIncrement64(&_IOCount);
        Ret = WSARecv(_ClientInfo._Socket, (LPWSABUF)&RecvWsaBuf, BufCount, NULL, &flags, &_RecvOL, NULL);
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            if (Ret != ERROR_IO_PENDING)
            {
                //OnError(Ret, L"WSA Recv Error");
                Disconnect(true);
                //ClientShutdown(p_Session);
                if (0 == InterlockedDecrement64(&_IOCount))
                {
                    _LogoutFlag = true;
                    //ClientReleaseDebug(p_Session, 1);
                    //ClientRelease(p_Session);
                }
                return false;
            }
        }

        return true;
    }

    bool NetSession::SendPost(void)
    {
    //RETRY:
        if (InterlockedCompareExchange(&_SendIO, 1, 0) != 0)        // 0이라면 1로 바꾼다.
            return false;

        // 세션 모드 확인
        if (_Mode != MODE_AUTH && _Mode != MODE_GAME)
        {
            //InterlockedExchange(&_SendIO, 0);
            InterlockedDecrement(&_SendIO);
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
        if (_SendQ.GetUseSize() > 0)
        {
            for (Cnt = 0; Cnt < SEND_WSA_BUF_MAX; ++Cnt)
            {
                if (false == _SendQ.Peek(&p_Packet, Cnt))
                    break;

                //SendWsaBuf[Cnt].buf = p_Packet->GetHeaderBufferPtr_CustomHeader(sizeof(st_NetHeader));
                //SendWsaBuf[Cnt].len = p_Packet->GetPacketSize_CustomHeader(sizeof(st_NetHeader));
                SendWsaBuf[Cnt].buf = p_Packet->GetHeaderBufferPtr();
                SendWsaBuf[Cnt].len = p_Packet->GetPacketSize();
                BufCount++;
                BufSize += p_Packet->GetPacketSize();
            }
        }
        
        if (0 == BufCount)
        {
            //InterlockedExchange(&_SendIO, 0);
            InterlockedDecrement(&_SendIO);

            if (true == _DisconnectFlag)
                Disconnect(true);

            return false;
        }

        _SendPacketCnt = BufCount;
        _SendPacketSize = BufSize;

        memset(&_SendOL, 0, sizeof(OVERLAPPED));
        InterlockedIncrement64(&_IOCount);
        Ret = WSASend(_ClientInfo._Socket, (LPWSABUF)&SendWsaBuf, BufCount, NULL, 0, &_SendOL, NULL);
        if (SOCKET_ERROR == Ret)
        {
            Ret = WSAGetLastError();
            if (Ret != ERROR_IO_PENDING)
            {
                Disconnect(true);
                if (0 == InterlockedDecrement64(&_IOCount))
                {
                    _LogoutFlag = true;
                }
                //InterlockedExchange(&_SendIO, 0);
                InterlockedDecrement(&_SendIO);

                return false;
            }
        }

        CMMOServer::_Monitor_Counter_PacketSend += BufCount;

        return true;
    }

}