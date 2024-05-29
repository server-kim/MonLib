#ifndef __LOCKFREE_QUEUE_HEADER__
#define __LOCKFREE_QUEUE_HEADER__

namespace MonLib
{
    // 큐 해제 할 때 락 걸어줄것.

    template<typename DATA>
    class LockfreeQueue
    {
    private:
        struct st_NODE
        {
            DATA Data;
            volatile st_NODE *_p_Next;
        };

        struct st_END_NODE
        {
            st_NODE volatile * volatile _p_Node;
            __int64 volatile _UniqueNum;
        };

        int                 _QueueSize;
        long                _UseSize;
        //LONG64              _UseSize;
        LONG64              _UniqueNum;
        st_END_NODE         *_Head;
        st_END_NODE         *_Tail;

        LockfreeMemoryPool<st_NODE> *_MemoryPool;
        //MemoryPoolTLS<st_NODE> *_MemoryPool;

        // TLS를 쓰면 문제가 생길 여지가 있다.(Enqueue 로직 참고할 것.)
        //static MemoryPoolTLS<st_NODE> *_MemoryPool;

    public:
        LockfreeQueue(int Size = 0);
        virtual ~LockfreeQueue(void);

        void Initial(int Size);
        void Release(void);

        bool Enqueue(DATA Data);
        bool Dequeue(DATA *p_Data);
        bool Peek(DATA *p_Data, int Index);

        int GetQueueSize(void);
        int GetUseSize(void);
        bool isEmpty(void);
        int GetFreeSize(void);
    };

    template<typename DATA>
    LockfreeQueue<DATA>::LockfreeQueue(int Size)
    {
        _QueueSize = 0;
        _UseSize = 0;
        _UniqueNum = 0;
        _Head = nullptr;
        _Tail = nullptr;

        // TLS 아닐 때
        _MemoryPool = nullptr;

        // TLS
        //if (nullptr == _MemoryPool)
        //    _MemoryPool = new MonLib::MemoryPoolTLS<st_NODE>(300, false);

        Initial(Size);
    }
    template<typename DATA>
    LockfreeQueue<DATA>::~LockfreeQueue(void)
    {
        Release();
    }

    template<typename DATA>
    void LockfreeQueue<DATA>::Initial(int Size)
    {
        Release();

        _QueueSize = Size + 1;              // 더미 노드까지 포함하면 개수에 하나 적다. +1 해줘야 하나? -> 여기서 해준다.
        if (Size <= 0)
            _QueueSize = 0;

        // TLS 아닐 때
        _MemoryPool = new LockfreeMemoryPool<st_NODE>(_QueueSize);
        //

        _Head = (st_END_NODE *)_aligned_malloc(sizeof(st_END_NODE), 16);
        _Head->_UniqueNum = InterlockedIncrement64(&_UniqueNum);
        _Head->_p_Node = _MemoryPool->Alloc();
        _Head->_p_Node->_p_Next = nullptr;

        _Tail = (st_END_NODE *)_aligned_malloc(sizeof(st_END_NODE), 16);
        _Tail->_UniqueNum = InterlockedIncrement64(&_UniqueNum);
        _Tail->_p_Node = _Head->_p_Node;
    }
    template<typename DATA>
    void LockfreeQueue<DATA>::Release(void)
    {
        // TLS 아닐 때
        if (_MemoryPool != nullptr)
        {
            st_NODE *DeleteNode = (st_NODE *)(((st_END_NODE *)_Head)->_p_Node);
            st_NODE *NextNode = nullptr;
            while (DeleteNode != nullptr)
            {
                NextNode = (st_NODE *)DeleteNode->_p_Next;
                _MemoryPool->Free(DeleteNode);
                DeleteNode = NextNode;
            }
        
            delete _MemoryPool;
            _MemoryPool = nullptr;
        }
        //

        if (_UseSize > 0)
        {
            st_NODE *DeleteNode = (st_NODE *)(((st_END_NODE *)_Head)->_p_Node);
            st_NODE *NextNode = nullptr;
            while (DeleteNode != nullptr)
            {
                NextNode = (st_NODE *)DeleteNode->_p_Next;
                _MemoryPool->Free(DeleteNode);
                DeleteNode = NextNode;
            }
        }

        if (_Head != nullptr)
        {
            _aligned_free(_Head);
            _Head = nullptr;
        }

        if (_Tail != nullptr)
        {
            _aligned_free(_Tail);
            _Tail = nullptr;
        }

        _QueueSize = 0;
        _UseSize = 0;
        _UniqueNum = 0;
    }

    template<typename DATA>
    bool LockfreeQueue<DATA>::Enqueue(DATA Data)
    {
        st_NODE *p_Node = _MemoryPool->Alloc();
        if (nullptr == p_Node)
            return false;

        p_Node->Data = Data;
        //p_Node->_p_Next = nullptr;

        st_END_NODE CheckTail;
        volatile st_NODE *NowNext;       // volatile?

        LONG64 UniqueNum = InterlockedIncrement64(&_UniqueNum);

        while (1)
        {
            CheckTail._p_Node = _Tail->_p_Node;
            CheckTail._UniqueNum = _Tail->_UniqueNum;

            NowNext = CheckTail._p_Node->_p_Next;

            // Tail이 이미 반환이 되었다면 next가 null일 경우는 어차피 큐에 들어가기 직전이므로 그냥 뒤에 붙인다.
            // 따라서 TLS를 쓰면 큐 끼리 메모리풀을 공유하기 때문에 문제가 생긴다.
            // 큐마다 독자적인 메모리풀을 쓰도록 해줄 것.
            if (nullptr == NowNext)
            {
                p_Node->_p_Next = nullptr;
                if (InterlockedCompareExchangePointer((PVOID *)&CheckTail._p_Node->_p_Next, p_Node, nullptr) == nullptr)
                    //if (InterlockedCompareExchangePointer((PVOID *)&_Tail->_p_Node->_p_Next, p_Node, nullptr) == nullptr)
                {
                    InterlockedCompareExchange128((LONG64 *)_Tail, (LONG64)UniqueNum, (LONG64)p_Node, (LONG64 *)&CheckTail);
                    break;
                }
            }
            else
            {
                InterlockedCompareExchange128((LONG64 *)_Tail, (LONG64)UniqueNum, (LONG64)NowNext, (LONG64 *)&CheckTail);
                UniqueNum = InterlockedIncrement64(&_UniqueNum);
            }
        }

        InterlockedIncrement(&_UseSize);
        //InterlockedIncrement64(&_UseSize);
        return true;
    }
    template<typename DATA>
    bool LockfreeQueue<DATA>::Dequeue(DATA *p_Data)
    {
        st_END_NODE CheckHead;
        st_END_NODE CheckTail;

        volatile st_NODE *p_Next;

        if (InterlockedDecrement(&_UseSize) < 0)
        {
            InterlockedIncrement(&_UseSize);
            return false;
        }

        //if (InterlockedDecrement64(&_UseSize) < 0)
        //{
        //    InterlockedIncrement64(&_UseSize);
        //    return false;
        //}

        LONG64 UniqueNum = InterlockedIncrement64(&_UniqueNum);

        while (1)
        {
            //RETRY:
            CheckHead._p_Node = _Head->_p_Node;
            CheckHead._UniqueNum = _Head->_UniqueNum;

            //p_Next = CheckHead._p_Node->_p_Next;
            p_Next = _Head->_p_Node->_p_Next;

            if (nullptr == p_Next)  // 나올 수가 없는 상황인거 같은데 선생님은 나온다고 함. 일단 체크하고 나중에 확인할 것.
            {
                //InterlockedIncrement64(&DequeueNextNullCount);
                continue;
                //goto RETRY;
                //return false;
            }

            //if (&CheckHead == _Tail)
            if (CheckHead._p_Node == _Tail->_p_Node)
            {
                CheckTail._p_Node = _Tail->_p_Node;
                CheckTail._UniqueNum = _Tail->_UniqueNum;

                p_Next = CheckTail._p_Node->_p_Next;

                if (p_Next != nullptr)
                {
                    InterlockedCompareExchange128((LONG64 *)_Tail, (LONG64)UniqueNum, (LONG64)p_Next, (LONG64 *)&CheckTail);
                    UniqueNum = InterlockedIncrement64(&_UniqueNum);
                }
            }
            else
            {
                //*p_Data = ((st_NODE *)CheckHead._p_Node)->Data;
                *p_Data = ((st_NODE *)p_Next)->Data;
                if (InterlockedCompareExchange128((LONG64 *)_Head, (LONG64)UniqueNum, (LONG64)p_Next, (LONG64 *)&CheckHead) == 1)
                    break;
            }
        }

        _MemoryPool->Free((st_NODE *)CheckHead._p_Node);
        return true;
    }
    template<typename DATA>
    bool LockfreeQueue<DATA>::Peek(DATA *p_Data, int Index)
    {
        if (nullptr == p_Data || Index < 0)
            return false;

        // peek는 dequeue와 동시에 일어나지 않아야 한다는 전제에 UseSize를 체크한다.(UseSize 체크를 안하면 Enqueue와 겹쳐서 문제가 됨.)
        if (_UseSize < Index + 1)
            return false;

        int Cnt;
        volatile st_NODE *p_HeadNode;
        volatile st_NODE *p_DataNode;

        p_HeadNode = _Head->_p_Node;
        if (nullptr == p_HeadNode)
            return false;

        p_DataNode = p_HeadNode->_p_Next;
        if (nullptr == p_DataNode)
            return false;

        for (Cnt = 0; Cnt < Index; ++Cnt)
        {
            p_DataNode = p_DataNode->_p_Next;
            if (nullptr == p_DataNode)
                return false;
        }

        *p_Data = p_DataNode->Data;
        return true;
    }

    template<typename DATA>
    int LockfreeQueue<DATA>::GetQueueSize(void)
    {
        return _QueueSize;
    }
    template<typename DATA>
    int LockfreeQueue<DATA>::GetUseSize(void)
    {
        return _UseSize;
    }
    template<typename DATA>
    bool LockfreeQueue<DATA>::isEmpty(void)
    {
        if (_Head->_p_Node == _Tail->_p_Node)
            return true;
        return false;
    }
    template<typename DATA>
    int LockfreeQueue<DATA>::GetFreeSize(void)
    {
        return _QueueSize - _UseSize;
    }
}

#endif