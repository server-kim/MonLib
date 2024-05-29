#ifndef __PARSER_HEADER__
#define __PARSER_HEADER__

class Parser
{
public:
    enum ParserDefine
    {
        FILE_SIZE_MIN = 10,
        FILE_SIZE_MAX = 512000,
        WORD_SIZE = 256
    };
private:
    int _FileSize;
    char *_p_FileBuff;

    char *_p_ProvideStart;
    char *_p_ProvideEnd;
    char *_p_ProvideCur;

    void Initial(void);
    void Release(void);
public:
    Parser(void);
    virtual ~Parser(void);

    bool Parser_LoadFile(const char *p_FileName);
    void Parser_Initial(void);
    void Parser_Release(void);
    bool Parser_ProvideArea(char *p_AreaName);

    bool Parser_SkipNoneCommand(void);
    bool Parser_GetNextWord(char **pp_Buff, int *p_Length);
    bool Parser_GetStringWord(char **pp_Buff, int *p_Length);

    bool Parser_GetValue_int(char *p_Name, int *p_Value);
    bool Parser_GetValue_float(char *p_Name, float *p_Value);
    bool Parser_GetValue_string(char *p_Name, char *p_Value);
};

#endif