#ifndef __MEMORY_POOL_TLS_HEADER__
#define __MEMORY_POOL_TLS_HEADER__

namespace MonLib
{
    template <class DATA>
    class MemoryPoolTLS
    {
        enum MemoryPoolTls : unsigned __int64
        {
            BLOCK_CHECK = ((unsigned __int64)(123456789))
        };

        template <class DATA>
        class ChunkBlock
        {
            struct DataBlock
            {
                unsigned __int64    _BlockCheck;

                ChunkBlock          *_p_ChunkBlock;
                DATA                _Data;
                DataBlock           *_p_Next;
            };

        private:
            ChunkBlock(void)
            {
                _p_MemoryPool = nullptr;
                _AllocMax = 0;
                _AllocRef = 0;
                _PlacementNew = false;

                _p_DataBlock = nullptr;
                _p_AllocTop = nullptr;
            }
            ~ChunkBlock(void)
            {
                delete _p_DataBlock;
            }

            void Constructor(MemoryPoolTLS<DATA> *p_MemoryPool, long AllocCount, bool PlacementNew)
            {
                _p_MemoryPool = p_MemoryPool;
                _AllocMax = AllocCount;
                _AllocRef = AllocCount;
                _PlacementNew = PlacementNew;

                _p_DataBlock = new DataBlock[AllocCount];

                int DataBlockIndex;
                for (DataBlockIndex = 0; DataBlockIndex < AllocCount; ++DataBlockIndex)
                {
                    _p_DataBlock[DataBlockIndex]._BlockCheck = BLOCK_CHECK;
                    _p_DataBlock[DataBlockIndex]._p_ChunkBlock = this;
                    //new (&_p_DataBlock[DataBlockIndex]._Data) DATA;       // 이거 안해줘도 위의 new 생성자에서 멤버변수 생성자를 호출해준다.

                    if (DataBlockIndex == AllocCount - 1)
                        _p_DataBlock[DataBlockIndex]._p_Next = nullptr;
                    else
                        _p_DataBlock[DataBlockIndex]._p_Next = &_p_DataBlock[DataBlockIndex + 1];
                }

                _p_AllocTop = _p_DataBlock;
            }
            void Initialize(void)
            {
                _p_AllocTop = _p_DataBlock;
                _AllocRef = _AllocMax;
            }

            DATA *Alloc(void)
            {
                DataBlock *p_Top = _p_AllocTop;
                if (nullptr == p_Top)
                {
                    int *p = nullptr;
                    *p = 0;
                    return nullptr;
                }

                DATA *p_Return = &p_Top->_Data;
                _p_AllocTop = p_Top->_p_Next;

                if (nullptr == p_Top->_p_Next)
                {
                    if (false == _p_MemoryPool->ChunkAlloc())
                    {
                        int *p = nullptr;
                        *p = 0;
                        return nullptr;
                    }
                }

                return p_Return;
            }
            bool Free(void)
            {
                long Ref = InterlockedDecrement(&_AllocRef);
                if (0 == Ref)
                {
                    if (_p_AllocTop != nullptr)
                        //if (_p_AllocTop->_p_Next != nullptr)
                    {
                        int *p = nullptr;
                        *p = 0;
                        return false;
                    }
                    _p_MemoryPool->_p_ChunkPool->Free(this);
                    return true;
                }
                return true;
            }

            long _AllocMax;
            DataBlock *_p_DataBlock;
            DataBlock *_p_AllocTop;
            long _AllocRef;
            bool _PlacementNew;

            MemoryPoolTLS<DATA> *_p_MemoryPool;

            friend class MemoryPoolTLS<DATA>;
            friend class LockfreeMemoryPool<ChunkBlock<DATA>>;
        };

    private:
        DWORD _TlsIndex;

        bool _PlacementNew;
        long _ChunkSize;
        long _AllocCount;
        long _UseCount;

        LockfreeMemoryPool<ChunkBlock<DATA>> *_p_ChunkPool;

    public:
        MemoryPoolTLS(long ChunkSize, bool PlacementNew)
        {
            _TlsIndex = TlsAlloc();
            if (TLS_OUT_OF_INDEXES == _TlsIndex)
            {
                CrashDump::Crash();
            }

            _PlacementNew = PlacementNew;
            _ChunkSize = ChunkSize;
            _AllocCount = 0;
            _UseCount = 0;

            _p_ChunkPool = new LockfreeMemoryPool<ChunkBlock<DATA>>(0);
        }
        ~MemoryPoolTLS(void)
        {
            delete _p_ChunkPool;
            TlsFree(_TlsIndex);          // tls 해제
        }

        DATA *Alloc(void)
        {
            ChunkBlock<DATA> *p_Chunk = (ChunkBlock<DATA> *)TlsGetValue(_TlsIndex);
            DATA *p_Return;

            if (0 == p_Chunk)
            {
                // 여기 들어오는 경우는 최초 한번 뿐이다. 그다음부터는 chunk에서 alloc할 때 다쓰면 알아서 해줄것이다.
                if (false == ChunkAlloc())
                {
                    CrashDump::Crash();
                    return nullptr;
                }
                p_Chunk = (ChunkBlock<DATA> *)TlsGetValue(_TlsIndex);
            }
            p_Return = p_Chunk->Alloc();
            if (nullptr == p_Return)
            {
                CrashDump::Crash();
                return nullptr;
            }

            InterlockedIncrement(&_UseCount);
            return p_Return;
        }
        bool Free(DATA *p_Data)
        {
            ChunkBlock<DATA>::DataBlock *p_DataBlock = (ChunkBlock<DATA>::DataBlock *)(((char *)p_Data) - (sizeof(unsigned __int64) + sizeof(ChunkBlock<DATA> *)));

            //unsigned __int64 BlockCheck = (unsigned __int64)(p_Data - sizeof(unsigned __int64));
            unsigned __int64 BlockCheck = p_DataBlock->_BlockCheck;
            // BlockCheck 할 것.
            if (BlockCheck != BLOCK_CHECK)
            {
                CrashDump::Crash();
                return false;
            }

            //ChunkBlock<DATA> *p_Chunk = (ChunkBlock<DATA> *)(p_Data - (sizeof(unsigned __int64) + sizeof(ChunkBlock<DATA> *)));
            //ChunkBlock<DATA> *p_Chunk = (ChunkBlock<DATA> *)(((char *)p_Data) - sizeof(ChunkBlock<DATA> *));
            ChunkBlock<DATA> *p_Chunk = p_DataBlock->_p_ChunkBlock;

            p_Chunk->Free();
            InterlockedDecrement(&_UseCount);
            return true;
        }
        bool ChunkAlloc(void)
        {
            ChunkBlock<DATA> *p_Chunk = _p_ChunkPool->Alloc();
            if (nullptr == p_Chunk)
                return false;
            if (false == TlsSetValue(_TlsIndex, (LPVOID)p_Chunk))
                return false;
            if (nullptr == p_Chunk->_p_DataBlock)
            {
                p_Chunk->Constructor(this, _ChunkSize, _PlacementNew);
                InterlockedAdd(&_AllocCount, _ChunkSize);
            }
            else
            {
                p_Chunk->Initialize();
            }

            return true;
        }

        int GetUseCount(void)
        {
            return _UseCount;
        }
        int GetAllocCount(void)
        {
            return _AllocCount;
        }

        int GetChunkPoolAllocCount(void)
        {
            return _p_ChunkPool->GetAllocCount();
        }
        int GetChunkPoolBlockCount(void)
        {
            return _p_ChunkPool->GetBlockCount();
        }
    };
}

#endif