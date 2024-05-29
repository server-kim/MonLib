#ifndef __STREAMING_QUEUE_HEADER__
#define __STREAMING_QUEUE_HEADER__

namespace MonLib
{
    class StreamingQueue
    {
    private:
        enum
        {
            Buffer_Blank = 4,
            //Default_Size = 1024
            Default_Size = 5000
        };

        int		_ReadPos;
        int		_WritePos;
        int		_BufSize;
        char	*_Buf;

        CRITICAL_SECTION _cs;
    public:
        StreamingQueue(int Size = Default_Size);
        virtual ~StreamingQueue(void);
        void Initial(int Size);
        void Release(void);

        int GetBufferSize(void);
        int GetUseSize(void);
        int GetFreeSize(void);
        int GetNotBrokenGetSize(void);
        int GetNotBrokenPutSize(void);

        int Put(char *buf, int size);
        int Get(char *buf, int size);
        int Peek(char *buf, int size, int index = 0);

        char *GetBufferPtr(void);
        char *GetReadBufferPtr(void);
        char *GetWriteBufferPtr(void);

        void Clear(void);
        bool RemoveData(int size);
        int MoveWritePtr(int size);

        void Lock(void);
        void Unlock(void);
    };
}

#endif