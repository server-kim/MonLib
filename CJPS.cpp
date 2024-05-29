#include "LibHeader.h"

namespace MonLib
{
    CJPS::CJPS(void)
    {
        _MaxWidth = 0;
        _MaxHeight = 0;
        _EndX = 0;
        _EndY = 0;

        OpenMultiMap = new std::multimap<int, st_JPS_NODE *>();
        CloseList = new std::list<st_JPS_NODE *>();
    }
    CJPS::~CJPS(void)
    {
        ClearList();
        delete OpenMultiMap;
        delete CloseList;
    }

    int CJPS::FindPath(int MapWidth, int MapHeight, int StartX, int StartY, int EndX, int EndY, int *XBuf, int *YBuf, int BufSize)
    {
        if (nullptr == XBuf || nullptr == YBuf || BufSize < 1)
            return -1;

        if (MapWidth < 1 || MapHeight < 1)
            return -1;

        _MaxWidth = MapWidth;
        _MaxHeight = MapHeight;

        if (false == IsWalkableAt(StartX, StartY))
            return -1;
        if (false == IsWalkableAt(EndX, EndY))
            return -1;

        ClearList();      // 시작할 때 할 수도 있는데 끝날 때 하는걸로 정책을 정함.

        SetStartNode(StartX, StartY);
        SetEndPos(EndX, EndY);

        st_JPS_NODE *CurNode = nullptr;
        std::multimap<int, st_JPS_NODE *>::iterator OpenMultiMapItr;
        while (OpenMultiMap->size() < BufSize)      // 이건 일단 요청한 결과 사이즈와 같게 한다.
        {
            OpenMultiMapItr = OpenMultiMap->begin();
            if (OpenMultiMap->end() == OpenMultiMapItr)
            {
                // 못찾는 길이다.
                return -1;
            }
            CurNode = OpenMultiMapItr->second;
            OpenMultiMap->erase(OpenMultiMapItr);

            CloseList->push_back(CurNode);

            if (CurNode->_PrevCnt >= BufSize)
            {
                break;
            }

            if (CurNode->_X == _EndX && CurNode->_Y == _EndY)
            {
                break;
            }

            NeighborNode(CurNode);
        }

        // 패스 길이에 맞게 자른다.
        st_JPS_NODE *PathNode;
        int NodeCnt;
        int NodeSliceCnt;
        int Cnt;

        PathNode = CurNode;
        NodeCnt = CurNode->_PrevCnt;
        if (NodeCnt > BufSize)
        {
            NodeSliceCnt = NodeCnt - BufSize;
            for (Cnt = 0; Cnt < NodeSliceCnt; ++Cnt)
            {
                PathNode = PathNode->_Prev;
            }
            NodeCnt = PathNode->_PrevCnt;
            if (NodeCnt > BufSize)
            {
                int *p = nullptr;
                *p = 0;
            }
        }

        // 패스를 저장한다.
        for (Cnt = 0; Cnt < NodeCnt; ++Cnt)
        {
            XBuf[NodeCnt - Cnt - 1] = PathNode->_X;
            YBuf[NodeCnt - Cnt - 1] = PathNode->_Y;
            PathNode = PathNode->_Prev;
        }

        //ClearList();

        return NodeCnt;
    }

    int CJPS::GetDir(int DirX, int DirY)
    {
        // 배열 이니셜라이져를 가독성 있게 표현하고 싶었으나 클래스 내부에서는 선언할 수 없어 이 함수를 만들었다.
        static const int DirMatrix[3][3] = {
            { DirLU, DirUU,   DirRU },
            { DirLL, DirNone, DirRR },
            { DirLD, DirDD,   DirRD }
        };

        if (DirX < 0 || DirX >= DIR_MATRIX_MAX_X || DirY < 0 || DirY >= DIR_MATRIX_MAX_Y)
            return DirNone;
        return DirMatrix[DirY][DirX];
    }
    bool CJPS::IsWalkableAt(int X, int Y)
    {
        if (X < 0 || X >= _MaxWidth || Y < 0 || Y >= _MaxHeight)
            return false;
        return CheckMapAttributeJPS(X, Y);
    }
    void CJPS::ClearList(void)
    {
        if (false == OpenMultiMap->empty())
        {
            std::multimap<int, st_JPS_NODE *>::iterator OpenMultiMapItr;
            for (OpenMultiMapItr = OpenMultiMap->begin(); OpenMultiMapItr != OpenMultiMap->end(); ++OpenMultiMapItr)
                delete OpenMultiMapItr->second;
            OpenMultiMap->clear();
        }
        if (false == CloseList->empty())
        {
            std::list<st_JPS_NODE *>::iterator CloseListItr;
            for (CloseListItr = CloseList->begin(); CloseListItr != CloseList->end(); ++CloseListItr)
                delete (*CloseListItr);
            CloseList->clear();
        }
    }

    void CJPS::SetStartNode(int StartX, int StartY)
    {
        st_JPS_NODE *StartNode = new st_JPS_NODE;
        StartNode->_X = StartX;
        StartNode->_Y = StartY;
        StartNode->_H = 0;
        StartNode->_G = 0;
        StartNode->_F = StartNode->_H + StartNode->_G;
        StartNode->_PrevCnt = 0;
        StartNode->_Prev = nullptr;

        OpenMultiMap->insert(std::pair<int, st_JPS_NODE *>(StartNode->_F, StartNode));
    }
    void CJPS::SetEndPos(int EndX, int EndY)
    {
        _EndX = EndX;
        _EndY = EndY;
    }

    // 현재 노드의 주변을 탐색.
    void CJPS::NeighborNode(st_JPS_NODE *Node)
    {
        if (nullptr == Node->_Prev)
        {
            // 최초 노드일 때
            // 8방향 체크
            CheckDirection(Node, Node->_X - 1, Node->_Y, DirLL);
            CheckDirection(Node, Node->_X - 1, Node->_Y - 1, DirLU);
            CheckDirection(Node, Node->_X, Node->_Y - 1, DirUU);
            CheckDirection(Node, Node->_X + 1, Node->_Y - 1, DirRU);
            CheckDirection(Node, Node->_X + 1, Node->_Y, DirRR);
            CheckDirection(Node, Node->_X + 1, Node->_Y + 1, DirRD);
            CheckDirection(Node, Node->_X, Node->_Y + 1, DirDD);
            CheckDirection(Node, Node->_X - 1, Node->_Y + 1, DirLD);
        }
        else
        {
            // 기존에 탐색하던 방향을 검색.     // 2 : RR, 0 : LL     // 2 : DD, 0 : UU
            int DirX = (Node->_X - Node->_Prev->_X) / max(abs(Node->_X - Node->_Prev->_X), 1) + 1;
            int DirY = (Node->_Y - Node->_Prev->_Y) / max(abs(Node->_Y - Node->_Prev->_Y), 1) + 1;

            int Dir = GetDir(DirX, DirY);

            // 기존에 탐색하던 방향으로 탐색 진행.
            switch (Dir)
            {
            case DirLL:
                //1. LL
                //기본방향 LL -> CheckDirection(LL)
                //옵션체크 LU, LD -> 막혀있는 곳은 내가 체크한다. 뚫려있는 곳은 이미 체크할 것이므로. -> 나의 위칸이 막혀있으면 CheckDirection(LU), 나의 아래칸이 막혀있으면 CheckDirection(LD)
                CheckDirection(Node, Node->_X - 1, Node->_Y, DirLL);
                if (false == IsWalkableAt(Node->_X, Node->_Y - 1))
                    CheckDirection(Node, Node->_X - 1, Node->_Y - 1, DirLU);
                if (false == IsWalkableAt(Node->_X, Node->_Y + 1))
                    CheckDirection(Node, Node->_X - 1, Node->_Y + 1, DirLD);
                break;
            case DirRR:
                CheckDirection(Node, Node->_X + 1, Node->_Y, DirRR);
                if (false == IsWalkableAt(Node->_X, Node->_Y - 1))
                    CheckDirection(Node, Node->_X + 1, Node->_Y - 1, DirRU);
                if (false == IsWalkableAt(Node->_X, Node->_Y + 1))
                    CheckDirection(Node, Node->_X + 1, Node->_Y + 1, DirRD);
                break;
            case DirUU:
                CheckDirection(Node, Node->_X, Node->_Y - 1, DirUU);
                if (false == IsWalkableAt(Node->_X - 1, Node->_Y))
                    CheckDirection(Node, Node->_X - 1, Node->_Y - 1, DirLU);
                if (false == IsWalkableAt(Node->_X + 1, Node->_Y))
                    CheckDirection(Node, Node->_X + 1, Node->_Y - 1, DirRU);
                break;
            case DirDD:
                CheckDirection(Node, Node->_X, Node->_Y + 1, DirDD);
                if (false == IsWalkableAt(Node->_X - 1, Node->_Y))
                    CheckDirection(Node, Node->_X - 1, Node->_Y + 1, DirLD);
                if (false == IsWalkableAt(Node->_X + 1, Node->_Y))
                    CheckDirection(Node, Node->_X + 1, Node->_Y + 1, DirRD);
                break;
            case DirLU:
                //      [O]
                //   [P][X]
                //[O][X][S]
                //2. LU
                //기본방향 LU, UU, LL
                //옵션체크 RU, LD -> 역시 막혀있는 곳 CheckDirection() 날려줌.
                CheckDirection(Node, Node->_X - 1, Node->_Y - 1, DirLU);
                CheckDirection(Node, Node->_X - 1, Node->_Y, DirLL);
                CheckDirection(Node, Node->_X, Node->_Y - 1, DirUU);
                if (false == IsWalkableAt(Node->_X + 1, Node->_Y))
                    CheckDirection(Node, Node->_X + 1, Node->_Y - 1, DirRU);
                if (false == IsWalkableAt(Node->_X, Node->_Y + 1))
                    CheckDirection(Node, Node->_X - 1, Node->_Y + 1, DirLD);
                break;
            case DirRU:
                //[O]
                //[X][P]
                //[S][X][O]
                //기본방향 RU, UU, RR
                //옵션체크 LU, RD -> 역시 막혀있는 곳 CheckDirection() 날려줌.
                CheckDirection(Node, Node->_X + 1, Node->_Y - 1, DirRU);
                CheckDirection(Node, Node->_X + 1, Node->_Y, DirRR);
                CheckDirection(Node, Node->_X, Node->_Y - 1, DirUU);
                if (false == IsWalkableAt(Node->_X - 1, Node->_Y))
                    CheckDirection(Node, Node->_X - 1, Node->_Y - 1, DirLU);
                if (false == IsWalkableAt(Node->_X, Node->_Y + 1))
                    CheckDirection(Node, Node->_X + 1, Node->_Y + 1, DirRD);
                break;
            case DirRD:
                //[S][X][O]
                //[X][P]
                //[O]
                //기본방향 RD, DD, RR
                //옵션체크 LD, RU -> 역시 막혀있는 곳 CheckDirection() 날려줌.
                CheckDirection(Node, Node->_X + 1, Node->_Y + 1, DirRD);
                CheckDirection(Node, Node->_X + 1, Node->_Y, DirRR);
                CheckDirection(Node, Node->_X, Node->_Y + 1, DirDD);
                if (false == IsWalkableAt(Node->_X - 1, Node->_Y))
                    CheckDirection(Node, Node->_X - 1, Node->_Y + 1, DirLD);
                if (false == IsWalkableAt(Node->_X, Node->_Y - 1))
                    CheckDirection(Node, Node->_X + 1, Node->_Y - 1, DirRU);
                break;
            case DirLD:
                //[O][X][S]
                //   [P][X]
                //      [O]
                //기본방향 LD, DD, LL
                //옵션체크 LU, RD -> 역시 막혀있는 곳 CheckDirection() 날려줌.
                CheckDirection(Node, Node->_X - 1, Node->_Y + 1, DirLD);
                CheckDirection(Node, Node->_X - 1, Node->_Y, DirLL);
                CheckDirection(Node, Node->_X, Node->_Y + 1, DirDD);
                if (false == IsWalkableAt(Node->_X + 1, Node->_Y))
                    CheckDirection(Node, Node->_X + 1, Node->_Y + 1, DirRD);
                if (false == IsWalkableAt(Node->_X, Node->_Y - 1))
                    CheckDirection(Node, Node->_X - 1, Node->_Y - 1, DirLU);
                break;
            default:
                int *p = nullptr;
                *p = 0;
                exit(1);			// 여긴 또 예외처리 어떻게??
                break;
            }


        }
    }

    // 주어진 방향으로 점프.
    void CJPS::CheckDirection(st_JPS_NODE *Node, int X, int Y, int Dir)
    {
        if (false == IsWalkableAt(X, Y))
            return;

        OnCheckDirection(X, Y);

        int JumpX;
        int JumpY;
        int NewG;
        st_JPS_NODE *JumpNode;

        if (false == Jump(X, Y, &JumpX, &JumpY, Dir))
            return;

        //Jump() true 리턴되면
        //G값 새로 계산.
        //오픈 클로즈 체크
        //있으면 바꿔줌.
        //없으면 만들고 오픈리스트에 넣으면 된다.
        NewG = abs(X - JumpX) + abs(Y - JumpY) + Node->_G;
        JumpNode = nullptr;

        // open list 검색
        if (false == OpenMultiMap->empty())
        {
            std::multimap<int, st_JPS_NODE *>::iterator OpenMultiMapItr;
            for (OpenMultiMapItr = OpenMultiMap->begin(); OpenMultiMapItr != OpenMultiMap->end(); ++OpenMultiMapItr)
            {
                if (JumpX == OpenMultiMapItr->second->_X && JumpY == OpenMultiMapItr->second->_Y)
                {
                    JumpNode = OpenMultiMapItr->second;
                    break;
                }
            }

            if (JumpNode != nullptr)
            {
                // Node가 있으면 내것보다 G값이 작으면 건드리지 말아야 한다.
                // G값이 크면 끊어버린다.-> G값을 기준으로 헛짓거리 하고 온 애를 다 끊어버린다.
                if (JumpNode->_G > NewG)
                {
                    JumpNode->_G = NewG;
                    JumpNode->_F = JumpNode->_H + JumpNode->_G;
                    JumpNode->_PrevCnt = Node->_PrevCnt + 1;
                    JumpNode->_Prev = Node;

                    // 키값이 바뀌었으므로 지우고 다시 넣는다.
                    OpenMultiMap->erase(OpenMultiMapItr);
                    OpenMultiMap->insert(std::pair<int, st_JPS_NODE *>(JumpNode->_F, JumpNode));
                }
                return;
            }
        }

        // close list 검색
        if (false == CloseList->empty())
        {
            std::list<st_JPS_NODE *>::iterator CloseListItr;
            for (CloseListItr = CloseList->begin(); CloseListItr != CloseList->end(); ++CloseListItr)
            {
                if (JumpX == (*CloseListItr)->_X && JumpY == (*CloseListItr)->_Y)
                {
                    JumpNode = *CloseListItr;
                    break;
                }
            }

            if (JumpNode != nullptr)
            {
                // Node가 있으면 내것보다 G값이 작으면 건드리지 말아야 한다.
                // G값이 크면 끊어버린다.-> G값을 기준으로 헛짓거리 하고 온 애를 다 끊어버린다.
                if (JumpNode->_G > NewG)
                {
                    JumpNode->_G = NewG;
                    JumpNode->_F = JumpNode->_H + JumpNode->_G;
                    JumpNode->_PrevCnt = Node->_PrevCnt + 1;
                    JumpNode->_Prev = Node;
                }
                return;
            }
        }

        // 없으면 만들고 오픈리스트에 넣으면 된다.
        JumpNode = new st_JPS_NODE;
        JumpNode->_X = JumpX;
        JumpNode->_Y = JumpY;
        JumpNode->_H = abs(_EndX - JumpNode->_X) + abs(_EndY - JumpNode->_Y);
        JumpNode->_G = NewG;
        JumpNode->_F = JumpNode->_H + JumpNode->_G;
        JumpNode->_PrevCnt = Node->_PrevCnt + 1;
        JumpNode->_Prev = Node;

        OpenMultiMap->insert(std::pair<int, st_JPS_NODE *>(JumpNode->_F, JumpNode));
    }

    // 벽이나 코너 또는 목적지가 나올때까지 계속 진행.
    bool CJPS::Jump(int StartX, int StartY, int *JumpX, int *JumpY, int Dir)
    {
        int X;
        int Y;
        int LoopCnt;
        int LoopMax;

        X = StartX;
        Y = StartY;

        LoopMax = max(_MaxWidth, _MaxHeight);       // 루트 최대값을 구한다.
        for (LoopCnt = 0; LoopCnt < LoopMax; ++LoopCnt)
        {
            if (false == IsWalkableAt(X, Y))
                return false;

            OnJump(X, Y);

            // 목적지에 다다랐다.
            if (X == _EndX && Y == _EndY)
            {
                *JumpX = X;
                *JumpY = Y;
                return true;
            }

            // 8방향 코너체크
            switch (Dir)
            {
                //->먼저 직선 4방향. ex) 오른쪽으로 진행할 때 위쪽이나 아래쪽에 벽이 있고 그 다음이 뚫려있을 때 현재 좌표넣고 리턴 true
                //->아래방향(!isWalkable(x - 1, y) && isWalkable(x - 1, y + 1) || (!isWalkable(x + 1, y) && isWalkable(x + 1, y + 1))
                //->노드 생성 좌표가 없다면 Jump 재귀호출.
            case DirLL:
                //[O][X]
                //   [P]
                //[O][X]
                if ((true == IsWalkableAt(X - 1, Y - 1) && false == IsWalkableAt(X, Y - 1)) ||
                    (true == IsWalkableAt(X - 1, Y + 1) && false == IsWalkableAt(X, Y + 1)))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                //return Jump(X - 1, Y, JumpPos, Dir);

                X -= 1;
                continue;

            case DirRR:
                if ((true == IsWalkableAt(X + 1, Y - 1) && false == IsWalkableAt(X, Y - 1)) ||
                    (true == IsWalkableAt(X + 1, Y + 1) && false == IsWalkableAt(X, Y + 1)))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                //return Jump(X + 1, Y, JumpPos, Dir);

                X += 1;
                continue;

            case DirUU:
                //[O]   [O]
                //[X][P][X]
                if ((true == IsWalkableAt(X - 1, Y - 1) && false == IsWalkableAt(X - 1, Y)) ||
                    (true == IsWalkableAt(X + 1, Y - 1) && false == IsWalkableAt(X + 1, Y)))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                //return Jump(X, Y - 1, JumpPos, Dir);

                Y -= 1;
                continue;

            case DirDD:
                if ((true == IsWalkableAt(X - 1, Y + 1) && false == IsWalkableAt(X - 1, Y)) ||
                    (true == IsWalkableAt(X + 1, Y + 1) && false == IsWalkableAt(X + 1, Y)))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                //return Jump(X, Y + 1, JumpPos, Dir);

                Y += 1;
                continue;

                //-> 대각선 체크
                //      [O]
                //   [P][X]
                //[O][X][S]
                //좌상단으로 진행할 때(P가 현재 좌표)
                //-> 아래방향 (!isWalkable(x + 1, y) && isWalkable(x + 1, y - 1) || (!isWalkable(x, y + 1) && isWalkable(x - 1, y + 1))
                //
                //->
                //// 직성방향과는 달리 조건 하나 더 들어감. -> 내 좌표를 덮어씌움. -> 멀리있는 꺽임을 미리 잡아주기 위함.
                //if (점프 왼쪽 방향 || 점프 위쪽 방향)
                //{
                //내 좌표를 덮어씌운다.
                //}
                //
                //-> 좌상단으로 재귀 호출
            case DirLU:
                //      [O]
                //   [P][X]
                //[O][X][S]
                if ((true == IsWalkableAt(X - 1, Y + 1) && false == IsWalkableAt(X, Y + 1)) ||
                    (true == IsWalkableAt(X + 1, Y - 1) && false == IsWalkableAt(X + 1, Y)))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                // 직성방향과는 달리 조건 하나 더 들어감. -> 내 좌표를 덮어씌움. -> 멀리있는 꺽임을 미리 잡아주기 위함.
                //if (점프 왼쪽 방향 || 점프 위쪽 방향)
                //{
                //내 좌표를 덮어씌운다.
                //}
                if (true == Jump(X - 1, Y, JumpX, JumpY, DirLL) || true == Jump(X, Y - 1, JumpX, JumpY, DirUU))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                //return Jump(X - 1, Y - 1, JumpPos, Dir);

                X -= 1;
                Y -= 1;
                continue;

            case DirRU:
                //[O]
                //[X][P]
                //[S][X][O]
                if ((true == IsWalkableAt(X + 1, Y + 1) && false == IsWalkableAt(X, Y + 1)) ||
                    (true == IsWalkableAt(X - 1, Y - 1) && false == IsWalkableAt(X - 1, Y)))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                if (true == Jump(X + 1, Y, JumpX, JumpY, DirRR) || true == Jump(X, Y - 1, JumpX, JumpY, DirUU))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                //return Jump(X + 1, Y - 1, JumpPos, Dir);

                X += 1;
                Y -= 1;
                continue;

            case DirRD:
                //[S][X][O]
                //[X][P]
                //[O]
                if ((true == IsWalkableAt(X + 1, Y - 1) && false == IsWalkableAt(X, Y - 1)) ||
                    (true == IsWalkableAt(X - 1, Y + 1) && false == IsWalkableAt(X - 1, Y)))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                if (true == Jump(X + 1, Y, JumpX, JumpY, DirRR) || true == Jump(X, Y + 1, JumpX, JumpY, DirDD))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                //return Jump(X + 1, Y + 1, JumpPos, Dir);

                X += 1;
                Y += 1;
                continue;

            case DirLD:
                //[O][X][S]
                //   [P][X]
                //      [O]
                if ((true == IsWalkableAt(X - 1, Y - 1) && false == IsWalkableAt(X, Y - 1)) ||
                    (true == IsWalkableAt(X + 1, Y + 1) && false == IsWalkableAt(X + 1, Y)))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                if (true == Jump(X - 1, Y, JumpX, JumpY, DirLL) || true == Jump(X, Y + 1, JumpX, JumpY, DirDD))
                {
                    *JumpX = X;
                    *JumpY = Y;
                    return true;
                }
                //return Jump(X - 1, Y + 1, JumpPos, Dir);

                X -= 1;
                Y += 1;
                continue;

            default:
                int *p = nullptr;
                *p = 0;
                exit(1);			// 여긴 또 예외처리 어떻게??
                //break;
                return false;
            }
        }// end of for

        // 이건 맵의 범위를 초과한 경우이다.
        int *p = nullptr;
        *p = 0;
        return false;
    }
}