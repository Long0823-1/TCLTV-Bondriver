// Standard BonDriver interface (IBonDriver / IBonDriver2), as used by
// TVTest and other Japanese digital-TV viewer apps. This is the common,
// de-facto public interface definition shared across many open-source
// BonDriver implementations -- not proprietary to any one of them.
#pragma once
#include <windows.h>

class IBonDriver {
public:
    virtual const BOOL OpenTuner(void) = 0;
    virtual void CloseTuner(void) = 0;
    virtual const BOOL SetChannel(const BYTE bCh) = 0;
    virtual const float GetSignalLevel(void) = 0;
    virtual const DWORD WaitTsStream(const DWORD dwTimeOut = 0) = 0;
    virtual const DWORD GetReadyCount(void) = 0;
    virtual const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain) = 0;
    virtual const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain) = 0;
    virtual void PurgeTsStream(void) = 0;
    virtual void Release(void) = 0;
};

class IBonDriver2 : public IBonDriver {
public:
    virtual LPCWSTR GetTunerName(void) = 0;
    virtual const BOOL IsTunerOpening(void) = 0;
    virtual LPCWSTR EnumTuningSpace(const DWORD dwSpace) = 0;
    virtual LPCWSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel) = 0;
    virtual const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel) = 0;
    virtual const DWORD GetCurSpace(void) = 0;
    virtual const DWORD GetCurChannel(void) = 0;
};

