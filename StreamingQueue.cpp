#include "LibHeader.h"

namespace MonLib
{
    StreamingQueue::StreamingQueue(int Size)
    {
        _Buf = nullptr;
        _BufSize = 0;
        _ReadPos = 0;
        _WritePos = 0;

        InitializeCriticalSection(&_cs);

        Initial(Size);
    }
    StreamingQueue::~StreamingQueue(void)
    {
        Release();
    }
    void StreamingQueue::Initial(int Size)
    {
        Release();

        _ReadPos = 0;
        _WritePos = 0;
        _BufSize = Size + Buffer_Blank;
        _Buf = new char[_BufSize];
    }
    void StreamingQueue::Release(void)
    {
        if (_Buf != nullptr)
            delete[]_Buf;
    }

    int StreamingQueue::GetBufferSize(void)
    {
        return _BufSize - Buffer_Blank;
    }
    int StreamingQueue::GetUseSize(void)
    {
        if (_WritePos >= _ReadPos)
            return _WritePos - _ReadPos;
        else
            return _WritePos + (_BufSize - _ReadPos);
    }
    int StreamingQueue::GetFreeSize(void)
    {
        int UseSize = 0;

        if (_WritePos >= _ReadPos)
            UseSize = _WritePos - _ReadPos;
        else
            UseSize = _WritePos + (_BufSize - _ReadPos);

        return _BufSize - Buffer_Blank - UseSize;
    }
    int StreamingQueue::GetNotBrokenGetSize(void)
    {
        int GetSize = 0;

        if (_WritePos >= _ReadPos)
            GetSize = _WritePos - _ReadPos;
        else
            GetSize = _BufSize - _ReadPos;

        return GetSize;
    }
    int StreamingQueue::GetNotBrokenPutSize(void)
    {
        int PutSize = 0;
        int WritePosMaxVal = _ReadPos - Buffer_Blank;

        if (_WritePos >= _ReadPos)
        {
            //if (_ReadPos - Buffer_Blank < 0)
            if (WritePosMaxVal < 0)
            {
                //PutSize = _BufSize - _WritePos + _ReadPos - Buffer_Blank;
                PutSize = _BufSize - _WritePos + WritePosMaxVal;
            }
            else
            {
                PutSize = _BufSize - _WritePos;
            }
        }
        else if (_WritePos < _ReadPos)
        {
            //PutSize = _ReadPos - Buffer_Blank - _WritePos;
            PutSize = WritePosMaxVal - _WritePos;
        }

        return PutSize;
    }

    int StreamingQueue::Put(char *buf, int size)
    {
        int ReadPos = _ReadPos;
        int WritePos = _WritePos;

        if (WritePos + Buffer_Blank == ReadPos)	// 꽉찼는지 체크
            return 0;

        // Free Size를 구한다.
        int UseSize = 0;
        if (WritePos >= ReadPos)
            UseSize = WritePos - ReadPos;
        else
            UseSize = WritePos + (_BufSize - ReadPos);

        int FreeSize = _BufSize - Buffer_Blank - UseSize;
        if (FreeSize < size)
            return 0;

        // GetNotBrokenPutSize를 구한다.
        int PutSize = 0;
        int WritePosMaxVal = ReadPos - Buffer_Blank;
        if (WritePos >= ReadPos)
        {
            if (WritePosMaxVal < 0)
                PutSize = _BufSize - WritePos + WritePosMaxVal;
            else
                PutSize = _BufSize - WritePos;
        }
        else if (WritePos < ReadPos)
        {
            PutSize = WritePosMaxVal - WritePos;
        }

        if (PutSize >= size)
        {
            memcpy_s(_Buf + WritePos, size, buf, size);
        }
        else
        {
            // 버퍼가 잘려진 경우
            int FirstWriteSize = _BufSize - WritePos;
            memcpy_s(_Buf + WritePos, FirstWriteSize, buf, FirstWriteSize);
            memcpy_s(_Buf, size - FirstWriteSize, buf + FirstWriteSize, size - FirstWriteSize);
        }

        _WritePos = (_WritePos + size) % _BufSize;
        return size;
    }
    int StreamingQueue::Get(char *buf, int size)
    {
        int ReadPos = _ReadPos;
        int WritePos = _WritePos;

        if (ReadPos == WritePos)	// 비었는지 체크
            return 0;

        // Use Size를 구한다.
        int UseSize = 0;
        if (WritePos >= ReadPos)
            UseSize = WritePos - ReadPos;
        else
            UseSize = WritePos + (_BufSize - ReadPos);

        if (UseSize < size)
            return 0;

        // 한번에 가져올 수 있는 사이즈를 구한다.
        int GetSize = 0;
        if (WritePos >= ReadPos)
            GetSize = WritePos - ReadPos;
        else
            GetSize = _BufSize - ReadPos;

        if (GetSize >= size)
        {
            memcpy_s(buf, size, _Buf + ReadPos, size);
        }
        else
        {
            // 버퍼가 잘려진 경우
            memcpy_s(buf, GetSize, _Buf + ReadPos, GetSize);
            memcpy_s(buf + GetSize, size - GetSize, _Buf, size - GetSize);
        }

        _ReadPos = (_ReadPos + size) % _BufSize;
        return size;
    }
    int StreamingQueue::Peek(char *buf, int size, int index)
    {
        int ReadPos = _ReadPos;
        int WritePos = _WritePos;

        if (ReadPos == WritePos)          // 비었는지 체크
            return 0;

        // Use Size를 구한다.
        int UseSize = 0;
        if (WritePos >= ReadPos)
            UseSize = WritePos - ReadPos;
        else
            UseSize = WritePos + (_BufSize - ReadPos);

        if (UseSize < index + size)    // index check
            return 0;

        // 인덱스 지점 설정.
        int IndexPos = (ReadPos + index) % _BufSize;

        // 한번에 가져올 수 있는 사이즈를 구한다.
        int GetSize = 0;
        if (WritePos >= IndexPos)
            GetSize = WritePos - IndexPos;
        else
            GetSize = _BufSize - IndexPos;

        if (GetSize >= size)
        {
            memcpy_s(buf, size, _Buf + IndexPos, size);
        }
        else
        {
            // 버퍼가 잘려진 경우
            memcpy_s(buf, GetSize, _Buf + IndexPos, GetSize);
            memcpy_s(buf + GetSize, size - GetSize, _Buf, size - GetSize);
        }
        return size;
    }

    char *StreamingQueue::GetBufferPtr(void)
    {
        return _Buf;
    }
    char *StreamingQueue::GetReadBufferPtr(void)
    {
        if (_ReadPos == _WritePos)	// 비었는지 체크
            return nullptr;
        return &_Buf[_ReadPos];
    }
    char *StreamingQueue::GetWriteBufferPtr(void)
    {
        if (_WritePos + Buffer_Blank == _ReadPos)	// 꽉찼는지 체크
            return nullptr;
        return &_Buf[_WritePos];
    }

    void StreamingQueue::Clear(void)
    {
        _ReadPos = 0;
        _WritePos = 0;
    }
    bool StreamingQueue::RemoveData(int size)
    {
        int UseSize = 0;

        if (_WritePos >= _ReadPos)
            UseSize = _WritePos - _ReadPos;
        else
            UseSize = _WritePos + (_BufSize - _ReadPos);

        if (size <= UseSize)
        {
            _ReadPos = (_ReadPos + size) % _BufSize;
            return true;
        }
        else
        {
            return false;
        }
    }
    int StreamingQueue::MoveWritePtr(int size)
    {
        int UseSize = 0;
        int FreeSize = 0;

        if (_WritePos >= _ReadPos)
            UseSize = _WritePos - _ReadPos;
        else
            UseSize = _WritePos + (_BufSize - _ReadPos);

        FreeSize = _BufSize - Buffer_Blank - UseSize;

        if (size > FreeSize)
            return 0;

        _WritePos = (_WritePos + size) % _BufSize;
        return size;
    }

    void StreamingQueue::Lock(void)
    {
        EnterCriticalSection(&_cs);
    }
    void StreamingQueue::Unlock(void)
    {
        LeaveCriticalSection(&_cs);
    }
}