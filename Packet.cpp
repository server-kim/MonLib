#include "LibHeader.h"

namespace MonLib
{
    BYTE                     Packet::_PacketCode;
    BYTE                     Packet::_XORCode1;
    BYTE                     Packet::_XORCode2;

    MemoryPoolTLS<Packet> *Packet::_PacketPool;

    Packet::Packet(int Size)
    {
        if (Size < HEADER_SIZE_MAX + 1)
            Size = BUFFER_SIZE_DEFAULT;

        _Buf = nullptr;
        _BufSize = 0;
        _DataSize = 0;

        _HeaderPos = nullptr;
        _ReadPos = nullptr;
        _WritePos = nullptr;

        _RefCount = 1;      // 생성과 동시에 참조.

        _AllocCount = 0;

        Initial(Size);
    }
    Packet::~Packet(void)
    {
        Release();
    }

    void Packet::Initial(void)
    {
        memset(_Buf, 0, _BufSize + HEADER_SIZE_MAX);
        _RefCount = 1;
        Clear();
    }
    void Packet::Initial(int Size)
    {
        // Packet 재할당
        Release();

        _Buf = new char[Size];
        _BufSize = Size - HEADER_SIZE_MAX;

        // _RefCount = 1;      // 이거는 들어가야 하나?

        Clear();
    }
    void Packet::Release(void)
    {
        if (_Buf != nullptr)
        {
            delete[]_Buf;
            _Buf = nullptr;
        }
    }
    void Packet::Clear(void)
    {
        // Packet 재사용할 때

        _EncodeFlag = false;

        _DataSize = 0;
        _HeaderPos = _Buf;
        _ReadPos = _Buf + HEADER_SIZE_MAX;
        _WritePos = _Buf + HEADER_SIZE_MAX;
    }

    // BYTE
    Packet& Packet::operator << (BYTE Data)
    {
        PutData((char *)&Data, sizeof(BYTE));
        return *this;
    }
    Packet& Packet::operator >> (BYTE &Data)
    {
        GetData((char *)&Data, sizeof(BYTE));
        return *this;
    }
    // char
    Packet& Packet::operator << (char Data)
    {
        PutData((char *)&Data, sizeof(char));
        return *this;
    }
    Packet& Packet::operator >> (char &Data)
    {
        GetData((char *)&Data, sizeof(char));
        return *this;
    }
    // short
    Packet& Packet::operator << (short Data)
    {
        PutData((char *)&Data, sizeof(short));
        return *this;
    }
    Packet& Packet::operator >> (short &Data)
    {
        GetData((char *)&Data, sizeof(short));
        return *this;
    }
    // WORD
    Packet& Packet::operator << (WORD Data)
    {
        PutData((char *)&Data, sizeof(WORD));
        return *this;
    }
    Packet& Packet::operator >> (WORD &Data)
    {
        GetData((char *)&Data, sizeof(WORD));
        return *this;
    }
    // int
    Packet& Packet::operator << (int Data)
    {
        PutData((char *)&Data, sizeof(int));
        return *this;
    }
    Packet& Packet::operator >> (int &Data)
    {
        GetData((char *)&Data, sizeof(int));
        return *this;
    }
    // DWORD
    Packet& Packet::operator << (DWORD Data)
    {
        PutData((char *)&Data, sizeof(DWORD));
        return *this;
    }
    Packet& Packet::operator >> (DWORD &Data)
    {
        GetData((char *)&Data, sizeof(DWORD));
        return *this;
    }
    // float
    Packet& Packet::operator << (float Data)
    {
        PutData((char *)&Data, sizeof(float));
        return *this;
    }
    Packet& Packet::operator >> (float &Data)
    {
        GetData((char *)&Data, sizeof(float));
        return *this;
    }
    // __int64
    Packet& Packet::operator << (__int64 Data)
    {
        PutData((char *)&Data, sizeof(__int64));
        return *this;
    }
    Packet& Packet::operator >> (__int64 &Data)
    {
        GetData((char *)&Data, sizeof(__int64));
        return *this;
    }
    // double
    Packet& Packet::operator << (double Data)
    {
        PutData((char *)&Data, sizeof(double));
        return *this;
    }
    Packet& Packet::operator >> (double &Data)
    {
        GetData((char *)&Data, sizeof(double));
        return *this;
    }

    int Packet::GetBufSize(void)                                        { return _BufSize; }
    int Packet::GetDataSize(void)                                       { return _DataSize; }
    int Packet::GetPacketSize(void)                                     { return (int)(_WritePos - _Buf); }
    int Packet::GetPacketSize_CustomHeader(int CustomHeaderSize)        { return (int)(_WritePos - _Buf - (HEADER_SIZE_MAX - CustomHeaderSize)); }

    char *Packet::GetBufferPtr(void)                                    { return _ReadPos; }
    char *Packet::GetHeaderBufferPtr(void)                              { return _HeaderPos; }
    char *Packet::GetHeaderBufferPtr_CustomHeader(int CustomHeaderSize) { return _HeaderPos + (HEADER_SIZE_MAX - CustomHeaderSize); }

    void Packet::SetHeader(char *Header)
    {
        memcpy_s(_HeaderPos, HEADER_SIZE_MAX, Header, HEADER_SIZE_MAX);
    }
    void Packet::SetHeader_CustomHeader(char *Header, int CustomHeaderSize)
    {
        memcpy_s(_HeaderPos + (HEADER_SIZE_MAX - CustomHeaderSize), CustomHeaderSize, Header, CustomHeaderSize);
    }

    void Packet::SetHeader_CustomHeader_Short(unsigned short Header)
    {
        *(unsigned short *)(_HeaderPos + (HEADER_SIZE_MAX - 2)) = Header;
    }

    char *Packet::GetReadBufferPtr(void)
    {
        return _ReadPos;
    }
    char *Packet::GetWriteBufferPtr(void)
    {
        return _WritePos;
    }

    bool Packet::MoveWritePos(int size)
    {
        if (_BufSize - _DataSize < size)
            return false;
        _WritePos += size;
        _DataSize += size;
        return true;
    }
    bool Packet::MoveReadPos(int size)
    {
        if (_DataSize < size)
            return false;
        _ReadPos += size;
        _DataSize -= size;
        return true;
    }

    int Packet::PutData(char *p_Buf, int SrcSize)
    {
        if (_BufSize - _DataSize < SrcSize)
        {
            throw Exception_PacketIn(SrcSize);
            return 0;
        }

        memcpy_s(_WritePos, SrcSize, p_Buf, SrcSize);

        _WritePos += SrcSize;
        _DataSize += SrcSize;
        return SrcSize;
    }
    int Packet::GetData(char *p_Buf, int SrcSize)
    {
        if (_DataSize < SrcSize)
        {
            throw Exception_PacketOut(SrcSize);
            return 0;
        }

        memcpy_s(p_Buf, SrcSize, _ReadPos, SrcSize);

        _ReadPos += SrcSize;
        _DataSize -= SrcSize;
        return SrcSize;
    }

    // 패킷풀
    void Packet::MemoryPool(int Size)
    {
        _PacketPool = new MemoryPoolTLS<Packet>(Size, false);       // 선생님 말로는 200개 정도가 적당하다고 함.
    }
    Packet *Packet::Alloc(void)
    {
        //ProfileBegin(L"Packet_Alloc");
        Packet *p_Packet = Packet::_PacketPool->Alloc();
        //Packet *p_Packet = new Packet();
        //ProfileEnd(L"Packet_Alloc");

        p_Packet->Initial();
        InterlockedIncrement(&p_Packet->_AllocCount);
        return p_Packet;
    }
    void Packet::Free(Packet *p_Packet)
    {
        p_Packet->_RefCount--;
        if (0 == p_Packet->_RefCount)
        {
            Packet::_PacketPool->Free(p_Packet);
            return;     // 혹시 모르니 리턴.
        }
        else if (p_Packet->_RefCount < 0)
        {
            // 특별 관리 대상. 죽어랏!
            int *p = nullptr;
            *p = 0;
        }
    }
    int Packet::GetPacketUseCount(void)
    {
        return Packet::_PacketPool->GetUseCount();
    }
    int Packet::GetPacketAllocCount(void)
    {
        return Packet::_PacketPool->GetAllocCount();
    }

    void Packet::addRef(void)
    {
        //_RefCount++;
        InterlockedIncrement(&_RefCount);
    }
    void Packet::Free(void)
    {
        int RefCount = InterlockedDecrement(&_RefCount);
        if (0 == RefCount)
        {
            //ProfileBegin(L"Packet_Free");
            Packet::_PacketPool->Free(this);
            //delete this;
            //ProfileEnd(L"Packet_Free");

            return;     // 혹시 모르니 리턴.
        }
        else if (RefCount < 0)
        {
            // 특별 관리 대상. 죽어랏!
            int *p = nullptr;
            *p = 0;
        }
    }
    int Packet::GetRefCount(void)
    {
        return _RefCount;
    }

    bool Packet::Encode(void)
    {
        if (true == _EncodeFlag)
            return false;
        _EncodeFlag = true;

        int Cnt;
        int RandXORCode;
        __int64 PayloadSum = 0;
        char *EncodeStart = nullptr;
        st_PACKET_HEADER *Header = (st_PACKET_HEADER *)_HeaderPos;
        int DataSize = GetDataSize();

        Header->Code = Packet::_PacketCode;
        Header->Len = DataSize;

        // 1. Rand XOR Code 생성
        RandXORCode = rand() % 256;
        Header->XORCode = RandXORCode;

        // 2. Payload의 Checksum 계산(Payload 부분을 1byte 씩 모두 더해서 % 256 한 unsigned char 값)
        for (Cnt = 0; Cnt < DataSize; ++Cnt)
        {
            PayloadSum += _ReadPos[Cnt];
        }
        Header->CheckSum = (unsigned char)(PayloadSum % 256);

        // 3. Rand XOR Code로 [CheckSum, Payload] xor
        EncodeStart = (char *)&Header->CheckSum;
        for (Cnt = 0; Cnt < 1 + DataSize; ++Cnt)
        {
            EncodeStart[Cnt] = EncodeStart[Cnt] ^ RandXORCode;
        }

        // 4. 고정 XOR Code로 [Rand XOR Code, CheckSum, Payload] xor
        EncodeStart = (char *)&Header->XORCode;
        for (Cnt = 0; Cnt < 2 + DataSize; ++Cnt)
        {
            EncodeStart[Cnt] = (EncodeStart[Cnt] ^ Packet::_XORCode1) ^ Packet::_XORCode2;
            //EncodeStart[Cnt] = EncodeStart[Cnt] ^ g_XORCode1;
            //EncodeStart[Cnt] = EncodeStart[Cnt] ^ g_XORCode2;
        }

        return true;
    }
    bool Packet::Decode(st_PACKET_HEADER *InHeader)
    {
        //if (false == _EncodeFlag)
        //    return false;
        //_EncodeFlag = false;

        int Cnt;
        int RandXORCode;
        __int64 PayloadSum = 0;
        char *DecodeStart = nullptr;
        st_PACKET_HEADER *Header = (st_PACKET_HEADER *)_HeaderPos;
        int DataSize = GetDataSize();
        BYTE PacketCheckSum;

        // header 가 null 이 아닐 경우 그 헤더를 기준으로 디코딩 하도록 수정할 것.
        //if (nullptr == InHeader)
        //    Header = (st_PACKET_HEADER *)_HeaderPos;
        //else
        //    Header = InHeader;

        if (Header->Len != DataSize)
            return false;

        //1. 고정 XOR Code 2 로[Rand XOR Code, CheckSum, Payload] 를 XOR
        //2. 고정 XOR Code 1 로[Rand XOR Code, CheckSum, Payload] 를 XOR
        DecodeStart = (char *)&Header->XORCode;
        for (Cnt = 0; Cnt < 2 + DataSize; ++Cnt)
        {
            DecodeStart[Cnt] = (DecodeStart[Cnt] ^ Packet::_XORCode2) ^ Packet::_XORCode1;
        }

        //3. Rand XOR Code 를 파악.
        RandXORCode = Header->XORCode;

        //4. Rand XOR Code 로[CheckSum - Payload] 바이트 단위 xor
        DecodeStart = (char *)&Header->CheckSum;
        for (Cnt = 0; Cnt < 1 + DataSize; ++Cnt)
        {
            DecodeStart[Cnt] = DecodeStart[Cnt] ^ RandXORCode;
        }

        //5. Payload 를 checksum 공식으로 계산 후 패킷의 checksum 과 비교
        for (Cnt = 0; Cnt < DataSize; ++Cnt)
        {
            PayloadSum += _ReadPos[Cnt];
        }
        PacketCheckSum = (unsigned char)(PayloadSum % 256);
        if (Header->CheckSum != PacketCheckSum)
            return false;

        return true;
    }


    /*


    bool Encode(void);
    bool Decode(st_PACKET_HEADER *pInHeader = nullptr);     // 인자 안 넣으면 자기껄로 하는데 인자를 넣는 경우는 CompletePacket()에서 헤더를 peek해서 확인하는 용도로 쓴다.


    */
}