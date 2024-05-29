#include "LibHeader.h"

namespace MonLib
{
    CNetClient::CNetClient(void)
    {
        int Cnt;

        //_Connect = false;
        _ConnectTry = 0;

        _Sock = INVALID_SOCKET;
        //memset(&_RecvOL, 0, sizeof(OVERLAPPED));
        //memset(&_SendOL, 0, sizeof(OVERLAPPED));

        _p_IOCompare = (st_IO_COMPARE *)_aligned_malloc(sizeof(st_IO_COMPARE), 16);
        _p_IOCompare->IOCount = 0;
        _p_IOCompare->ReleaseFlag = 1;

        _SendIO = 0;
        _SendPacketCnt = 0;
        _SendPacketSize = 0;

        _hIocp = INVALID_HANDLE_VALUE;
        _WorkerThreadMax = 0;
        for (Cnt = 0; Cnt < WORKER_THREAD_MAX_COUNT; ++Cnt)
        {
            _WorkerThread[Cnt] = INVALID_HANDLE_VALUE;
            _WorkerThreadId[Cnt] = 0;
        }

        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            exit(0);
        }

        // Iocp 생성
        _hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, _WorkerThreadMax);
        if (NULL == _hIocp)
        {
            CrashDump::Crash();
            //exit(0);
            //OnError(1, L"[IOCP Creation Error]");
        }

        // 워커스레드 생성
        for (Cnt = 0; Cnt < WORKER_THREAD_MAX_COUNT; ++Cnt)
        {
            _WorkerThread[Cnt] = (HANDLE)_beginthreadex(NULL, 0, WorkerThreadProc, this, 0, &_WorkerThreadId[Cnt]);
            if (INVALID_HANDLE_VALUE == _WorkerThread[Cnt])
            {
                CrashDump::Crash();
                //OnError(1, L"[WorkerThread Creation Error]");
                //return false;
            }
        }
    }
    CNetClient::~CNetClient(void)
    {
        //int Cnt;
        ////DWORD ExitCode;
        //
        //// thread exit
        //for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        //{
        //    PostQueuedCompletionStatus(_hIocp, NULL, NULL, NULL);
        //}
        //WaitForMultipleObjects(_WorkerThreadMax, _WorkerThread, TRUE, INFINITE);
        //
        ////for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        ////{
        ////    GetExitCodeThread(_WorkerThread[Cnt], &ExitCode);
        ////    if (0 == ExitCode)
        ////        OnError(1, L"Worker Thread Exit");
        ////    else
        ////        OnError(1, L"Worker Thread Exit Error");
        ////}
        //
        //// thread init
        //_hIocp = INVALID_HANDLE_VALUE;
        //
        //_WorkerThreadMax = 0;
        //for (Cnt = 0; Cnt < WORKER_THREAD_MAX_COUNT; ++Cnt)
        //{
        //    _WorkerThread[Cnt] = INVALID_HANDLE_VALUE;
        //    _WorkerThreadId[Cnt] = 0;
        //}
        //
        //_aligned_free(_p_IOCompare);
        ////_p_IOCompare = nullptr;
        //WSACleanup();

        // worker thread
        int Cnt;
        for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        {
            PostQueuedCompletionStatus(_hIocp, 0, NULL, NULL);
        }
        WaitForMultipleObjects(_WorkerThreadMax, _WorkerThread, TRUE, INFINITE);

        // worker thread init
        _WorkerThreadMax = 0;
        for (Cnt = 0; Cnt < WORKER_THREAD_MAX_COUNT; ++Cnt)
        {
            CloseHandle(_WorkerThread[Cnt]);
            _WorkerThread[Cnt] = INVALID_HANDLE_VALUE;
            _WorkerThreadId[Cnt] = 0;
        }

        // iocp init
        CloseHandle(_hIocp);
        _hIocp = INVALID_HANDLE_VALUE;

        _aligned_free(_p_IOCompare);
        WSACleanup();
    }

    bool CNetClient::Connect(WCHAR *p_IP, USHORT Port, int WorkerThreadCnt, BYTE PacketCode, BYTE XORCode1, BYTE XORCode2, bool Nagle)
    {
        // Connect 할 때 동기화가 걸리지 않는다. 이는 하트비트가 싱글스레드이고 여기에서만 호출하기 때문이다.

        //if (true == _Connect)
        //    return false;
        //_Connect = true;
        
        if (InterlockedIncrement(&_ConnectTry) != 1)
        {
            InterlockedDecrement(&_ConnectTry);
            return false;
        }

        if (0 == _p_IOCompare->ReleaseFlag)
        {
            InterlockedDecrement(&_ConnectTry);
            return false;
        }

        if (nullptr == p_IP)
        {
            //SYSLOG(L"SYSTEM", LOG_SYSTEM, L"IP Address Invalid");
            CrashDump::Crash();
            InterlockedDecrement(&_ConnectTry);
            return false;
        }

        SOCKADDR_IN ServerAddr;
        int Ret;
        HANDLE hResult;

        //// worker thread
        //for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        //{
        //    PostQueuedCompletionStatus(_hIocp, 0, NULL, NULL);
        //}
        //WaitForMultipleObjects(_WorkerThreadMax, _WorkerThread, TRUE, INFINITE);
        //
        //// worker thread init
        //_WorkerThreadMax = 0;
        //for (Cnt = 0; Cnt < WORKER_THREAD_MAX_COUNT; ++Cnt)
        //{
        //    CloseHandle(_WorkerThread[Cnt]);
        //    _WorkerThread[Cnt] = INVALID_HANDLE_VALUE;
        //    _WorkerThreadId[Cnt] = 0;
        //}
        //
        //// iocp init
        //CloseHandle(_hIocp);
        //_hIocp = INVALID_HANDLE_VALUE;
        //
        //// Iocp 생성
        //_hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, _WorkerThreadMax);
        //if (NULL == _hIocp)
        //{
        //    CrashDump::Crash();
        //    //exit(0);
        //    //OnError(1, L"[IOCP Creation Error]");
        //}

        //// ref count 초기화
        //_p_IOCompare->IOCount = 0;
        //_p_IOCompare->ReleaseFlag = 0;

        _SendPacketCnt = 0;
        _SendPacketSize = 0;
        //_SendIO = 0;
        if (_SendIO != 0)
            CrashDump::Crash();

        if (_RecvQ.GetUseSize() != 0)
            CrashDump::Crash();
        if (_SendQ.GetUseSize() != 0)
            CrashDump::Crash();

        // 프로토콜 및 암호화 패킷 변수 세팅
        Packet::_PacketCode = PacketCode;
        Packet::_XORCode1 = XORCode1;
        Packet::_XORCode2 = XORCode2;

        //// determine worker thread count
        //_WorkerThreadMax = WORKER_THREAD_MAX_COUNT;
        //if (WorkerThreadCnt > 0 || WorkerThreadCnt < WORKER_THREAD_MAX_COUNT)
        //    _WorkerThreadMax = WorkerThreadCnt;
        //
        //// 워커스레드 생성
        //for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        //{
        //    _WorkerThread[Cnt] = (HANDLE)_beginthreadex(NULL, 0, WorkerThreadProc, this, 0, &_WorkerThreadId[Cnt]);
        //    if (INVALID_HANDLE_VALUE == _WorkerThread[Cnt])
        //    {
        //        CrashDump::Crash();
        //        //OnError(1, L"[WorkerThread Creation Error]");
        //        return false;
        //    }
        //}

        //------------------------------------------------------
        // Network Init
        //------------------------------------------------------

        // socket 생성
        _Sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (INVALID_SOCKET == _Sock)
        {
            //SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Socket Creation Error");
            //OnError(1, L"[Socket Creation Error]");
            CrashDump::Crash();
            InterlockedDecrement(&_ConnectTry);
            return false;
        }

        // NoDelay
        //int Ret;
        BOOL OptVal = true;
        if (false == Nagle)
        {
            Ret = setsockopt(_Sock, IPPROTO_TCP, TCP_NODELAY, (char*)&OptVal, sizeof(BOOL));
            if (Ret != 0)
            {
                //SYSLOG(L"SYSTEM", LOG_SYSTEM, L"NoDelay Error");
                //OnError(1, L"[NoDelay Error]");
                CrashDump::Crash();
                InterlockedDecrement(&_ConnectTry);
                closesocket(_Sock);
                return false;
            }
        }

        // keepalive
        tcp_keepalive tcpkl;
        DWORD Result;

        tcpkl.onoff = 1;
        tcpkl.keepalivetime = 30000;        // 30초마다 keepalive 신호를 보내겠다.(윈도우 기본은 2시간)
        tcpkl.keepaliveinterval = 2000;     // keepalive 신호를 보내고 응답이 없으면 2초마다 재전송. (ms tcp는 10회 재시도 한다.)

        WSAIoctl(_Sock, SIO_KEEPALIVE_VALS, &tcpkl, sizeof(tcp_keepalive), 0, 0, &Result, NULL, NULL);
        if (Ret != 0)
        {
            //SYSLOG(L"SYSTEM", LOG_SYSTEM, L"KeepAlive Error");
            CrashDump::Crash();
            InterlockedDecrement(&_ConnectTry);
            closesocket(_Sock);
            return false;
        }

        // connect에서 블락 걸릴 때 -> 아예 포트가 안열렸거나 tcp stack에서 조차 응답이 없는 경우다.(만약 엑셉트를 못받으면 fail이 떠버려요.)

        // 바인딩(이거는 자신의 IP 넣어야함.)
        //memset(&ServerAddr, 0, sizeof(ServerAddr));
        ServerAddr.sin_family = AF_INET;
        ServerAddr.sin_port = htons(Port);
        InetPton(AF_INET, p_IP, &(ServerAddr.sin_addr));
        //Ret = bind(_Sock, (sockaddr *)&ServerAddr, sizeof(ServerAddr));
        //if (SOCKET_ERROR == Ret)
        //{
        //    OnError(1, L"[Binding Error]");
        //    closesocket(_Sock);
        //    return false;
        //}

        // iocp 등록
        hResult = CreateIoCompletionPort((HANDLE)_Sock, _hIocp, (ULONG_PTR)this, 0);
        if (NULL == hResult)
        {
            CrashDump::Crash();
            closesocket(_Sock);
            InterlockedDecrement(&_ConnectTry);
            //SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Sock IOCP Error");
            //OnError(1, L"Sock IOCP Error");
            return false;
        }

        // 커넥트
        if (SOCKET_ERROR == WSAConnect(_Sock, (sockaddr *)&ServerAddr, sizeof(ServerAddr), NULL, NULL, NULL, NULL))
        {
            //CrashDump::Crash();
            //SYSLOG(L"SYSTEM", LOG_SYSTEM, L"Connect Error");
            //OnError(1, L"[Connect Error]");
            closesocket(_Sock);
            InterlockedDecrement(&_ConnectTry);
            return false;
        }

        if (InterlockedIncrement64(&_p_IOCompare->IOCount) != 1)
            CrashDump::Crash();
        if (InterlockedCompareExchange64(&_p_IOCompare->ReleaseFlag, 0, 1) != 1)
            CrashDump::Crash();

        //InterlockedIncrement64(&_p_IOCompare->IOCount);
        //InterlockedCompareExchange64(&_p_IOCompare->ReleaseFlag, 0, 1);
        OnEnterJoinServer();
        RecvPost();
        if (0 == InterlockedDecrement64(&_p_IOCompare->IOCount))
        {
            ClientRelease();
            InterlockedDecrement(&_ConnectTry);
            return false;
        }

        InterlockedDecrement(&_ConnectTry);
        return true;
    }
    void CNetClient::Disconnect(void)
    {
        // 사용자가 밖에서 호출하는 함수이지만 내부에서도 쓴다.

        /*
        // ClientDisconnect()
        bsoocketshutdown = false;
        shutdown(send);     // 원래는 shutdown를 걸고 리시브를 받아야 한다.(우리는 이미 걸려있기 때문에 생략)

        // 아래는 선생님이 테스트중.(오버랩 중복이 되면 먹통이 되는걸 해결하기 위함.)
        //shutdown(socket, SD_BOTH);                   // shutdown(both)는 closesocket과 동일하나 소켓 반환 안함.
        //CancelIoEx(socket, overlapped 구조체)        // io작업 걸린걸 강제로 취소.(send는 완료 뜰꺼에요. 거의 실시간으로.)(그래서 recv만 취소함.)

        // 우아한 종료
        1. 서버 shutdown(X, SD_SEND) 호출
        2. 서버 recv 호출
        3. 클라이언트 EOF(더이상 보낼 것이 없음.) -> recv 0 받음.
        4. 클라이언트는 closesocket 호출
        5. 서버 recv의 결과로 0 리턴

        // 우아한 종료 안될 경우 -> linger 옵션을 조절하여 closesocket으로 강제 종료.(timeout 남지 않도록)

        // bool SocketClose()       // linger 옵션 세팅해서 강제종료
        Linger ling;

        ling.l_onoff = 1;
        ling.l_linger = 0;

        // 클라이언트를 디스커넥트(shutdown)와 릴리즈(closesocket)로 나눌것 -> 필수!

        // clientshutdown - shutdown으로 안전한 종료 유도
        // clientdisconnect - socket 접속 강제 끊기
        // socketclose - closesocket(내부용)
        // clientrelease - client session 해제

        */

        //int Cnt;
        //DWORD ExitCode;
        //
        //// close socket
        //closesocket(_Sock);
        //_Sock = INVALID_SOCKET;
        //
        //// thread exit
        //for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        //{
        //    PostQueuedCompletionStatus(_hIocp, NULL, NULL, NULL);
        //}
        //WaitForMultipleObjects(_WorkerThreadMax, _WorkerThread, TRUE, INFINITE);
        //
        //for (Cnt = 0; Cnt < _WorkerThreadMax; ++Cnt)
        //{
        //    GetExitCodeThread(_WorkerThread[Cnt], &ExitCode);
        //    if (0 == ExitCode)
        //        OnError(1, L"Worker Thread Exit");
        //    else
        //        OnError(1, L"Worker Thread Exit Error");
        //}
        //
        //// thread init
        //_hIocp = INVALID_HANDLE_VALUE;
        //
        //_WorkerThreadMax = 0;
        //for (Cnt = 0; Cnt < WORKER_THREAD_MAX_COUNT; ++Cnt)
        //{
        //    _WorkerThread[Cnt] = INVALID_HANDLE_VALUE;
        //    _WorkerThreadId[Cnt] = 0;
        //}

        //ClientShutdown();
        ClientDisconnect();

        //int Cnt;
        //int ShutdownCnt = 100;
        //for (Cnt = 0; Cnt < ShutdownCnt; ++Cnt)
        //{
        //
        //}
        //
        //ClientRelease();
    }
    bool CNetClient::SendPacket(Packet *p_Packet)
    {
        //if (false == _Connect)
        //    return false;
        if (1 == _p_IOCompare->ReleaseFlag)
            return false;

        bool Ret;
        //st_LAN_HEADER Header;

        if (InterlockedIncrement64(&_p_IOCompare->IOCount) == 1)
        {
            if (InterlockedDecrement64(&_p_IOCompare->IOCount) == 0)
                ClientRelease();
            return false;
        }

        //// 헤더 설정
        //Header.PayloadSize = p_Packet->GetDataSize();
        ////p_Packet->SetHeader_CustomHeader((char *)&Header, sizeof(st_LAN_HEADER));
        //p_Packet->SetHeader_CustomHeader_Short(Header.PayloadSize);

        // 암호화
        p_Packet->Encode();

        // 패킷을 인큐한다.
        p_Packet->addRef();
        if (false == _SendQ.Enqueue(p_Packet))
        {
            CrashDump::Crash();
            return false;
        }
        Ret = SendPost();

        if (InterlockedDecrement64(&_p_IOCompare->IOCount) == 0)
        {
            ClientRelease();
            return false;
        }

        return Ret;
    }
    //bool CNetClient::GetClientConnect(void)
    //{
    //    if (1 == _p_IOCompare->ReleaseFlag)
    //        return false;
    //    else
    //        return true;
    //}
    int CNetClient::GetRecvQSize(void)
    {
        return _RecvQ.GetUseSize();
    }
    bool CNetClient::IsConnect(void)
    {
        if (1 == _p_IOCompare->ReleaseFlag)
            return false;
        return true;
    }

    int CNetClient::CompletePacket(void)
    {
        Packet *p_Packet;
        int RecvQSize;
        st_PACKET_HEADER Header;

        // header size check
        RecvQSize = _RecvQ.GetUseSize();
        if (RecvQSize < sizeof(st_PACKET_HEADER))
            return Packet_NotComplete;

        // payload size check
        _RecvQ.Peek((char *)&Header, sizeof(st_PACKET_HEADER));
        if (Header.Code != Packet::_PacketCode)
            return Packet_Error;
        if (Header.Len > Packet::BUFFER_SIZE_DEFAULT - Packet::HEADER_SIZE_MAX)
            return Packet_Error;

        if (Header.Len + sizeof(st_PACKET_HEADER) > RecvQSize)
            return Packet_NotComplete;
        if (false == _RecvQ.RemoveData(sizeof(st_PACKET_HEADER)))
            return Packet_Error;
        
        // get payload
        p_Packet = Packet::Alloc();
        if (nullptr == p_Packet)
        {
            CrashDump::Crash();
            return Packet_Error;
        }

        p_Packet->SetHeader((char *)&Header);
        if (Header.Len != _RecvQ.Get((char *)p_Packet->GetWriteBufferPtr(), Header.Len))
        {
            p_Packet->Free();
            return Packet_Error;
        }
        if (false == p_Packet->MoveWritePos(Header.Len))
        {
            p_Packet->Free();
            return Packet_Error;
        }

        if (false == p_Packet->Decode())
        {
            //CrashDump::Crash();
            SYSLOG(L"NetClient", LOG_ERROR, L"Packet Decode");
            p_Packet->Free();
            return Packet_Error;
        }

        //InterlockedIncrement(&g_Monitor_RecvPacketCounter);

        // packet proc
        try
        {
            OnRecv(p_Packet);
        }
        catch (Packet::Exception_PacketOut err)     // 여기서는 딱 내것만 캐치하도록 할 것.
        {
            // 로그 찍어준다.
            //SYSLOG(L"LanServer", LOG_ERROR, L"OnRecv");
            p_Packet->Free();
            return Packet_Error;
        }
    
        p_Packet->Free();

        return Packet_Complete;
    }
    bool CNetClient::RecvPost(void)
    {
        int BufCount;
        WSABUF RecvWsaBuf[2];
        DWORD flags;
        int Ret;

        flags = 0;

        BufCount = 1;
        RecvWsaBuf[0].buf = _RecvQ.GetWriteBufferPtr();
        RecvWsaBuf[0].len = _RecvQ.GetNotBrokenPutSize();

        // 리시브 큐가 꽉찼을 때.
        if (nullptr == RecvWsaBuf[0].buf || RecvWsaBuf[0].len <= 0)
            CrashDump::Crash();

        if (_RecvQ.GetNotBrokenPutSize() < _RecvQ.GetFreeSize())
        {
            RecvWsaBuf[1].buf = _RecvQ.GetBufferPtr();
            RecvWsaBuf[1].len = _RecvQ.GetFreeSize() - _RecvQ.GetNotBrokenPutSize();
            BufCount++;
        }

        memset(&_RecvOL, 0, sizeof(OVERLAPPED));
        InterlockedIncrement64(&_p_IOCompare->IOCount);
        Ret = WSARecv(_Sock, (LPWSABUF)&RecvWsaBuf, BufCount, NULL, &flags, &_RecvOL, NULL);
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

                //OnError(1, L"WSA Recv Error");
                ClientShutdown();
                if (0 == InterlockedDecrement64(&_p_IOCompare->IOCount))
                {
                    ClientRelease();
                }
                return false;
            }
        }

        return true;
    }
    bool CNetClient::SendPost(void)
    {
    RETRY:
        if (InterlockedCompareExchange(&_SendIO, 1, 0) != 0)        // 0이라면 1로 바꾼다.
            return false;

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

                //SendWsaBuf[Cnt].buf = p_Packet->GetHeaderBufferPtr_CustomHeader(sizeof(st_LAN_HEADER));
                //SendWsaBuf[Cnt].len = p_Packet->GetPacketSize_CustomHeader(sizeof(st_LAN_HEADER));
                //BufCount++;
                //BufSize += p_Packet->GetPacketSize_CustomHeader(sizeof(st_LAN_HEADER));
                SendWsaBuf[Cnt].buf = p_Packet->GetHeaderBufferPtr();
                SendWsaBuf[Cnt].len = p_Packet->GetPacketSize();
                BufCount++;
                BufSize += p_Packet->GetPacketSize();
            }
        }

        if (0 == BufCount)
        {
            InterlockedExchange(&_SendIO, 0);

            if (_SendQ.GetUseSize() > 0)
                goto RETRY;

            return false;
        }

        _SendPacketCnt = BufCount;
        _SendPacketSize = BufSize;

        memset(&_SendOL, 0, sizeof(OVERLAPPED));
        InterlockedIncrement64(&_p_IOCompare->IOCount);
        Ret = WSASend(_Sock, (LPWSABUF)&SendWsaBuf, BufCount, NULL, 0, &_SendOL, NULL);
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

                //OnError(1, L"WSA Send Error");

                //// send io 0으로 초기화
                //InterlockedExchange(&_SendIO, 0);

                // 이미 끊긴 애 다시 디스커넥트(소켓 연결만 끊고 메모리는 그대로) 할 것임. -> 아름다운 종료 그딴건 없다.
                ClientShutdown();

                // send io 0으로 초기화
                InterlockedExchange(&_SendIO, 0);

                // IOCount decrement -> 0이면 릴리즈(이 세션 등 메모리 해제)
                if (0 == InterlockedDecrement64(&_p_IOCompare->IOCount))
                {
                    ClientRelease();
                }
                return false;
            }
        }

        return true;
    }
    void CNetClient::CompleteRecv(DWORD cbTransferred)
    {
        if (0 == _RecvQ.MoveWritePtr(cbTransferred))
            CrashDump::Crash();

        // 패킷 처리
        int CompletePacketResult;
        while (1)
        {
            CompletePacketResult = CompletePacket();
            if (Packet_Complete == CompletePacketResult)
                continue;
            else if (Packet_NotComplete == CompletePacketResult)
                break;
            else
            {
                //OnError(1, L"[Invalid CompletePacketResult]");
                ClientDisconnect();
                return;
            }
        }

        RecvPost();
    }
    void CNetClient::CompleteSend(DWORD cbTransferred)
    {
        if (cbTransferred != _SendPacketSize)
        {
            // 이 경우는 끊어야 하지만 일단 죽여보자.
            CrashDump::Crash();
            ClientShutdown();
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

        //InterlockedAdd(&g_Monitor_SendPacketCounter, _SendPacketCnt);
        OnSend(cbTransferred);
        InterlockedExchange(&_SendIO, 0);

        SendPost();
        //if (_SendQ.GetUseSize() > 0)
        //    SendPost();
    }
    void CNetClient::ClientShutdown(void)              // shutdown으로 안전한 종료 유도
    {
        shutdown(_Sock, SD_SEND);
    }
    void CNetClient::ClientDisconnect(void)            // socket 접속 강제 끊기
    {
        shutdown(_Sock, SD_BOTH);     // shutdown both
    }
    void CNetClient::SocketClose(void)                    // closesocket(내부용)
    {
        linger Ling;
        Ling.l_onoff = 1;
        Ling.l_linger = 0;
        setsockopt(_Sock, SOL_SOCKET, SO_LINGER, (char *)&Ling, sizeof(LINGER));
        closesocket(_Sock);
    }
    void CNetClient::ClientRelease(void)               // client session 해제
    {
        st_IO_COMPARE ReleaseCompare;
        ReleaseCompare.IOCount = 0;
        ReleaseCompare.ReleaseFlag = 0;
        if (InterlockedCompareExchange128((LONG64 *)_p_IOCompare, 1, 0, (LONG64 *)&ReleaseCompare) != 1)
            return;

        OnLeaveServer();

        SocketClose();

        Packet *p_Packet;

        // Send Queue Clear
        while (1)
        {
            if (false == _SendQ.Dequeue(&p_Packet))
                break;
            p_Packet->Free();
        }

        // Recv Queue Clear
        _RecvQ.Clear();

        // Release 마무리
        _Sock = INVALID_SOCKET;
        //_Connect = false;
    }

    unsigned __stdcall CNetClient::WorkerThreadProc(LPVOID lpParam)
    {
        CNetClient *p_This;
        BOOL Ret;
        DWORD cbTransferred;
        void *p_CompletionKey;
        LPOVERLAPPED lpOverlapped;
        DWORD ErrorCode;

        p_This = (CNetClient *)lpParam;
        while (1)
        {
            cbTransferred = 0;
            lpOverlapped = nullptr;
            p_CompletionKey = nullptr;

            Ret = GetQueuedCompletionStatus(p_This->_hIocp, &cbTransferred, (PULONG_PTR)&p_CompletionKey, &lpOverlapped, INFINITE);
        
            // 종료
            if (0 == cbTransferred && nullptr == p_CompletionKey && nullptr == lpOverlapped)
            {
                return 0;
            }
        
            p_This->OnWorkerThreadBegin();

            //if (FALSE == Ret && nullptr == p_CompletionKey)
            //{
            //    p_This->OnError(1, L"GQCS Error [Invalid Ret]");
            //    wprintf(L"GetLastError : %u\n", GetLastError());
            //    return -1;
            //}
        
            if (FALSE == Ret)
            {
                ErrorCode = GetLastError();
                if (ErrorCode != ERROR_NETNAME_DELETED && ErrorCode != ERROR_SEM_TIMEOUT)
                {
                    //SYSLOG(L"GQCS", LOG_ERROR, L"[%d]%s", ErrorCode, L"GQCS Error");
                    //p_This->OnError(ErrorCode, L"GQCS Error");
                }
                p_This->ClientShutdown();
            }
            else if (0 == cbTransferred)
            {
                p_This->ClientShutdown();
            }
            else
            {
                if (&p_This->_RecvOL == lpOverlapped)
                {
                    p_This->CompleteRecv(cbTransferred);
                }
                else if (&p_This->_SendOL == lpOverlapped)
                {
                    p_This->CompleteSend(cbTransferred);
                }
                else
                {
                    CrashDump::Crash();
                    //SYSLOG(L"GQCS", LOG_ERROR, L"[%d]%s", 0, L"Invalid Overlappped");
                    //p_This->OnError(1, L"GQCS Error [Invalid Overlappped]");
                    p_This->ClientShutdown();
                }
            }

            if (0 == InterlockedDecrement64(&p_This->_p_IOCompare->IOCount))
            {
                p_This->ClientRelease();
            }
            p_This->OnWorkerThreadEnd();
        }

        return 0;
    }
}