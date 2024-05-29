#include "LibHeader.h"

namespace MonLib
{
    bool UTF8toUTF16(const char *p_Text, WCHAR *p_Buf, int BufLen)
    {
        int Ret = MultiByteToWideChar(CP_UTF8, 0, p_Text, (int)strlen(p_Text), p_Buf, BufLen);
        if (Ret < BufLen)
            p_Buf[Ret] = L'\0';
        else
            return false;
        return true;
    }
    bool UTF16toUTF8(const WCHAR *p_Text, char *p_Buf, int BufLen)
    {
        int Ret = WideCharToMultiByte(CP_UTF8, 0, p_Text, (int)wcslen(p_Text), NULL, 0, NULL, NULL);
        if (Ret < BufLen)
        {
            Ret = WideCharToMultiByte(CP_UTF8, 0, p_Text, (int)wcslen(p_Text), p_Buf, BufLen, NULL, NULL);
            p_Buf[Ret] = '\0';
            return true;
        }
        else
            return false;
    }

    int GetLengthUTF16ToUtf8(const WCHAR *p_Text)
    {
        if (NULL == p_Text || *p_Text == L'\0')
            return -1;
        return WideCharToMultiByte(CP_UTF8, 0, p_Text, (int)wcslen(p_Text), NULL, 0, NULL, NULL);
    }
    bool ConvertUTF16ToUtf8(const WCHAR *p_Text, char *p_Buf, int BufLen)
    {
        if (NULL == p_Text || *p_Text == L'\0')
            return false;

        int Ret = WideCharToMultiByte(CP_UTF8, 0, p_Text, (int)wcslen(p_Text), NULL, 0, NULL, NULL);
        if (Ret >= BufLen)
            return false;
        WideCharToMultiByte(CP_UTF8, 0, p_Text, (int)wcslen(p_Text), p_Buf, BufLen, NULL, NULL);
        p_Buf[Ret] = '\0';
        return true;
    }
    std::string ConvertUTF16ToUtf8(const WCHAR *p_Text)
    {
        if (NULL == p_Text || *p_Text == L'\0')
            return "";

        int Ret = WideCharToMultiByte(CP_UTF8, 0, p_Text, (int)wcslen(p_Text), NULL, 0, NULL, NULL);
        std::string Result(Ret, 0);
        WideCharToMultiByte(CP_UTF8, 0, p_Text, (int)wcslen(p_Text), &Result[0], Ret, NULL, NULL);
        return Result;
    }

    int GetLengthUTF8ToUtf16(const char *p_Text)
    {
        if (NULL == p_Text || '\0' == *p_Text)
            return -1;
        return MultiByteToWideChar(CP_UTF8, 0, p_Text, (int)strlen(p_Text), NULL, 0);
    }
    bool ConvertUTF8ToUtf16(const char *p_Text, WCHAR *p_Buf, int BufLen)
    {
        if (NULL == p_Text || '\0' == *p_Text)
            return false;

        int Ret = MultiByteToWideChar(CP_UTF8, 0, p_Text, (int)strlen(p_Text), NULL, 0);
        if (Ret >= BufLen)
            return false;
        MultiByteToWideChar(CP_UTF8, 0, p_Text, (int)strlen(p_Text), p_Buf, BufLen);
        p_Buf[Ret] = L'\0';
        return true;
    }
    std::wstring ConvertUTF8ToUtf16(const char *p_Text)
    {
        if (NULL == p_Text || '\0' == *p_Text)
            return L"";

        int Ret = MultiByteToWideChar(CP_UTF8, 0, p_Text, (int)strlen(p_Text), NULL, 0);
        std::wstring Result(Ret, 0);
        MultiByteToWideChar(CP_UTF8, 0, p_Text, (int)strlen(p_Text), &Result[0], Ret);
        return Result;
    }

    bool MyAtoi(char *p_DestStr, int *p_Val)
    {
        char *end;
        errno = 0;

        const long Ret = strtol(p_DestStr, &end, 10);
        if (end == p_DestStr)
            return false;
        else if ('\0' != *end)
            return false;
        else if ((LONG_MAX == Ret || LONG_MIN == Ret) && ERANGE == errno)
            return false;
        else if (Ret > INT_MAX || Ret < INT_MIN)
            return false;

        *p_Val = (int)Ret;
        return true;
    }

    bool StrToIP(WCHAR *p_Src, WCHAR *p_Dest)
    {
        HRESULT hResult;
        if (wcslen(p_Src) > IP_V4_MAX_LEN)
            return false;
        hResult = StringCchCopyW(p_Dest, IP_V4_MAX_LEN + 1, p_Src);
        if (FAILED(hResult))
            return false;
        return true;
    }
    bool IntToUShort(int *p_Src, USHORT *p_Dest)
    {
        if (*p_Src > USHRT_MAX || *p_Src < 0)
            return false;
        *p_Dest = (USHORT)(*p_Src);
        return true;
    }
    bool IntToByte(int *p_Src, BYTE *p_Dest)
    {
        if (*p_Src > UCHAR_MAX || *p_Src < 0)
            return false;
        *p_Dest = (BYTE)(*p_Src);
        return true;
    }

    // 윈도우 전용 함수들이다.(fopen_s, CreateFile etc.)
    //
    // 주의사항
    // 파일 버퍼의 끝은 null 문자이다.
    // 따라서 파일에 null 문자가 포함되어 있다면 로드되지 않는다.

    bool LoadFile(const char *p_FileName, char *p_FileBuf, int FileBufSize)
    {
        FILE *p_File;
        int Error;
        int FileSize;
        size_t FileReadRet;
        size_t ReadSize;

        Error = fopen_s(&p_File, p_FileName, "rb");
        if (Error != 0)
            return false;

        fseek(p_File, 0, SEEK_END);
        FileSize = ftell(p_File);
        fseek(p_File, 0, SEEK_SET);

        if (FileSize > FileBufSize - 1)
        {
            fclose(p_File);
            return false;
        }

        FileReadRet = fread_s(p_FileBuf, FileBufSize - 1, FileSize, 1, p_File);
        if (FileReadRet != 1)
        {
            fclose(p_File);
            return false;
        }

        p_FileBuf[FileSize] = '\0';
        ReadSize = strlen(p_FileBuf);
        if (ReadSize > INT_MAX || ReadSize != FileSize)
        {
            fclose(p_File);
            return false;
        }

        fclose(p_File);
        return true;
    }

    bool LoadFile(const WCHAR *p_FileName, char *p_FileBuf, int FileBufSize)
    {
        if (nullptr == p_FileName || nullptr == p_FileBuf || FileBufSize < 1)
            return false;

        bool IsLoad;
        HANDLE h_File;
        LARGE_INTEGER li_FileSize;
        DWORD dw_FileSize;
        DWORD dw_ReadSize;
        size_t ReadSize;

        IsLoad = false;
        h_File = INVALID_HANDLE_VALUE;
        li_FileSize.QuadPart = 0;
        dw_FileSize = 0;
        dw_ReadSize = 0;

        h_File = CreateFile(p_FileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (INVALID_HANDLE_VALUE == h_File)
            return false;

        do
        {
            if (!GetFileSizeEx(h_File, &li_FileSize))
            {
                break;
            }

            // 버퍼 사이즈는 32bit integer형이므로 체크한다.
            if (li_FileSize.HighPart != 0 || (li_FileSize.LowPart & 0x80000000) != 0)     // 비트연산 우선순위 때문에 괄호 꼭 할 것.
            {
                break;
            }

            dw_FileSize = li_FileSize.LowPart;
            if (0 == dw_FileSize || dw_FileSize > (DWORD)(FileBufSize - 1))
            {
                break;
            }

            ////if (li_FileSize.HighPart != 0 ||
            ////    (li_FileSize.LowPart & 0x80000000) != 0 ||     // 비트연산 우선순위 때문에 괄호 꼭 할 것.
            ////    li_FileSize.LowPart > FileBufSize)
            //if (0 == li_FileSize.LowPart || li_FileSize.LowPart > (DWORD)(FileBufSize - 1))
            //{
            //    break;
            //}
            //dw_FileSize = li_FileSize.LowPart;

            if (!ReadFile(h_File, p_FileBuf, dw_FileSize, &dw_ReadSize, NULL))
            {
                break;
            }
            if (dw_FileSize != dw_ReadSize)
            {
                break;
            }
            p_FileBuf[dw_FileSize] = '\0';

            ReadSize = strlen(p_FileBuf);
            if (ReadSize > UINT_MAX || ReadSize != dw_FileSize)
            {
                break;
            }

            IsLoad = true;
        } while (0);

        CloseHandle(h_File);
        return IsLoad;
    }

    bool ParseMapFileData(char *p_FileBuf, int FileBufSize, int *p_Width, int *p_Height)
    {
        // FileBuf Parsing
        bool IsParse;
        bool CharDelimiter;

        int LineLength;
        int LineLengthMax;
        int LineCount;

        int BufIndex;

        IsParse = true;
        CharDelimiter = false;

        LineLength = 0;
        LineLengthMax = 0;
        LineCount = 0;

        for (BufIndex = 0; BufIndex < FileBufSize; ++BufIndex)
        {
            if ('\n' == p_FileBuf[BufIndex])
            {
                if (true == CharDelimiter)
                {
                    IsParse = false;
                    break;
                }

                if (LineLengthMax < LineLength)
                    LineLengthMax = LineLength;
                LineLength = 0;
                LineCount++;
            }
            else
            {
                if (true == CharDelimiter)
                {
                    if (' ' == p_FileBuf[BufIndex])
                    {
                        CharDelimiter = false;
                        p_FileBuf[BufIndex] = '\0';
                    }
                    else
                    {
                        IsParse = false;
                        break;
                    }
                }
                else if (',' == p_FileBuf[BufIndex])
                {
                    CharDelimiter = true;
                    p_FileBuf[BufIndex] = '\0';
                }
                else
                {
                    LineLength++;
                }
            }
        }// for end

        if (true == IsParse)
        {
            *p_Width = LineLengthMax;
            *p_Height = LineCount;
        }
        else
        {
            *p_Width = 0;
            *p_Height = 0;
        }

        return IsParse;
    }
}