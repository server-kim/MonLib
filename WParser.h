#ifndef __WCHAR_PARSER_HEADER__
#define __WCHAR_PARSER_HEADER__

//----------------------------------------------
// Wide Character Parser
//
// 주의사항 : 리틀 엔디언에서 올바르게 동작한다.
//
//----------------------------------------------

namespace MonLib
{
    class WParser
    {
    public:
        enum ParserDefine
        {
            FILE_SIZE_MIN = 10,
            FILE_SIZE_MAX = 512000,
            WORD_SIZE = 256,

            COMMENT_NONE = 0,           // 코멘트가 아님
            COMMENT_LINE = 1,           // "//"로 시작하는 코멘트
            COMMENT_AREA = 2            // "/**/"안에 있는 코멘트
        };
    private:
        int _FileSize;
        WCHAR *_p_FileBuff;

        WCHAR *_p_ProvideStart;
        WCHAR *_p_ProvideEnd;
        WCHAR *_p_ProvideCur;

        int _CommentLevel;

        void Initial(void);
        void Release(void);
    public:
        WParser(void);
        virtual ~WParser(void);

        bool Parser_LoadFile(const WCHAR *p_FileName);
        void Parser_Initial(void);
        void Parser_Release(void);
        bool Parser_ProvideArea(WCHAR *p_AreaName);

        bool Parser_CommentPass(WCHAR **pp_CurPos, WCHAR *p_EndPos);
        bool Parser_SkipNoneCommand(void);
        bool Parser_GetNextWord(WCHAR **pp_Buff, int *p_Length);
        bool Parser_GetStringWord(WCHAR **pp_Buff, int *p_Length);

        bool Parser_GetValue_int(WCHAR *p_Name, int *p_Value);
        bool Parser_GetValue_float(WCHAR *p_Name, float *p_Value);
        bool Parser_GetValue_string(WCHAR *p_Name, WCHAR *p_Value);
    };
}

#endif