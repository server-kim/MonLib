#ifndef __PACKET_HEADER__
#define __PACKET_HEADER__

// 패킷 디코드할 때 null이면 자기것으로 하는거 구현하기.
// 패킷 헤더를 다른 곳으로 빼자. -> Common.h 로 옮겼음.

namespace MonLib
{
    class Packet
    {
    public:

        // 패킷 검사할 때 쓰므로 public으로 풀어둔다.
        enum PACKET_DEFINE
        {
            BUFFER_SIZE_DEFAULT = 1024,                         // 데이터 영역 실제 사이즈는 1019
            //BUFFER_SIZE_DEFAULT = 20,                           // 데이터 영역 실제 사이즈는 1019
            HEADER_SIZE_MAX = 5
        };

        struct Exception_PacketIn
        {
            Exception_PacketIn(int Size) : _RequestInSize(Size) {}
            int _RequestInSize;
            // char Data[20];
        };
        struct Exception_PacketOut
        {
            Exception_PacketOut(int Size) : _RequestOutSize(Size) {}
            int _RequestOutSize;
            // char Data[20];
        };

    private:
        bool                            _EncodeFlag;            // 암호화 되었는지 플래그

        char                            *_Buf;
        int                             _BufSize;               // 데이터 영역 전체 사이즈
        int                             _DataSize;              // 데이터 영역에 들어있는 데이터 사이즈

        char                            *_HeaderPos;            // 헤더 시작 위치
        char                            *_ReadPos;              // 데이터를 읽을 위치
        char                            *_WritePos;             // 데이터를 쓸 위치

    public:
        // 패킷풀 변환 시 수정항목 : 멤버변수, cpp 변수 선언, 메모리풀 초기화, 카운트 함수 두개, 메인문 메모리풀 초기화 호출.
        //static MemoryPool<Packet>   *_PacketPool;              // 패킷풀
        //static LockfreeMemoryPool<Packet> *_PacketPool;        // 기존 패킷풀
        static MemoryPoolTLS<Packet>    *_PacketPool;          // TLS 패킷풀

        long                            _RefCount;             // 참조카운트(0이 되면 메모리 반환)

        long                            _AllocCount;           // 락프리 디버깅용 할당 카운트

        // Packet 암호화 항목
        static BYTE                     _PacketCode;
        static BYTE                     _XORCode1;
        static BYTE                     _XORCode2;

    public:
        Packet(int Size = BUFFER_SIZE_DEFAULT);
        virtual ~Packet(void);

        void Initial(void);
        void Initial(int Size);
        void Release(void);
        void Clear(void);

        // BYTE
        Packet& operator << (BYTE Data);
        Packet& operator >> (BYTE &Data);
        // char
        Packet& operator << (char Data);
        Packet& operator >> (char &Data);
        // short
        Packet& operator << (short Data);
        Packet& operator >> (short &Data);
        // WORD
        Packet& operator << (WORD Data);
        Packet& operator >> (WORD &Data);
        // int
        Packet& operator << (int Data);
        Packet& operator >> (int &Data);
        // DWORD
        Packet& operator << (DWORD Data);
        Packet& operator >> (DWORD &Data);
        // float
        Packet& operator << (float Data);
        Packet& operator >> (float &Data);
        // __int64
        Packet& operator << (__int64 Data);
        Packet& operator >> (__int64 &Data);
        // double
        Packet& operator << (double Data);
        Packet& operator >> (double &Data);

        int GetBufSize(void);
        int GetDataSize(void);
        int GetPacketSize(void);
        int GetPacketSize_CustomHeader(int CustomHeaderSize);

        char *GetBufferPtr(void);
        char *GetHeaderBufferPtr(void);
        char *GetHeaderBufferPtr_CustomHeader(int CustomHeaderSize);

        void SetHeader(char *Header);
        void SetHeader_CustomHeader(char *Header, int CustomHeaderSize);
        void SetHeader_CustomHeader_Short(unsigned short Header);

        char *GetReadBufferPtr(void);
        char *GetWriteBufferPtr(void);

        bool MoveWritePos(int size);
        bool MoveReadPos(int size);

        int PutData(char *p_Buf, int SrcSize);
        int GetData(char *p_Buf, int SrcSize);

        // 패킷풀
        static void MemoryPool(int Size);
        static Packet *Alloc(void);
        static void Free(Packet *p_Packet);
        static int GetPacketUseCount(void);
        static int GetPacketAllocCount(void);

        void addRef(void);
        void Free(void);
        int GetRefCount(void);

        // 암호화
        bool Encode(void);
        bool Decode(st_PACKET_HEADER *InHeader = nullptr);
    };
}

#endif