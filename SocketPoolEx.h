#ifndef __SOCKET_POOL_HEADER__
#define __SOCKET_POOL_HEADER__

namespace MonLib
{
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

    class SocketPoolEx
    {
    private:
        __int64                                             _SocketAllocCount;
        __int64                                             _SocketUseCount;
        LockfreeQueue<st_NETWORK_SOCKET *>   _SocketPool;

    public:
        SocketPoolEx(int SocketPoolSize)
        {
            int Cnt;
            st_NETWORK_SOCKET *p_NetworkSocket;

            _SocketAllocCount = SocketPoolSize;
            _SocketUseCount = 0;
            for (Cnt = 0; Cnt < _SocketAllocCount; ++Cnt)
            {
                p_NetworkSocket = new st_NETWORK_SOCKET;
                p_NetworkSocket->_IOCPFlag = false;
                p_NetworkSocket->_Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (INVALID_SOCKET == p_NetworkSocket->_Sock)
                    CrashDump::Crash();
                if (false == _SocketPool.Enqueue(p_NetworkSocket))
                    CrashDump::Crash();
            }
        }
        virtual ~SocketPoolEx(void)
        {

        }

        bool AllocSocket(st_NETWORK_SOCKET **pp_NetworkSocket)
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
        bool FreeSocket(st_NETWORK_SOCKET *p_NetworkSocket)
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