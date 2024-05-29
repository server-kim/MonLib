#include "LibHeader.h"

namespace MonLib
{
    WParser::WParser(void)
    {
        _FileSize = 0;
        _p_FileBuff = nullptr;
        _p_ProvideStart = nullptr;
        _p_ProvideEnd = nullptr;
        _p_ProvideCur = nullptr;
        _CommentLevel = COMMENT_NONE;

        Initial();
    }
    WParser::~WParser(void)
    {
        Release();
    }
    void WParser::Initial(void)
    {
        if (nullptr == _p_FileBuff)
            _p_FileBuff = new WCHAR[FILE_SIZE_MAX];
    }
    void WParser::Release(void)
    {
        if (_p_FileBuff != nullptr)
            delete[] _p_FileBuff;
        _p_FileBuff = nullptr;
    }

    bool WParser::Parser_LoadFile(const WCHAR *p_FileName)
    {
        bool isLoad;
        FILE *p_File;
        int Error;
        int FileTotalSize;

        FileTotalSize = 0;
        Error = _wfopen_s(&p_File, p_FileName, L"r, ccs=UTF-8");
        if (Error != 0)
            return false;

        fseek(p_File, 0, SEEK_END);
        FileTotalSize = ftell(p_File);
        fseek(p_File, 0, SEEK_SET);

        isLoad = false;
        if (FileTotalSize > FILE_SIZE_MIN && FileTotalSize <= FILE_SIZE_MAX)
        {
            fread_s(_p_FileBuff, sizeof(WCHAR) * FILE_SIZE_MAX, sizeof(WCHAR) * FileTotalSize, 1, p_File);
            isLoad = true;
            _FileSize = FileTotalSize;
        }
        fclose(p_File);
        return isLoad;
    }
    void WParser::Parser_Initial(void)
    {
        _p_ProvideCur = _p_ProvideStart;
        _CommentLevel = COMMENT_NONE;
    }
    void WParser::Parser_Release(void)
    {
        _FileSize = 0;

        _p_ProvideStart = nullptr;
        _p_ProvideEnd = nullptr;
        _p_ProvideCur = nullptr;
    }
    bool WParser::Parser_ProvideArea(WCHAR *p_AreaName)
    {
        if (0 == _FileSize) return false;       // 일단 여기만 체크

        bool isFindArea = false;
        bool isString = false;
        WCHAR *p_SearchBegin = nullptr;
        WCHAR *p_SearchPos = nullptr;
        int AreaNameLength = 0;
        WCHAR AreaNameBuff[WORD_SIZE + 1];

        p_SearchPos = _p_FileBuff;
        while (false == isFindArea)
        {
            // area name buff 초기화
            AreaNameLength = 0;
            wmemset(AreaNameBuff, 0, WORD_SIZE + 1);

            // area name 시작 찾기
            while (p_SearchPos != &_p_FileBuff[_FileSize - 1])
            {
                if (false == Parser_CommentPass(&p_SearchPos, &_p_FileBuff[_FileSize - 1]))
                    return false;

                if (L':' == *p_SearchPos)
                    break;
                else
                    p_SearchPos++;
            }
            if (p_SearchPos == &_p_FileBuff[_FileSize - 1])
                return false;

            // ':' 다음부터 검색 시작 지점 설정
            p_SearchBegin = ++p_SearchPos;

            // area name 찾기
            while (p_SearchPos != &_p_FileBuff[_FileSize - 1])
            {
                if (L'{' == *p_SearchPos
                    || 0x20 == *p_SearchPos || 0x09 == *p_SearchPos || 0x0a == *p_SearchPos || 0x0d == *p_SearchPos
                    || L',' == *p_SearchPos || L'.' == *p_SearchPos)
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
            wmemcpy_s(AreaNameBuff, WORD_SIZE, p_SearchBegin, AreaNameLength);

            // area name check
            if (0 == wcscmp(AreaNameBuff, p_AreaName))
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
            if (false == Parser_CommentPass(&p_SearchPos, &_p_FileBuff[_FileSize - 1]))
                return false;

            if (L'{' == *p_SearchPos && false == isString)
            {
                break;
            }
            else if (L'"' == *p_SearchPos)
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

        // area } 체크
        while (p_SearchPos != &_p_FileBuff[_FileSize - 1])
        {
            if (false == Parser_CommentPass(&p_SearchPos, &_p_FileBuff[_FileSize - 1]))
                return false;

            if (L'}' == *p_SearchPos && false == isString)
            {
                break;
            }
            else if (L'"' == *p_SearchPos)
            {
                if (false == isString)
                    isString = true;
                else
                    isString = false;
            }
            p_SearchPos++;
        }
        if (p_SearchPos == &_p_FileBuff[_FileSize - 1] && *p_SearchPos != L'}')
            return false;
        _p_ProvideEnd = p_SearchPos;
        return true;
    }

    // --------------------------------------------------
    // Comment를 체크하고 이동시켜주는 함수
    //
    // 리턴값
    // true : CurPos는 Comment가 아니다.
    // false : CurPos가 Comment이거나 EndPos에 다다랐다.
    // --------------------------------------------------
    bool WParser::Parser_CommentPass(WCHAR **pp_CurPos, WCHAR *p_EndPos)
    {
        while (*pp_CurPos != p_EndPos)
        {
            switch (_CommentLevel)
            {
            case COMMENT_NONE:
                if (L'/' == **pp_CurPos)
                {
                    if ((*pp_CurPos) + 1 != p_EndPos && L'/' == *((*pp_CurPos) + 1))
                    {
                        _CommentLevel = COMMENT_LINE;
                        *pp_CurPos += 2;
                        continue;
                    }
                    else if ((*pp_CurPos) + 1 != p_EndPos && L'*' == *((*pp_CurPos) + 1))
                    {
                        _CommentLevel = COMMENT_AREA;
                        *pp_CurPos += 2;
                        continue;
                    }
                }
                return true;
            case COMMENT_LINE:
                if (0x0a == **pp_CurPos || 0x0d == **pp_CurPos)
                {
                    _CommentLevel = COMMENT_NONE;
                    return true;
                }
                (*pp_CurPos)++;
                break;
            case COMMENT_AREA:
                if (L'*' == **pp_CurPos)
                {
                    if ((*pp_CurPos) + 1 != p_EndPos && L'/' == *((*pp_CurPos) + 1))
                    {
                        _CommentLevel = COMMENT_NONE;
                        return true;
                    }
                }
                (*pp_CurPos)++;
                break;
            default:
                return false;
            }
        }
        return false;
    }
    bool WParser::Parser_SkipNoneCommand(void)
    {
        while (_p_ProvideCur != _p_ProvideEnd)
        {
            if (false == Parser_CommentPass(&_p_ProvideCur, _p_ProvideEnd))
                return false;

            if (0x20 == *_p_ProvideCur || 0x09 == *_p_ProvideCur || 0x0a == *_p_ProvideCur || 0x0d == *_p_ProvideCur
                || L',' == *_p_ProvideCur || L'.' == *_p_ProvideCur)
                _p_ProvideCur++;
            else
                return true;
        }
        return false;		// 파일 끝에 다다르면 false
    }
    bool WParser::Parser_GetNextWord(WCHAR **pp_Buff, int *p_Length)
    {
        if (false == Parser_SkipNoneCommand())	return false;

        bool isString = false;

        *pp_Buff = _p_ProvideCur;
        *p_Length = 0;
        while (_p_ProvideCur != _p_ProvideEnd)
        {
            if (false == Parser_CommentPass(&_p_ProvideCur, _p_ProvideEnd))
                return false;

            if (L'"' == *_p_ProvideCur)
            {
                if (false == isString)
                    isString = true;
                else
                    isString = false;
            }

            if (true == isString)
            {
                if (0 == *p_Length)
                {
                    _p_ProvideCur++;
                    continue;
                }
                else
                {
                    break;
                }
            }

            if (0x20 == *_p_ProvideCur || 0x09 == *_p_ProvideCur || 0x0a == *_p_ProvideCur || 0x0d == *_p_ProvideCur
                || L',' == *_p_ProvideCur)// || L'"' == *_p_ProvideCur)
            {
                break;
            }
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
    bool WParser::Parser_GetStringWord(WCHAR **pp_Buff, int *p_Length)
    {
        if (false == Parser_SkipNoneCommand())	return false;

        // 처음 " 체크
        if (L'"' != *_p_ProvideCur || (_p_ProvideCur + 1) == _p_ProvideEnd)
        {
            return false;
        }
        else
            _p_ProvideCur++;

        // 나중 " 체크
        *pp_Buff = _p_ProvideCur;
        *p_Length = 0;
        while (_p_ProvideCur != _p_ProvideEnd)
        {
            if (false == Parser_CommentPass(&_p_ProvideCur, _p_ProvideEnd))
                return false;

            if (L'"' == *_p_ProvideCur)
            {
                if (*p_Length > 0 && *p_Length <= WORD_SIZE)
                    return true;
                else
                {
                    return false;
                }
            }
            _p_ProvideCur++;
            (*p_Length)++;
        }
        return false;
    }

    bool WParser::Parser_GetValue_int(WCHAR *p_Name, int *p_Value)
    {
        int Length;
        WCHAR *p_Buff;
        WCHAR Word[WORD_SIZE + 1];

        while (true == Parser_GetNextWord(&p_Buff, &Length))
        {
            wmemcpy_s(Word, WORD_SIZE, p_Buff, Length);
            if (Length == wcslen(p_Name) && 0 == wmemcmp(p_Name, Word, Length))
            {
                if (true == Parser_GetNextWord(&p_Buff, &Length))
                {
                    wmemcpy_s(Word, WORD_SIZE, p_Buff, Length);
                    if (Length == 1 && 0 == wmemcmp(L"=", Word, Length))
                    {
                        if (true == Parser_GetNextWord(&p_Buff, &Length))
                        {
                            // 실제로 원하는 밸류값이 나온다.
                            wmemcpy_s(Word, WORD_SIZE, p_Buff, Length);
                            Word[Length] = L'\0';
                            *p_Value = _wtoi(Word);
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }
    bool WParser::Parser_GetValue_float(WCHAR *p_Name, float *p_Value)
    {
        int Length;
        WCHAR *p_Buff;
        WCHAR Word[WORD_SIZE + 1];

        while (true == Parser_GetNextWord(&p_Buff, &Length))
        {
            wmemcpy_s(Word, WORD_SIZE, p_Buff, Length);
            if (Length == wcslen(p_Name) && 0 == wmemcmp(p_Name, Word, Length))
            {
                if (true == Parser_GetNextWord(&p_Buff, &Length))
                {
                    wmemcpy_s(Word, WORD_SIZE, p_Buff, Length);
                    if (Length == 1 && 0 == wmemcmp(L"=", Word, Length))
                    {
                        if (true == Parser_GetNextWord(&p_Buff, &Length))
                        {
                            // 실제로 원하는 밸류값이 나온다.
                            wmemcpy_s(Word, WORD_SIZE, p_Buff, Length);
                            Word[Length] = L'\0';
                            *p_Value = _wtof(Word);
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }
    bool WParser::Parser_GetValue_string(WCHAR *p_Name, WCHAR *p_Value)
    {
        int Length;
        WCHAR *p_Buff;
        WCHAR Word[WORD_SIZE + 1];

        while (true == Parser_GetNextWord(&p_Buff, &Length))
        {
            wmemcpy_s(Word, WORD_SIZE, p_Buff, Length);
            if (Length == wcslen(p_Name) && 0 == wmemcmp(p_Name, Word, Length))
            {
                if (true == Parser_GetNextWord(&p_Buff, &Length))
                {
                    wmemcpy_s(Word, WORD_SIZE, p_Buff, Length);
                    if (Length == 1 && 0 == wmemcmp(L"=", Word, Length))
                    {
                        if (true == Parser_GetStringWord(&p_Buff, &Length))
                        {
                            // 실제로 원하는 밸류값이 나온다.
                            wmemcpy_s(p_Value, WORD_SIZE, p_Buff, Length);
                            p_Value[Length] = L'\0';
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }
}