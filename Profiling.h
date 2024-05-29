#ifndef __PROFILING_HEADER__
#define __PROFILING_HEADER__

namespace MonLib
{
    void ProfileInitial(void);

    void ProfileBegin(WCHAR *Name);
    void ProfileEnd(WCHAR *Name);

    void ProfileDataOutText(void);
}

#endif