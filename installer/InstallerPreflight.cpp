// Check-only Windows Restart Manager preflight. No shutdown/restart API is used.
// Targets are opened without CREATE/TRUNCATE or any WriteFile call. Only the
// explicitly supplied UTF-8 report in Setup's temporary directory is written.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <restartmanager.h>
#include <string>
#include <vector>
#include <cwctype>

namespace {
constexpr int clearCode=0, busyCode=32, errorCode=33;
std::wstring clean(std::wstring text) {
    for(auto& c:text)if(c==L'\r'||c==L'\n')c=L' ';
    return text;
}
bool absoluteFile(const std::wstring& path,std::wstring& out) {
    if(path.size()<4||!iswalpha(path[0])||path[1]!=L':'||
       (path[2]!=L'\\'&&path[2]!=L'/')||path.find_first_of(L"\r\n")!=std::wstring::npos)return false;
    const DWORD size=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);
    if(!size||size>32768)return false;
    std::vector<wchar_t> data(size);
    if(!GetFullPathNameW(path.c_str(),size,data.data(),nullptr))return false;
    out=data.data();return true;
}
bool writeReport(const std::wstring& path,const std::wstring& text) {
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    if(size<=0)return false;
    std::string utf8(size,'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),utf8.data(),size,nullptr,nullptr))return false;
    const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    const unsigned char bom[]{0xef,0xbb,0xbf};DWORD written=0;
    bool ok=WriteFile(file,bom,3,&written,nullptr)&&written==3;
    if(ok)ok=WriteFile(file,utf8.data(),DWORD(utf8.size()),&written,nullptr)&&written==utf8.size();
    CloseHandle(file);return ok;
}
struct Session {
    DWORD handle=0;bool open=false;
    ~Session(){if(open)RmEndSession(handle);}
};
int inspect(const std::vector<std::wstring>& paths,std::wstring& details) {
    std::vector<std::wstring> existing;
    for(const auto& path:paths) {
        details+=L"FILE="+path+L"\r\n";
        const DWORD attributes=GetFileAttributesW(path.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES) {
            const DWORD code=GetLastError();
            if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND){details+=L"FILE_STATE=ABSENT\r\n";continue;}
            details+=L"ERROR=GetFileAttributesW:"+std::to_wstring(code)+L"\r\n";return errorCode;
        }
        if(attributes&FILE_ATTRIBUTE_DIRECTORY){details+=L"ERROR=TARGET_IS_DIRECTORY\r\n";return errorCode;}
        existing.push_back(path);
    }
    if(existing.empty())return clearCode;
    Session session;WCHAR key[CCH_RM_SESSION_KEY+1]{};
    DWORD code=RmStartSession(&session.handle,0,key);
    if(code!=ERROR_SUCCESS){details+=L"ERROR=RmStartSession:"+std::to_wstring(code)+L"\r\n";return errorCode;}
    session.open=true;
    std::vector<LPCWSTR> resources;for(const auto& path:existing)resources.push_back(path.c_str());
    code=RmRegisterResources(session.handle,UINT(resources.size()),resources.data(),0,nullptr,0,nullptr);
    if(code!=ERROR_SUCCESS){details+=L"ERROR=RmRegisterResources:"+std::to_wstring(code)+L"\r\n";return errorCode;}
    UINT needed=0,count=0;DWORD reasons=0;
    code=RmGetList(session.handle,&needed,&count,nullptr,&reasons);
    std::vector<RM_PROCESS_INFO> processes;
    for(int attempt=0;code==ERROR_MORE_DATA&&attempt<5;++attempt) {
        if(needed>4096){details+=L"ERROR=PROCESS_LIST_LIMIT\r\n";return errorCode;}
        processes.resize(needed);count=UINT(processes.size());
        code=RmGetList(session.handle,&needed,&count,processes.data(),&reasons);
    }
    if(code!=ERROR_SUCCESS){details+=L"ERROR=RmGetList:"+std::to_wstring(code)+L"\r\n";return errorCode;}
    if(count) {
        for(UINT i=0;i<count;++i) {
            details+=L"PROCESS_PID="+std::to_wstring(processes[i].Process.dwProcessId)+L"\r\n";
            details+=L"PROCESS_NAME="+clean(processes[i].strAppName)+L"\r\n";
        }
        return busyCode;
    }
    for(const auto& path:existing) {
        const HANDLE file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,
                                      OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) {
            code=GetLastError();details+=L"PROBE_FILE="+path+L"\r\nWIN32_ERROR="+std::to_wstring(code)+L"\r\n";
            if(code==ERROR_SHARING_VIOLATION||code==ERROR_LOCK_VIOLATION||code==ERROR_USER_MAPPED_FILE)return busyCode;
            return errorCode;
        }
        CloseHandle(file);
    }
    return clearCode;
}
}
int wmain(int argc,wchar_t** argv) {
    std::vector<std::wstring> paths;std::wstring report,details;
    bool argumentsOk=true;
    for(int i=1;i<argc;++i) {
        const std::wstring flag=argv[i];
        if(i+1>=argc){argumentsOk=false;break;}
        std::wstring normalized;
        if(!absoluteFile(argv[++i],normalized)){argumentsOk=false;continue;}
        if(flag==L"--path")paths.push_back(normalized);
        else if(flag==L"--report")report=normalized;
        else argumentsOk=false;
    }
    if(paths.empty()||paths.size()>128||report.empty())argumentsOk=false;
    for(const auto& path:paths)if(_wcsicmp(path.c_str(),report.c_str())==0)return errorCode;
    int code=errorCode;
    if(argumentsOk)code=inspect(paths,details);else details=L"ERROR=INVALID_ARGUMENTS\r\n";
    const std::wstring status=code==clearCode?L"CLEAR":code==busyCode?L"LOCKED":L"ERROR";
    const std::wstring text=L"ODX_PREFLIGHT_STATUS="+status+L"\r\nODX_PREFLIGHT_CODE="+std::to_wstring(code)+L"\r\n"+details;
    if(report.empty()||!writeReport(report,text))return errorCode;
    return code;
}
