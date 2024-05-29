#ifndef __MAP_HEADER__
#define __MAP_HEADER__

namespace MonLib
{
    typedef int MAP_ATTRIBUTE;

    enum MAP_ATTRIBUTE_DEFINE : MAP_ATTRIBUTE
    {
        MAP_ATTRIBUTE_DEFAULT = 0,      // 디폴트값.
        MAP_ATTRIBUTE_WALL,             // 벽.
        MAP_ATTRIBUTE_PATH              // 갈수있는 길.
    };

    class CMAP
    {
    private:
        enum MAP_MAX_DEFINE
        {
            MAP_EXTENSION_SIZE = 2,         // 맵을 가로세로로 확장할 배율

            MAP_POS_X_START = 0,            // X좌표 시작값
            MAP_POS_Y_START = 0,            // Y좌표 시작값

            MAP_WIDTH_MIN = 6,              // 맵 너비 최소값(3x3 타일 기준으로 2배한 값 -> 6x6)
            MAP_WIDTH_MAX = 50000,          // 맵 너비 최대값

            MAP_HEIGHT_MIN = 6,             // 맵 높이 최소값
            MAP_HEIGHT_MAX = 50000,         // 맵 높이 최대값

            MAP_FILE_SIZE_MIN = 40,         // 맵파일 최소사이즈
            MAP_FILE_SIZE_MAX = 512000      // 맵파일 최대사이즈
        };

        MAP_ATTRIBUTE **_Map;
        int _Width;
        int _Height;

    public:
        CMAP(void);
        virtual ~CMAP(void);

        bool LoadMap(const WCHAR *p_FileName);

        MAP_ATTRIBUTE GetAttribute(int PosX, int PosY);
        bool SetAttribute(int PosX, int PosY, MAP_ATTRIBUTE MapAttribute);

        int GetWidth(void);
        int GetHeight(void);
        //MAP_ATTRIBUTE **GetMap(void);

    private:
        bool CreateExtensionMap(char *p_FileBuf, int FileBufSize, int Width, int Height, const int ExtensionMapSize);
        bool CreateMap(char *p_FileBuf, int FileBufSize, int Width, int Height);
        //bool SetMapFileData(char *p_FileBuf, int FileBufSize, int Width, int Height, MAP_ATTRIBUTE **pp_Map);

        void ReleaseMap(void);
    };
}

#endif