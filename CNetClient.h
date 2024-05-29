#ifndef __NET_CLIENT_HEADER__
#define __NET_CLIENT_HEADER__

namespace MonLib
{
    class CNetClient
    {
    private:
        enum LAN_CLIENT_DEFINE
        {
            PACKET_POOL_CHUNK_SIZE = 1,
            WORKER_THREAD_MAX_COUNT = 2
        };

        int CompletePacket(void);

        bool RecvPost(void);
        bool SendPost(void);

        // 완료통지 래핑
        void CompleteRecv(DWORD cbTransferred);
        void CompleteSend(DWORD cbTransferred);

        // workerthread(포인터) 던져서 내부에서 포인터->update 호출

        void ClientShutdown(void);              // shutdown으로 안전한 종료 유도
        void ClientDisconnect(void);            // socket 접속 강제 끊기
        void SocketClose(void);                 // closesocket(내부용)
        void ClientRelease(void);               // client session 해제

    public:
        CNetClient(void);
        virtual ~CNetClient(void);

        bool Connect(WCHAR *p_IP, USHORT Port, int WorkerThreadCnt, BYTE PacketCode, BYTE XORCode1, BYTE XORCode2, bool Nagle = false);  // OutIP -> 클라이언트도 바인딩해도 된다. / 오픈 IP / 포트 / 워커스레드 수 / 나글옵션
        void Disconnect(void);
        // release(); -> 얘는 파괴자에서 호출이라 퍼블릭은 아닌듯.
        bool SendPacket(Packet *p_Packet);

        //bool GetClientConnect(void);
        int GetRecvQSize(void);
        bool IsConnect(void);

    protected:
        virtual void OnEnterJoinServer(void) = 0;
        virtual void OnLeaveServer(void) = 0;

        virtual void OnRecv(Packet *p_Packet) = 0;
        virtual void OnSend(int SendSize) = 0;

        virtual void OnWorkerThreadBegin(void) = 0;
        virtual void OnWorkerThreadEnd(void) = 0;

        virtual void OnError(int ErrorCode, WCHAR *ErrorMessage) = 0;

        static unsigned __stdcall WorkerThreadProc(LPVOID lpParam);

    private:
        //bool                        _Connect;
        long                        _ConnectTry;

        SOCKET                      _Sock;
        StreamingQueue              _RecvQ;
        LockfreeQueue<Packet *>     _SendQ;
        OVERLAPPED                  _RecvOL;
        OVERLAPPED                  _SendOL;

        st_IO_COMPARE               *_p_IOCompare;

        ULONG32                     _SendIO;            // Send 작업 플래그 : 0, 1
        int                         _SendPacketCnt;     // 내가 이번에 보낸 패킷의 개수
        int                         _SendPacketSize;    // 내가 이번에 보낸 패킷의 사이즈

        HANDLE                      _hIocp;

        int                         _WorkerThreadMax;
        HANDLE                      _WorkerThread[WORKER_THREAD_MAX_COUNT];
        unsigned int                _WorkerThreadId[WORKER_THREAD_MAX_COUNT];
    };
}

#endif