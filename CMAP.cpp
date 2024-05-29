#include "LibHeader.h"

namespace MonLib
{
    CMAP::CMAP(void)
    {
        _Map = nullptr;
        _Width = 0;
        _Height = 0;
    }
    CMAP::~CMAP(void)
    {
        ReleaseMap();
    }
    bool CMAP::LoadMap(const WCHAR *p_FileName)
    {
        if (_Map != nullptr)
            return false;

        bool IsBOM;
        int *p_BOMCode;
        char *p_FileBuf;

        // 파일을 불러온다.
        p_FileBuf = new char[MAP_FILE_SIZE_MAX];
        if (false == LoadFile(p_FileName, p_FileBuf, MAP_FILE_SIZE_MAX))
        {
            delete[] p_FileBuf;
            return false;
        }

        // BOM Check
        IsBOM = false;
        p_BOMCode = (int *)p_FileBuf;
        if (0x00bfbbef == ((*p_BOMCode) & 0x00bfbbef))      // 앞의 3바이트만 비교
            IsBOM = true;
        else
            IsBOM = false;

        int BufSize;
        char *p_Buf;
        int FileMapWidth;
        int FileMapHeight;

        //int IndexY;
        //int IndexX;

        BufSize = strlen(p_FileBuf);      // 위에서 체크했으므로 그대로 받는다.
        p_Buf = p_FileBuf;
        if (true == IsBOM)
        {
            BufSize -= 3;
            p_Buf += 3;
        }

        // 파일 데이터를 파싱한다.(개행문자와 데이터 이외에는 전부 널문자 삽입.)
        if (false == ParseMapFileData(p_Buf, BufSize, &FileMapWidth, &FileMapHeight))
        {
            delete[] p_FileBuf;
            return false;
        }

        //_Map = new MAP_ATTRIBUTE *[Height];
        //for (IndexY = 0; IndexY < Height; ++IndexY)
        //{
        //    _Map[IndexY] = new MAP_ATTRIBUTE[Width];
        //    for (IndexX = 0; IndexX < Width; ++IndexX)
        //    {
        //        _Map[IndexY][IndexX] = MAP_ATTRIBUTE_WALL;
        //    }
        //    //memset(_Map[IndexY], MAP_ATTRIBUTE_WALL, Width * sizeof(MAP_ATTRIBUTE));      // 1바이트씩 밀기 때문에 0이 아니면 다른값이 나올 수 있다.
        //}
        //
        //if (false == SetMapFileData(p_Buf, BufSize, Width, Height, _Map))
        //{
        //    ReleaseMap();
        //    delete[] p_FileBuf;
        //    return false;
        //}

        // 맵을 생성한다.
        //if (false == CreateMap(p_Buf, BufSize, FileMapWidth, FileMapHeight))
        //{
        //    ReleaseMap();
        //    delete[] p_FileBuf;
        //    return false;
        //}

        bool IsLoad = true;

        if (false == CreateExtensionMap(p_Buf, BufSize, FileMapWidth, FileMapHeight, MAP_EXTENSION_SIZE))
        {
            ReleaseMap();
            IsLoad = false;
        }
        delete[] p_FileBuf;

        return IsLoad;
    }

    bool CMAP::CreateExtensionMap(char *p_FileBuf, int FileBufSize, int Width, int Height, const int ExtensionMapSize)
    {
        //const int ExtensionMapSize = 2;     // 가로세로로 확장할 비율.(현재는 가로와 세로가 같은 비율로 증가한다.)

        int MapWidth;
        int MapHeight;
        int IndexX;
        int IndexY;
        int BufIndex;
        int TempIncrementX;
        int TempIncrementY;

        MapWidth = Width * ExtensionMapSize;
        MapHeight = Height * ExtensionMapSize;

        if (MapWidth < MAP_WIDTH_MIN || MapWidth > MAP_WIDTH_MAX || MapHeight < MAP_HEIGHT_MIN || MapHeight > MAP_HEIGHT_MAX)
            return false;

        _Map = new MAP_ATTRIBUTE *[MapHeight];
        for (IndexY = 0; IndexY < MapHeight; ++IndexY)
        {
            _Map[IndexY] = new MAP_ATTRIBUTE[MapWidth];
            for (IndexX = 0; IndexX < MapWidth; ++IndexX)
            {
                _Map[IndexY][IndexX] = MAP_ATTRIBUTE_WALL;
            }
            //memset(_Map[IndexY], MAP_ATTRIBUTE_WALL, Width * sizeof(MAP_ATTRIBUTE));      // 1바이트씩 밀기 때문에 0이 아니면 다른값이 나올 수 있다.
        }

        IndexX = 0;
        IndexY = 0;
        //TempIncrementX = 0;
        //TempIncrementY = 0;
        for (BufIndex = 0; BufIndex < FileBufSize; ++BufIndex)
        {
            if (p_FileBuf[BufIndex] != '\0')
            {
                if ('\n' == p_FileBuf[BufIndex])
                {
                    IndexX = 0;
                    IndexY += ExtensionMapSize;
                }
                else
                {
                    if (' ' == p_FileBuf[BufIndex])
                    {
                        for (TempIncrementY = 0; TempIncrementY < ExtensionMapSize; ++TempIncrementY)
                        {
                            for (TempIncrementX = 0; TempIncrementX < ExtensionMapSize; ++TempIncrementX)
                            {
                                _Map[IndexY + TempIncrementY][IndexX + TempIncrementX] = MAP_ATTRIBUTE_PATH;
                            }
                        }
                        //_Map[IndexY][IndexX] = MAP_ATTRIBUTE_PATH;
                    }
                    else if ('X' == p_FileBuf[BufIndex])
                    {
                        for (TempIncrementY = 0; TempIncrementY < ExtensionMapSize; ++TempIncrementY)
                        {
                            for (TempIncrementX = 0; TempIncrementX < ExtensionMapSize; ++TempIncrementX)
                            {
                                _Map[IndexY + TempIncrementY][IndexX + TempIncrementX] = MAP_ATTRIBUTE_WALL;
                            }
                        }
                        //_Map[IndexY][IndexX] = MAP_ATTRIBUTE_WALL;
                    }
                    else
                    {
                        return false;
                    }
                    IndexX += ExtensionMapSize;
                }
            }
        }// for end

        _Width = MapWidth;
        _Height = MapHeight;

        return true;
    }
    bool CMAP::CreateMap(char *p_FileBuf, int FileBufSize, int Width, int Height)
    {
        if (Width < MAP_WIDTH_MIN || Width > MAP_WIDTH_MAX || Height < MAP_HEIGHT_MIN || Height > MAP_HEIGHT_MAX)
            return false;

        int MapWidth;
        int MapHeight;
        int IndexX;
        int IndexY;
        int BufIndex;

        MapWidth = Width;
        MapHeight = Height;

        _Map = new MAP_ATTRIBUTE *[Height];
        for (IndexY = 0; IndexY < Height; ++IndexY)
        {
            _Map[IndexY] = new MAP_ATTRIBUTE[Width];
            for (IndexX = 0; IndexX < Width; ++IndexX)
            {
                _Map[IndexY][IndexX] = MAP_ATTRIBUTE_WALL;
            }
            //memset(_Map[IndexY], MAP_ATTRIBUTE_WALL, Width * sizeof(MAP_ATTRIBUTE));      // 1바이트씩 밀기 때문에 0이 아니면 다른값이 나올 수 있다.
        }

        IndexX = 0;
        IndexY = 0;
        for (BufIndex = 0; BufIndex < FileBufSize; ++BufIndex)
        {
            if (p_FileBuf[BufIndex] != '\0')
            {
                if ('\n' == p_FileBuf[BufIndex])
                {
                    IndexX = 0;
                    IndexY++;
                }
                else
                {
                    if (' ' == p_FileBuf[BufIndex])
                    {
                        _Map[IndexY][IndexX] = MAP_ATTRIBUTE_PATH;
                    }
                    else if ('X' == p_FileBuf[BufIndex])
                    {
                        _Map[IndexY][IndexX] = MAP_ATTRIBUTE_WALL;
                    }
                    else
                    {
                        return false;
                    }
                    IndexX++;
                }
            }
        }// for end

        _Width = Width;
        _Height = Height;

        return true;
    }
    //bool CMAP::SetMapFileData(char *p_FileBuf, int FileBufSize, int Width, int Height, MAP_ATTRIBUTE **pp_Map)
    //{
    //    int MapWidth;
    //    int MapHeight;
    //
    //    int IndexX;
    //    int IndexY;
    //    int BufIndex;
    //
    //    MapWidth = Width;
    //    MapHeight = Height;
    //
    //    if (MapWidth < MAP_WIDTH_MIN || MapWidth > MAP_WIDTH_MAX || MapHeight < MAP_HEIGHT_MIN || MapHeight > MAP_HEIGHT_MAX)
    //    {
    //        return false;
    //    }
    //
    //    IndexX = 0;
    //    IndexY = 0;
    //    for (BufIndex = 0; BufIndex < FileBufSize; ++BufIndex)
    //    {
    //        if (p_FileBuf[BufIndex] != '\0')
    //        {
    //            if ('\n' == p_FileBuf[BufIndex])
    //            {
    //                IndexX = 0;
    //                IndexY++;
    //            }
    //            else
    //            {
    //                if (' ' == p_FileBuf[BufIndex])
    //                {
    //                    pp_Map[IndexY][IndexX] = MAP_ATTRIBUTE_PATH;
    //                }
    //                else if ('X' == p_FileBuf[BufIndex])
    //                {
    //                    pp_Map[IndexY][IndexX] = MAP_ATTRIBUTE_WALL;
    //                }
    //                else
    //                {
    //                    return false;
    //                }
    //                IndexX++;
    //            }
    //        }
    //    }// for end
    //
    //    return true;
    //}
    void CMAP::ReleaseMap(void)
    {
        int IndexY;
        if (_Map != nullptr)
        {
            for (IndexY = 0; IndexY < _Height; ++IndexY)
            {
                delete[] _Map[IndexY];      // X좌표 삭제
            }
            delete[]_Map;
            _Map = nullptr;
        }
    }

    MAP_ATTRIBUTE CMAP::GetAttribute(int PosX, int PosY)
    {
        if (nullptr == _Map)
            return MAP_ATTRIBUTE_DEFAULT;

        int MapPosX;
        int MapPosY;

        MapPosX = PosX - MAP_POS_X_START;
        MapPosY = PosY - MAP_POS_Y_START;

        if (MapPosX < 0 || MapPosX >= _Width || MapPosY < 0 || MapPosY >= _Height)
            return MAP_ATTRIBUTE_DEFAULT;

        return _Map[MapPosY][MapPosX];
    }
    bool CMAP::SetAttribute(int PosX, int PosY, MAP_ATTRIBUTE MapAttribute)
    {
        if (nullptr == _Map)
            return false;

        int MapPosX;
        int MapPosY;

        MapPosX = PosX - MAP_POS_X_START;
        MapPosY = PosY - MAP_POS_Y_START;

        if (MapPosX < 0 || MapPosX >= _Width || MapPosY < 0 || MapPosY >= _Height)
            return false;

        _Map[MapPosY][MapPosX] = MapAttribute;
        return true;
    }

    int CMAP::GetWidth(void)
    {
        return _Width;
    }
    int CMAP::GetHeight(void)
    {
        return _Height;
    }
}