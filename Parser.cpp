#include "LibHeader.h"

Parser::Parser(void)
{
    _FileSize = 0;
    _p_FileBuff = nullptr;
    _p_ProvideStart = nullptr;
    _p_ProvideEnd = nullptr;
    _p_ProvideCur = nullptr;

    Initial();
}
Parser::~Parser(void)
{
    Release();
}
void Parser::Initial(void)
{
    if (nullptr == _p_FileBuff)
        _p_FileBuff = new char[FILE_SIZE_MAX];
    //memset(_p_FileBuff, 0, FILE_SIZE_MAX);
}
void Parser::Release(void)
{
    if (_p_FileBuff != nullptr)
        delete[] _p_FileBuff;
    _p_FileBuff = nullptr;
}

bool Parser::Parser_LoadFile(const char *p_FileName)
{
    bool isLoad;
    FILE *p_File;
    int Error;
    int FileTotalSize;

    FileTotalSize = 0;
    Error = fopen_s(&p_File, p_FileName, "rb");
    if (Error != 0)
        return false;

    fseek(p_File, 0, SEEK_END);
    FileTotalSize = ftell(p_File);
    fseek(p_File, 0, SEEK_SET);

    isLoad = false;
    if (FileTotalSize > FILE_SIZE_MIN && FileTotalSize <= FILE_SIZE_MAX)
    {
        //memset(_p_FileBuff, 0, FILE_SIZE_MAX);
        fread_s(_p_FileBuff, FILE_SIZE_MAX, FileTotalSize, 1, p_File);
        isLoad = true;
        _FileSize = FileTotalSize;
    }
    fclose(p_File);
    return isLoad;
}
void Parser::Parser_Initial(void)
{
    _p_ProvideCur = _p_ProvideStart;
}
void Parser::Parser_Release(void)
{
    _FileSize = 0;
    //memset(_p_FileBuff, 0, FILE_SIZE_MAX + 1);
    //Release();

    _p_ProvideStart = nullptr;
    _p_ProvideEnd = nullptr;
    _p_ProvideCur = nullptr;
}
bool Parser::Parser_ProvideArea(char *p_AreaName)
{
    if (0 == _FileSize) return false;       // 일단 여기만 체크

    bool isFindArea = false;
    bool isString = false;
    char *p_SearchBegin = nullptr;
    char *p_SearchPos = nullptr;
    int AreaNameLength = 0;
    char AreaNameBuff[WORD_SIZE + 1];

    p_SearchPos = _p_FileBuff;
    while (false == isFindArea)
    {
        // area name buff 초기화
        AreaNameLength = 0;
        memset(AreaNameBuff, 0, WORD_SIZE + 1);

        // area name 시작 찾기
        while (p_SearchPos != &_p_FileBuff[_FileSize - 1])
        {
            if (':' == *p_SearchPos)
                break;
            else
                p_SearchPos++;
        }
        if (p_SearchPos == &_p_FileBuff[_FileSize - 1])
            return false;

        /*
        while (':' != *p_SearchPos)
        {
            p_SearchPos++;
            if (&_p_FileBuff[_FileSize - 1] == p_SearchPos)
                return false;
        }

        // 콜론이 파일의 끝에 있을 때 체크
        if (&_p_FileBuff[_FileSize - 1] == (p_SearchPos + 1))
            return false;
        p_SearchPos++;
        //*/

        // ':' 다음부터 검색 시작 지점 설정
        p_SearchBegin = ++p_SearchPos;

        // area name 찾기
        while (p_SearchPos != &_p_FileBuff[_FileSize - 1])
        {
            if ('{' == *p_SearchPos
                || 0x20 == *p_SearchPos || 0x09 == *p_SearchPos || 0x0a == *p_SearchPos || 0x0d == *p_SearchPos
                || ',' == *p_SearchPos || '.' == *p_SearchPos)
            {
                break;
            }
            else
            {
                p_SearchPos++;
                AreaNameLength++;
            }
        }
        if (p_SearchPos == &_p_FileBuff[_FileSize - 1])
            return false;
        if (AreaNameLength > WORD_SIZE)
            return false;
        memcpy_s(AreaNameBuff, WORD_SIZE, p_SearchBegin, AreaNameLength);

        /*
        // area name 찾기
        while ('{' != *(p_SearchPos + AreaNameBuffLength))
        {
            if (&_p_FileBuff[_FileSize - 1] == p_SearchPos + AreaNameBuffLength)
                return false;

            if (0x20 == *(p_SearchPos + AreaNameBuffLength)
                || 0x09 == *(p_SearchPos + AreaNameBuffLength)
                || 0x0a == *(p_SearchPos + AreaNameBuffLength)
                || 0x0d == *(p_SearchPos + AreaNameBuffLength)
                || ',' == *(p_SearchPos + AreaNameBuffLength)
                || '.' == *(p_SearchPos + AreaNameBuffLength))
                break;
            AreaNameBuffLength++;
        }
        memcpy_s(AreaNameBuff, WORD_SIZE, p_SearchPos, AreaNameBuffLength);
        p_SearchPos += AreaNameBuffLength;
        */

        // area name check
        if (0 == strcmp(AreaNameBuff, p_AreaName))
        {
            isFindArea = true;
            break;
        }   
    }

    if (false == isFindArea)
        return false;

    // area { 체크
    while (p_SearchPos != &_p_FileBuff[_FileSize - 1])
    {
        if ('{' == *p_SearchPos && false == isString)
        {
            break;
        }
        else if ('"' == *p_SearchPos)
        {
            if (false == isString)
                isString = true;
            else
                isString = false;
        }
        else
            p_SearchPos++;
    }
    if (p_SearchPos == &_p_FileBuff[_FileSize - 1])
        return false;
    _p_ProvideStart = p_SearchPos;
    _p_ProvideCur = p_SearchPos;

    /*
    // area { 체크
    while ('{' != *p_SearchPos || true == isString)
    {
        if ('"' == *p_SearchPos)
        {
            if (false == isString)
                isString = true;
            else
                isString = false;
        }

        if (p_SearchPos == &_p_FileBuff[_FileSize - 1])
            return false;
        p_SearchPos++;
    }
    _p_ProvideStart = p_SearchPos;
    _p_ProvideCur = p_SearchPos;
    */

    // area } 체크
    while (p_SearchPos != &_p_FileBuff[_FileSize - 1])
    {
        if ('}' == *p_SearchPos && false == isString)
        {
            break;
        }
        else if ('"' == *p_SearchPos)
        {
            if (false == isString)
                isString = true;
            else
                isString = false;
        }
        p_SearchPos++;
    }
    if (p_SearchPos == &_p_FileBuff[_FileSize - 1] && *p_SearchPos != '}')
        return false;
    _p_ProvideEnd = p_SearchPos;
    return true;

    /*
    while ('}' != *p_SearchPos || true == isString)
    {
        if (p_SearchPos == &_p_FileBuff[_FileSize - 1])
            return false;

        if ('"' == *p_SearchPos)
        {
            if (false == isString)
                isString = true;
            else
                isString = false;
        }

        p_SearchPos++;
    }
    _p_ProvideEnd = p_SearchPos;
    return true;
    */
}

bool Parser::Parser_SkipNoneCommand(void)
{
    while (_p_ProvideCur != _p_ProvideEnd)
    {
        if (0x20 == *_p_ProvideCur || 0x09 == *_p_ProvideCur || 0x0a == *_p_ProvideCur || 0x0d == *_p_ProvideCur
            || ',' == *_p_ProvideCur || '.' == *_p_ProvideCur)
            _p_ProvideCur++;
        else
            return true;
    }
    return false;		// 파일 끝에 다다르면 false
}
bool Parser::Parser_GetNextWord(char **pp_Buff, int *p_Length)
{
    if (false == Parser_SkipNoneCommand())	return false;

    *pp_Buff = _p_ProvideCur;
    *p_Length = 0;
    while (_p_ProvideCur != _p_ProvideEnd)
    {
        if (0x20 == *_p_ProvideCur || 0x09 == *_p_ProvideCur || 0x0a == *_p_ProvideCur || 0x0d == *_p_ProvideCur
            || ',' == *_p_ProvideCur || '"' == *_p_ProvideCur)// '.' == *chpFileBuff ||
            break;
        else
        {
            (*p_Length)++;
            _p_ProvideCur++;
        }
    }

    if (*p_Length > 0 && *p_Length <= WORD_SIZE)
        return true;
    else
        return false;
}
bool Parser::Parser_GetStringWord(char **pp_Buff, int *p_Length)
{
    if (false == Parser_SkipNoneCommand())	return false;

    // 처음 " 체크
    if ('"' != *_p_ProvideCur || (_p_ProvideCur + 1) == _p_ProvideEnd)
    {
        //printf("Parser_GetStringWord :: 스트링 기호의 시작이 파일의 끝에 있거나 존재하지 않습니다.\n");
        return false;
    }
    else
        _p_ProvideCur++;

    // 나중 " 체크
    *pp_Buff = _p_ProvideCur;
    *p_Length = 0;
    while (_p_ProvideCur != _p_ProvideEnd)
    {
        if ('"' == *_p_ProvideCur)
        {
            if (*p_Length > 0 && *p_Length <= WORD_SIZE)
                return true;
            else
            {
                //printf("Parser_GetStringWord :: 스트링 기호의 끝까지 읽었으나 빈 스트링입니다.\n");
                return false;
            }
        }
        _p_ProvideCur++;
        (*p_Length)++;
    }
    //printf("Parser_GetStringWord :: 스트링 기호의 끝을 알 수 없습니다.\n");
    return false;
}

bool Parser::Parser_GetValue_int(char *p_Name, int *p_Value)
{
    int Length;
    char *p_Buff;
    char Word[WORD_SIZE + 1];

    //Word[WORD_SIZE] = '\0';
    while (true == Parser_GetNextWord(&p_Buff, &Length))
    {
        //memset(Word, 0, WORD_SIZE);
        memcpy(Word, p_Buff, Length);
        //Word[Length - 1] = '\0';
        //if (0 == strcmp(p_Name, Word))
        if (Length == strlen(p_Name) && 0 == memcmp(p_Name, Word, Length))
        {
            if (true == Parser_GetNextWord(&p_Buff, &Length))
            {
                //memset(Word, 0, WORD_SIZE);
                memcpy(Word, p_Buff, Length);
                //Word[Length - 1] = '\0';
                //if (0 == strcmp("=", Word))
                if (Length == 1 && 0 == memcmp("=", Word, Length))
                {
                    if (true == Parser_GetNextWord(&p_Buff, &Length))
                    {
                        // 실제로 원하는 밸류값이 나온다.
                        //memset(Word, 0, WORD_SIZE);
                        memcpy(Word, p_Buff, Length);
                        Word[Length] = '\0';
                        *p_Value = atoi(Word);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}
bool Parser::Parser_GetValue_float(char *p_Name, float *p_Value)
{
    int Length;
    char *p_Buff;
    char Word[WORD_SIZE + 1];

    while (true == Parser_GetNextWord(&p_Buff, &Length))
    {
        memcpy(Word, p_Buff, Length);
        if (Length == strlen(p_Name) && 0 == memcmp(p_Name, Word, Length))
        {
            if (true == Parser_GetNextWord(&p_Buff, &Length))
            {
                memcpy(Word, p_Buff, Length);
                if (Length == 1 && 0 == memcmp("=", Word, Length))
                {
                    if (true == Parser_GetNextWord(&p_Buff, &Length))
                    {
                        // 실제로 원하는 밸류값이 나온다.
                        memcpy(Word, p_Buff, Length);
                        Word[Length] = '\0';
                        *p_Value = atof(Word);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}
bool Parser::Parser_GetValue_string(char *p_Name, char *p_Value)
{
    int Length;
    char *p_Buff;
    char Word[WORD_SIZE + 1];

    while (true == Parser_GetNextWord(&p_Buff, &Length))
    {
        memcpy(Word, p_Buff, Length);
        if (Length == strlen(p_Name) && 0 == memcmp(p_Name, Word, Length))
        {
            if (true == Parser_GetNextWord(&p_Buff, &Length))
            {
                memcpy(Word, p_Buff, Length);
                if (Length == 1 && 0 == memcmp("=", Word, Length))
                {
                    if (true == Parser_GetStringWord(&p_Buff, &Length))
                    {
                        // 실제로 원하는 밸류값이 나온다.
                        memcpy(p_Value, p_Buff, Length);
                        p_Value[Length] = '\0';
                        return true;
                    }
                }
            }
        }
    }
    return false;
}