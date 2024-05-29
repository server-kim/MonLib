#ifndef __UDP_MODULE_HEADER__
#define __UDP_MODULE_HEADER__

// 아래 모듈은 서버간 일대다 통신에 사용하기 위함이다.
// 서버로 사용하기 위해서는 보완이 필요하다.

namespace MonLib
{
    class CUDPModule
    {
    private:
        enum UDP_DEFINE
        {
            RECV_BUF_SIZE = 1024
        };

        class Session
        {
        public:
            Session(void)
            {
                _Use = false;
            }
            virtual ~Session(void)
            {

            }

            void SessionInit(SOCKADDR_IN SessionAddr)
            {
                _Use = true;
                _Addr = SessionAddr;
                _AddrLen = sizeof(_Addr);
            }
            void SessionRelease(void)
            {
                _Use = false;
            }
            bool IsUse(void)
            {
                return _Use;
            }
            bool CheckSession(SOCKADDR_IN SessionAddr)
            {
                if (false == _Use)
                    return false;
                if (SessionAddr.sin_addr.S_un.S_addr == _Addr.sin_addr.S_un.S_addr &&
                    SessionAddr.sin_port == _Addr.sin_port)
                    return true;
                return false;
            }
            bool PacketProc(char *RecvBuf, int RecvSize, CUDPModule *p_UDPModule)
            {
                if (RecvSize != 2)
                    CrashDump::Crash();
                
                WORD RecvType = *((WORD *)RecvBuf);
                if (1001 == RecvType)
                    p_UDPModule->_ShutdownFlag = true;
                //printf_s("%s\n", RecvBuf);
                return true;
            }

        private:
            bool            _Use;
            SOCKADDR_IN     _Addr;
            int             _AddrLen;
        };

    public:
        CUDPModule(WCHAR *p_BindIP, USHORT BindPort, int SessionMax)
        {
            if (nullptr == p_BindIP)
                CrashDump::Crash();

            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
                CrashDump::Crash();

            _Run = false;

            _Sock = INVALID_SOCKET;
            wcscpy_s(_BindIP, IP_V4_MAX_LEN + 1, p_BindIP);
            _BindPort = BindPort;

            _SessionMax = SessionMax;
            _Session = new Session[SessionMax];

            _NetworkThread = INVALID_HANDLE_VALUE;
            _NetworkThreadId = 0;
        }
        virtual ~CUDPModule(void)
        {
            Stop();
            delete[] _Session;
            WSACleanup();
        }

        bool SendPacket(SOCKADDR_IN SendAddr, char *SendBuf, int SendBufSize)
        {
            int Ret = sendto(_Sock, SendBuf, SendBufSize, 0, (sockaddr *)&SendAddr, sizeof(SendAddr));
            return true;
        }
        bool Run(void)
        {
            //if (_Sock != INVALID_SOCKET)
            //    return false;
            if (true == _Run)
                return false;
            _Run = true;

            _Sock = socket(AF_INET, SOCK_DGRAM, 0);
            if (INVALID_SOCKET == _Sock)
            {
                CrashDump::Crash();
                return false;
            }

            int Ret;
            SOCKADDR_IN BindAddr;
            BindAddr.sin_family = AF_INET;
            BindAddr.sin_port = htons(_BindPort);
            InetPton(AF_INET, _BindIP, &(BindAddr.sin_addr));
            Ret = bind(_Sock, (sockaddr *)&BindAddr, sizeof(BindAddr));
            if (SOCKET_ERROR == Ret)
            {
                int WSAError = WSAGetLastError();
                CrashDump::Crash();
                closesocket(_Sock);
                return false;
            }

            _NetworkThread = (HANDLE)_beginthreadex(NULL, 0, NetworkThreadProc, this, NULL, &_NetworkThreadId);
            if (INVALID_HANDLE_VALUE == _NetworkThread)
                CrashDump::Crash();

            return true;
        }
        bool Stop(void)
        {
            if (false == _Run)
                return false;
            _Run = false;

            if (INVALID_HANDLE_VALUE == _NetworkThread)
                CrashDump::Crash();
            _NetworkThread = INVALID_HANDLE_VALUE;
            _NetworkThreadId = 0;

            if (INVALID_SOCKET == _Sock)
                CrashDump::Crash();
            closesocket(_Sock);
            _Sock = INVALID_SOCKET;
            return true;
        }
    private:
        static unsigned __stdcall NetworkThreadProc(LPVOID lpParam)
        {
            return ((CUDPModule *)lpParam)->NetworkThreadUpdate();
        }
        unsigned NetworkThreadUpdate(void)
        {
            int WSAErrorCode;
            DWORD WaitRet;
            int EnumRet;
            char RecvBuf[RECV_BUF_SIZE];
            int RecvBytes;
            SOCKADDR_IN RecvAddr;
            int RecvAddrLen;
            //int SendBytes;

            WSAEVENT hEvent = WSACreateEvent();
            //WSAEventSelect(_Sock, hEvent, FD_READ | FD_WRITE);
            WSAEventSelect(_Sock, hEvent, FD_READ);

            WSANETWORKEVENTS NetworkEvents;
            while (_Run)
            {
                WaitRet = WSAWaitForMultipleEvents(1, &hEvent, FALSE, WSA_INFINITE, FALSE);
                if (WSA_WAIT_FAILED == WaitRet)
                {
                    WSAErrorCode = WSAGetLastError();
                    CrashDump::Crash();
                }
                else if (WSA_WAIT_TIMEOUT == WaitRet)
                {
                    // infinite라서 걸리면 안됨.
                    CrashDump::Crash();
                    continue;
                }

                EnumRet = WSAEnumNetworkEvents(_Sock, hEvent, &NetworkEvents);
                if (SOCKET_ERROR == EnumRet)
                {
                    WSAErrorCode = WSAGetLastError();
                    CrashDump::Crash();
                }
                else
                {
                    if (NetworkEvents.lNetworkEvents & FD_READ)
                    {
                        RecvAddrLen = sizeof(RecvAddr);
                        ZeroMemory(&RecvAddr, RecvAddrLen);

                        RecvBytes = recvfrom(_Sock, RecvBuf, RECV_BUF_SIZE, 0, (sockaddr *)&RecvAddr, &RecvAddrLen);
                        if (SOCKET_ERROR == RecvBytes)
                        {
                            WSAErrorCode = WSAGetLastError();
                            wprintf_s(L"recvfrom Error:%d\n", WSAErrorCode);
                            continue;
                            //CrashDump::Crash();
                            //return 0;
                        }
                        else if (0 == RecvBytes)
                        {
                            CrashDump::Crash();
                            return 0;
                        }
                        else if (RECV_BUF_SIZE <= RecvBytes)
                        {
                            CrashDump::Crash();
                            return 0;
                        }
                        int SessionIndex = FindSessionIndex(RecvAddr);
                        if (-1 == SessionIndex)
                        {
                            SessionIndex = FindEmptySessionIndex();
                            if (-1 == SessionIndex)
                            {
                                CrashDump::Crash();
                                continue;
                            }
                            _Session[SessionIndex].SessionInit(RecvAddr);
                        }
                        _Session[SessionIndex].PacketProc(RecvBuf, RecvBytes, this);
                    }
                    //if (NetworkEvents.lNetworkEvents & FD_WRITE)
                    //{
                    //    int Cnt;
                    //    for (Cnt = 0; Cnt < _SessionMax; ++Cnt)
                    //    {
                    //
                    //    }
                    //    if (_SendQ.GetUseSize() > 0)
                    //    {
                    //        SendBytes = sendto(_Sock, _SendQ.GetReadBufferPtr(), _SendQ.GetNotBrokenGetSize(), 0, (sockaddr *)&RecvAddr, RecvAddrLen);
                    //        if (SOCKET_ERROR == SendBytes)
                    //        {
                    //            WSAErrorCode = WSAGetLastError();
                    //            CrashDump::Crash();
                    //            return 0;
                    //        }
                    //        else if (0 == SendBytes)
                    //        {
                    //            CrashDump::Crash();
                    //            return 0;
                    //        }
                    //        _SendQ.MoveWritePtr(SendBytes);
                    //    }
                    //}
                }
            }
            return 0;
        }

        int FindSessionIndex(SOCKADDR_IN SessionAddr)
        {
            int Cnt;
            for (Cnt = 0; Cnt < _SessionMax; ++Cnt)
            {
                if (true == _Session[Cnt].CheckSession(SessionAddr))
                    return Cnt;
            }
            return -1;
        }
        int FindEmptySessionIndex(void)
        {
            int Cnt;
            for (Cnt = 0; Cnt < _SessionMax; ++Cnt)
            {
                if (false == _Session[Cnt].IsUse())
                    return Cnt;
            }
            return -1;
        }

    public:
        bool                        _ShutdownFlag = false;

    private:
        bool                        _Run;

        SOCKET                      _Sock;
        WCHAR                       _BindIP[IP_V4_MAX_LEN + 1];
        USHORT                      _BindPort;

        int                         _SessionMax;
        Session                     *_Session;

        HANDLE                      _NetworkThread;
        unsigned int                _NetworkThreadId;
    };
}

#endif