#ifndef __MY_UTIL_HEADER__
#define __MY_UTIL_HEADER__

namespace MonLib
{
    bool UTF8toUTF16(const char *p_Text, WCHAR *p_Buf, int BufLen);
    bool UTF16toUTF8(const WCHAR *p_Text, char *p_Buf, int BufLen);

    int GetLengthUTF16ToUtf8(const WCHAR *p_Text);
    bool ConvertUTF16ToUtf8(const WCHAR *p_Text, char *p_Buf, int BufLen);
    std::string ConvertUTF16ToUtf8(const WCHAR *p_Text);

    int GetLengthUTF8ToUtf16(const char *p_Text);
    bool ConvertUTF8ToUtf16(const char *p_Text, WCHAR *p_Buf, int BufLen);
    std::wstring ConvertUTF8ToUtf16(const char *p_Text);

    bool MyAtoi(char *p_DestStr, int *p_Val);

    bool StrToIP(WCHAR *p_Src, WCHAR *p_Dest);
    bool IntToUShort(int *p_Src, USHORT *p_Dest);
    bool IntToByte(int *p_Src, BYTE *p_Dest);

    bool LoadFile(const char *p_FileName, char *p_FileBuf, int FileBufSize);
    bool LoadFile(const WCHAR *p_FileName, char *p_FileBuf, int FileBufSize);

    bool ParseMapFileData(char *p_FileBuf, int FileBufSize, int *p_Width, int *p_Height);
}

#endif