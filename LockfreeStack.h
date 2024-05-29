#ifndef __LOCKFREE_STACK_HEADER__
#define __LOCKFREE_STACK_HEADER__

namespace MonLib
{
    // TLS 메모리풀을 쓸 때 청크사이즈 입력받는 부분 구현하기.

    template <typename DATA>
    class LockfreeStack
    {
    private:
        struct st_Node
        {
            DATA Data;
            st_Node *_p_Next;
        };

        struct st_Free_Node
        {
            volatile st_Node *_p_FreeNode;
            volatile __int64 _UniqueNum;
        };

        int                 _StackSize;
        long                _UseSize;
        //LONG64              _UseSize;
        LONG64              _UniqueNum;
        st_Free_Node        *_FreeNode;

        //LockfreeMemoryPool<st_Node> *MemoryPool;
        MemoryPoolTLS<st_Node> *MemoryPool;

    public:
        LockfreeStack(int Size = 0);
        virtual ~LockfreeStack(void);

        void Initial(int Size);
        void Release(void);

        bool Push(DATA Data);
        bool Pop(DATA *p_Data);

        int GetStackSize(void);
        int GetUseSize(void);
        bool isEmpty(void);
        int GetFreeSize(void);
    };

    template <typename DATA>
    LockfreeStack<DATA>::LockfreeStack(int Size)
    {
        _StackSize = 0;
        _UseSize = 0;
        _UniqueNum = 0;
        _FreeNode = nullptr;

        MemoryPool = nullptr;

        Initial(Size);
    }
    template <typename DATA>
    LockfreeStack<DATA>::~LockfreeStack(void)
    {
        Release();
    }

    template <typename DATA>
    void LockfreeStack<DATA>::Initial(int Size)
    {
        Release();

        _StackSize = Size;
        if (Size <= 0)
        {
            _StackSize = 0;
        }

        //MemoryPool = new LockfreeMemoryPool<st_Node>(_StackSize);
        MemoryPool = new MonLib::MemoryPoolTLS<st_Node>(300, false);

        _FreeNode = (st_Free_Node *)_aligned_malloc(sizeof(st_Free_Node), 16);
        _FreeNode->_p_FreeNode = nullptr;
        _FreeNode->_UniqueNum = InterlockedIncrement64(&_UniqueNum);
    }
    template <typename DATA>
    void LockfreeStack<DATA>::Release(void)
    {
        if (_FreeNode != nullptr)
        {
            _aligned_free(_FreeNode);
            _FreeNode = nullptr;
        }

        if (MemoryPool != nullptr)
        {
            delete MemoryPool;
            MemoryPool = nullptr;
        }

        _StackSize = 0;
        _UseSize = 0;
        _UniqueNum = 0;
    }

    template <typename DATA>
    bool LockfreeStack<DATA>::Push(DATA Data)
    {
        st_Node *p_Node = MemoryPool->Alloc();
        if (nullptr == p_Node)
            return false;

        p_Node->Data = Data;
        p_Node->_p_Next = nullptr;

        st_Free_Node NewTop;
        st_Free_Node CurTop;

        NewTop._p_FreeNode = p_Node;
        NewTop._UniqueNum = InterlockedIncrement64(&_UniqueNum);

        do
        {
            CurTop._p_FreeNode = _FreeNode->_p_FreeNode;
            CurTop._UniqueNum = _FreeNode->_UniqueNum;

            p_Node->_p_Next = (st_Node *)CurTop._p_FreeNode;
        } while (InterlockedCompareExchange128((LONG64 *)_FreeNode, (LONG64)NewTop._UniqueNum, (LONG64)NewTop._p_FreeNode, (LONG64 *)&CurTop) != 1);

        InterlockedIncrement(&_UseSize);
        //InterlockedIncrement64(&_UseSize);
        return true;
    }
    template <typename DATA>
    bool LockfreeStack<DATA>::Pop(DATA *p_Data)
    {
        st_Free_Node NewTop;
        st_Free_Node CurTop;

        NewTop._p_FreeNode = nullptr;
        NewTop._UniqueNum = InterlockedIncrement64(&_UniqueNum);

        do
        {
            CurTop._p_FreeNode = _FreeNode->_p_FreeNode;
            // 이 때 다른 스레드에서 pop을 하면 프리노드와 유니크넘이 다르더라도 감지를 못한다.
            // -> 따라서 pop을 한 노드가 다시 top으로 들어온다면 감지를 못하고 그 노드의 next를 찌른다.
            // (메모리풀 안에서 초기화를 하거나 TLS를 쓴다면 next가 바뀌기 때문에 문제가 된다.)
            // 따라서 push할 때도 cas128을 사용해야 한다.
            CurTop._UniqueNum = _FreeNode->_UniqueNum;

            if (nullptr == CurTop._p_FreeNode)
            {
                return false;
            }

            NewTop._p_FreeNode = CurTop._p_FreeNode->_p_Next;

        } while (InterlockedCompareExchange128((LONG64 *)_FreeNode, (LONG64)NewTop._UniqueNum, (LONG64)NewTop._p_FreeNode, (LONG64 *)&CurTop) != 1);

        st_Node *DeleteNode = (st_Node *)CurTop._p_FreeNode;

        *p_Data = DeleteNode->Data;
        MemoryPool->Free(DeleteNode);

        InterlockedDecrement(&_UseSize);
        //InterlockedDecrement64(&_UseSize);

        return true;
    }

    template <typename DATA>
    int LockfreeStack<DATA>::GetStackSize(void)
    {
        return _StackSize;
    }
    template <typename DATA>
    int LockfreeStack<DATA>::GetUseSize(void)
    {
        return _UseSize;
    }
    template <typename DATA>
    bool LockfreeStack<DATA>::isEmpty(void)
    {
        if (0 <= _UseSize)
            return true;
        return false;
    }
    template <typename DATA>
    int LockfreeStack<DATA>::GetFreeSize(void)
    {
        return 0;
    }
}

#endif