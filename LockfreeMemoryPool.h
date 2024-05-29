#ifndef __LOCKFREE_MEMORY_POOL_HEADER__
#define __LOCKFREE_MEMORY_POOL_HEADER__

namespace MonLib
{
    template <typename DATA>
    class LockfreeMemoryPool
    {
    private:
        struct st_Node
        {
            st_Node(void)
            {
                _p_Next = nullptr;
            }

            st_Node *_p_Next;
        };

        struct st_Free_Node
        {
            st_Node volatile * volatile _p_FreeNode;
            __int64 volatile  _UniqueNum;
        };

        const bool          _DynamicAllocFlag = false;          // 동적할당 사용여부
        bool                _PlacementNewFlag = false;

        int                 _OneBlockSize;				// 하나의 블럭 사이즈
        long                _BlockCount;				// 메모리풀의 블럭 개수
        long                _AllocCount;				// 할당한 개수
        //LONG64              _BlockCount;				// 메모리풀의 블럭 개수
        //LONG64              _AllocCount;				// 할당한 개수

        LONG64              _UniqueNum;

        void                *_MemoryPool;
        st_Free_Node        *_FreeNode;

    public:
        LockfreeMemoryPool(int Size)
        {
            _OneBlockSize = sizeof(st_Node) + sizeof(DATA);
            _BlockCount = 0;
            _AllocCount = 0;

            _UniqueNum = 0;

            _MemoryPool = nullptr;
            _FreeNode = nullptr;

            Initial(Size);
        }
        virtual ~LockfreeMemoryPool(void)
        {
            Release();
        }

        void Initial(int Size)
        {
            Release();

            _BlockCount = Size;
            if (Size <= 0)
            {
                const_cast<bool&>(_DynamicAllocFlag) = true;
                _BlockCount = 0;
            }
            else
            {
                int Cnt;
                int MemoryPoolSize;
                st_Node *p_CurNode;
                st_Node *p_NextNode;

                MemoryPoolSize = _OneBlockSize * _BlockCount;
                _MemoryPool = _aligned_malloc(MemoryPoolSize, 16);

                p_CurNode = (st_Node *)&(((char *)(_MemoryPool))[MemoryPoolSize]);
                p_NextNode = nullptr;
                for (Cnt = 0; Cnt < _BlockCount; ++Cnt)
                {
                    p_CurNode = (st_Node *)(((char *)p_CurNode) - _OneBlockSize);
                    new ((DATA *)(p_CurNode + 1)) DATA;       // include <new>

                    p_CurNode->_p_Next = p_NextNode;
                    p_NextNode = p_CurNode;
                }
            }

            _FreeNode = (st_Free_Node *)_aligned_malloc(sizeof(st_Free_Node), 16);
            _FreeNode->_p_FreeNode = (st_Node *)_MemoryPool;
            _FreeNode->_UniqueNum = InterlockedIncrement64(&_UniqueNum);
        }
        void Release(void)
        {
            // template이 동적할당한 객체일 경우 해제를 어떻게 할 것인가? -> 사용자가 알아서 할 것.
            if (true == _DynamicAllocFlag)
            {
                if (_FreeNode != nullptr)
                {
                    // 동적할당일 때는 Top의 Next를 타고 가면서 Free 해줘야함.
                    st_Node *DeleteNode = (st_Node *)_FreeNode->_p_FreeNode;
                    st_Node *NextNode = nullptr;
                    while (1)
                    {
                        if (nullptr == DeleteNode)
                            break;
                        NextNode = DeleteNode->_p_Next;
                        _aligned_free(DeleteNode);
                        DeleteNode = NextNode;
                    }
                }
            }
            else
            {
                if (_MemoryPool != nullptr)
                {
                    // 메모리풀을 날리는 건 동적할당이 아닐 때.
                    _aligned_free(_MemoryPool);
                    _MemoryPool = nullptr;
                }
            }

            if (_FreeNode != nullptr)
            {
                _aligned_free(_FreeNode);
                _FreeNode = nullptr;
            }

            _BlockCount = 0;
            _AllocCount = 0;
            _UniqueNum = 0;
        }

        DATA *Alloc(void)
        {
            st_Node *p_Node = nullptr;
            st_Free_Node NewFreeNode;
            st_Free_Node CurFreeNode;

            DATA *p_Data = nullptr;

            if (InterlockedIncrement(&_AllocCount) > _BlockCount)
                //if (InterlockedIncrement64(&_AllocCount) > _BlockCount)
            {
                if (true == _DynamicAllocFlag)
                {
                    p_Node = (st_Node *)_aligned_malloc(_OneBlockSize, 16);
                    p_Node->_p_Next = nullptr;

                    p_Data = (DATA *)(p_Node + 1);
                    new (p_Data)DATA;

                    InterlockedIncrement(&_BlockCount);
                    //InterlockedIncrement64(&_BlockCount);
                    return p_Data;
                }

                InterlockedDecrement(&_AllocCount);
                //InterlockedDecrement64(&_AllocCount);
                return nullptr;
            }
            else
            {
                LONG64 UniqueNum = InterlockedIncrement64(&_UniqueNum);

                //NewFreeNode._p_FreeNode = p_Node;
                NewFreeNode._p_FreeNode = nullptr;
                NewFreeNode._UniqueNum = UniqueNum;

                int Ret;
                do
                {
                RETRY:
                    CurFreeNode._p_FreeNode = _FreeNode->_p_FreeNode;
                    CurFreeNode._UniqueNum = _FreeNode->_UniqueNum;

                    //// 이 부분을 체크해야 하는 이유
                    // 이미 블럭이 다 할당되어 있는 상황
                    // if (InterlockedIncrement(&_AllocCount) > _BlockCount) 비교문에서 비교하는 부분은 동기화가 없기 때문에
                    // 다른 스레드에서 동적 Alloc을 받아버리면 BlockCount가 증가한 후 비교하면 아래 상황이 나온다.
                    // 따라서 아래처럼 체크하던지 BlockCount를 지역변수로 받아서 비교하는 방법이 있다.
                    if (nullptr == CurFreeNode._p_FreeNode)
                    {
                        if (true == _DynamicAllocFlag)
                        {
                            goto RETRY;
                        }

                        InterlockedDecrement(&_AllocCount);
                        //InterlockedDecrement64(&_AllocCount);
                        return nullptr;
                    }

                    NewFreeNode._p_FreeNode = CurFreeNode._p_FreeNode->_p_Next;            // 얘를 대입하지 말고 그냥 써도 괜찮을 꺼 같아요.

                } while ((Ret = InterlockedCompareExchange128((LONG64 *)_FreeNode, (LONG64)NewFreeNode._UniqueNum, (LONG64)NewFreeNode._p_FreeNode, (LONG64 *)&CurFreeNode)) == 0);
            }

            p_Data = (DATA *)(CurFreeNode._p_FreeNode + 1);

            if (true == _PlacementNewFlag)
                new (p_Data)DATA;

            return p_Data;
        }
        bool Free(DATA *p_Data)
        {
            st_Node *p_Node = nullptr;
            st_Free_Node NewFreeNode;
            st_Free_Node CurFreeNode;

            LONG64 UniqueNum;

            if (_AllocCount <= 0 || nullptr == p_Data)
                return false;                               // 있어서는 안됨. 크래시 내려면 이 함수를 호출하는 부분에서 낼 것.

            p_Node = ((st_Node *)p_Data) - 1;

            UniqueNum = InterlockedIncrement64(&_UniqueNum);

            NewFreeNode._p_FreeNode = p_Node;
            NewFreeNode._UniqueNum = UniqueNum;

            do
            {
                CurFreeNode._p_FreeNode = _FreeNode->_p_FreeNode;
                CurFreeNode._UniqueNum = _FreeNode->_UniqueNum;

                p_Node->_p_Next = (st_Node *)CurFreeNode._p_FreeNode;

            } while (InterlockedCompareExchange128((LONG64 *)_FreeNode, (LONG64)NewFreeNode._UniqueNum, (LONG64)NewFreeNode._p_FreeNode, (LONG64 *)&CurFreeNode) != 1);

            InterlockedDecrement(&_AllocCount);
            //InterlockedDecrement64(&_AllocCount);
            return true;
        }
        int GetAllocCount(void)
        {
            return _AllocCount;
        }
        int GetBlockCount(void)
        {
            return _BlockCount;
        }
    };
}

#endif