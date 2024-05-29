#ifndef __JUMP_POINT_SEARCH_HEADER__
#define __JUMP_POINT_SEARCH_HEADER__

// 현재는 도착지점이 갈 수 없는 곳이라면 탐색하지 않는다.

namespace MonLib
{
    class CJPS
    {
    private:
        enum JPS_SEARCH_DIRECTION
        {
            DirLL = 0,
            DirRR,
            DirUU,
            DirDD,
            DirLU,
            DirRU,
            DirRD,
            DirLD,
            DirNone
        };

        enum JPS_DEFINE
        {
            DIR_MATRIX_MAX_X = 3,
            DIR_MATRIX_MAX_Y = 3,
        };

        struct st_JPS_NODE              // 노드의 거리는 X거리와 Y거리를 합한 수치이다.
        {
            int		_X;
            int		_Y;

            int		_H;                 // 이 노드로부터 도착점까지의 다이렉트 거리.
            int		_G;                 // 시작점부터 이 노드까지 거쳐온 중간노드들 간의 거리 합산.
            int		_F;                 // H + G

            int     _PrevCnt;           // 이전 노드가 몇개 쌓였는지 카운트

            st_JPS_NODE	*_Prev;
        };

        int			_MaxWidth;
        int			_MaxHeight;
        int         _EndX;
        int         _EndY;

        std::multimap<int, st_JPS_NODE *>		*OpenMultiMap;
        std::list<st_JPS_NODE *>				*CloseList;

    public:
        CJPS(void);
        virtual ~CJPS(void);

    protected:
        int FindPath(int MapWidth, int MapHeight, int StartX, int StartY, int EndX, int EndY, int *XBuf, int *YBuf, int BufSize);
        
        // 맵의 속성을 체크하여 유저가 갈 수 있는지 여부를 돌려주는 함수.
        virtual bool CheckMapAttributeJPS(int X, int Y) = 0;
        virtual void OnCheckDirection(int X, int Y) = 0;
        virtual void OnJump(int X, int Y) = 0;

    private:
        int GetDir(int DirX, int DirY);
        bool IsWalkableAt(int X, int Y);
        void ClearList(void);

        void SetStartNode(int StartX, int StartY);
        void SetEndPos(int EndX, int EndY);

        // 현재 노드의 주변을 탐색.
        void NeighborNode(st_JPS_NODE *Node);

        // 주어진 방향으로 점프.
        void CheckDirection(st_JPS_NODE *Node, int X, int Y, int Dir);

        // 벽이나 코너 또는 목적지가 나올때까지 계속 진행.
        bool Jump(int StartX, int StartY, int *JumpX, int *JumpY, int Dir);
    };
}

#endif