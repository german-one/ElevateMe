// Copyright (c) 2026 Steffen Illhardt
// Licensed under the MIT license ( https://opensource.org/license/mit ).

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeclaration-after-statement" // we meet recent C standards
#  if defined(__clang__)
#    pragma clang diagnostic ignored "-Wnonportable-system-include-path" // the capitalization of Windows header names varies
#    pragma clang diagnostic ignored "-Wunsafe-buffer-usage" // this is C code, which typically uses pointer arithmetic
#  endif
#endif

#include <stdlib.h>
#include <windows.h>
#include <winternl.h>
#include <initguid.h>
#define COBJMACROS
#include <taskschd.h>

#define REGION(...) (1) // only for code folding

// This macro only affects the MSVC, GCC and Clang linkers and aims to avoid unused startup prepartion in this app.
// If defined in this context, add the settings specified in the comments of the applicable MAIN definition.
#define OVERRIDE_START

#if REGION(macro definitions)
#  if defined(OVERRIDE_START) && (defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__))
#    if defined(__GNUC__) || defined(__clang__)
#      define NO_RETURN __attribute__((noreturn))
#    else
#      define NO_RETURN __declspec(noreturn)
#    endif
#    define QUIT(status_) _exit((int)(status_))
#    if defined(_MSC_VER)
//    ***  MSVC (also if used with the LLVM (clang-cl) tool-set)
//     - add compiler option: /Zl
//     - add linker options: /ENTRY:start /NODEFAULTLIB
//     - manually add libraries: msvcrt.lib;ucrt.lib;vcruntime.lib
#      define MAIN NO_RETURN void WINAPI start
#    else
//    *** GCC or Clang
//     - add linker options: -Wl,-e,__start -nostartfiles
#      if defined(__i386)
#        define MAIN NO_RETURN void _start
#      else
#        define MAIN NO_RETURN void __start
#      endif
#    endif
MAIN(void);
#  else
#    define QUIT(status_) return (int)(status_)
#    define MAIN int main
#  endif

#  if defined(__has_attribute)
#    if __has_attribute(always_inline)
#      define FORCE_INLINE inline __attribute__((always_inline))
#    endif
#  endif
#  if !defined(FORCE_INLINE)
#    if defined(_MSC_VER)
#      define FORCE_INLINE __forceinline
#    else
#      define FORCE_INLINE inline
#    endif
#  endif

#  define CLOSE_IF_NOT_NULL(handle_) \
    do                               \
    {                                \
      if (handle_)                   \
        CloseHandle(handle_);        \
    } while (0)

#  define CLEANUP_IF_FAILED(hr_) \
    do                           \
    {                            \
      hres = (hr_);              \
      if (FAILED(hres))          \
        goto cleanup;            \
    } while (0)

#  define CLEANUP_WITH_LAST_ERROR_IF(cond_) /* Win32 error codes only */ \
    do                                                                   \
    {                                                                    \
      if (cond_)                                                         \
      {                                                                  \
        hres = (HRESULT)(*pLastError | 0x80070000);                      \
        goto cleanup;                                                    \
      }                                                                  \
    } while (0)

#  define cmdLnBuf_TYPE const WCHAR *const // <cmdLnBuf> command line (UNICODE_STRING::Buffer); null terminated because this is the same pointer that GetCommandLineW() returns
#  define cmdLnSize_TYPE const WORD // <cmdLnSize> number of bytes, terminator not counted (UNICODE_STRING::Length)
#  define curDirBuf_TYPE const WCHAR *const // <curDirBuf> current directory (UNICODE_STRING::Buffer)
#  define curDirSize_TYPE const WORD // <curDirSize> number of bytes, terminator not counted (UNICODE_STRING::Length)
#  define envBuf_TYPE const WCHAR *const // <envBuf> flat sequence of null terminated environment strings, which is terminated by an additional null character
#  define envSize_TYPE const SIZE_T // <envSize> number of bytes, terminator counted; req. NT 6.0+ (min. Vista)
#  if defined(_WIN64)
#    define cmdLnBuf_OFF 0x78
#    define cmdLnSize_OFF 0x70
#    define curDirBuf_OFF 0x40
#    define curDirSize_OFF 0x38
#    define envBuf_OFF 0x80
#    define envSize_OFF 0x3F0
#  else
#    define cmdLnBuf_OFF 0x44
#    define cmdLnSize_OFF 0x40
#    define curDirBuf_OFF 0x28
#    define curDirSize_OFF 0x24
#    define envBuf_OFF 0x48
#    define envSize_OFF 0x290
#  endif
#  define PROC_PARAM_VALUE_OF(fieldalias_) /* point into the RTL_USER_PROCESS_PARAMETERS in the process memory to get the desired field data */ \
    (*(fieldalias_##_TYPE *)(const void *)(pProcParams + fieldalias_##_OFF))

#  define POINTS_TO_SEPARATOR(pchar_) (*(pchar_) == L' ' || *(pchar_) == L'\t') // space and tab are the default separators in a command line
#  define SKIP_SEPARATORS(pchar_) for (++(pchar_); POINTS_TO_SEPARATOR(pchar_); ++(pchar_))

#  define BSTR_BUF(varname_, count_) /* derived from https://learn.microsoft.com/en-us/previous-versions/windows/desktop/automat/bstr */                  \
    struct tag_##varname_                                                                                                                                 \
    {                                                                                                                                                     \
      DWORD nbytes; /* length of the string in bytes, null terminator not counted */                                                                      \
      WCHAR bstr[(((count_) * sizeof(WCHAR) + sizeof(DWORD) - 1) / sizeof(DWORD)) * sizeof(DWORD) / sizeof(WCHAR)]; /* DWORD aligned to avoid warnings */ \
    } varname_

#  define TASK_ROOT L"\\"
#  define TASK_NAME L"ElevateMe"
#  define TAGGED_UUID_PATTERN L"T{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" // placeholder for a tag character (see below) + scheme of a uuid string as created by StringFromGUID2()
#  define TAG_EVT L'\xFDDE' // begin of the name of a named event, also set for the identifier passed to the elevated process
#  define TAG_FMO L'\xFDDF' // begin of the name of a file mapping object; this and TAG_EVT are noncharacters, see https://www.unicode.org/faq/private_use#nonchar1
#endif

static HANDLE evt = NULL; // named event
static HANDLE fmo = NULL; // named file mapping object
static LPVOID view = NULL; // view of the file mapping
static BSTR_BUF(identifier, ARRAYSIZE(TAGGED_UUID_PATTERN)) = {
  .nbytes = sizeof(TAGGED_UUID_PATTERN) - sizeof(WCHAR),
  .bstr = { TAG_FMO } // tagged uuid string "T{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}", tag T is an alternating prefix: TAG_EVT or TAG_FMO (initially)
};

#if REGION(invoker branch)
static FORCE_INLINE HRESULT RunScheduledTask(const VARIANT *const pTaskArg)
{
  static const VARIANT vEmptyByVal = { .vt = VT_EMPTY };
  static ITaskFolder *pTaskFolder = NULL;
  static IRegisteredTask *pRegTask = NULL;
  static BSTR_BUF(rootName, ARRAYSIZE(TASK_ROOT)) = { .nbytes = sizeof(TASK_ROOT) - sizeof(WCHAR), .bstr = TASK_ROOT };
  static BSTR_BUF(taskName, ARRAYSIZE(TASK_NAME)) = { .nbytes = sizeof(TASK_NAME) - sizeof(WCHAR), .bstr = TASK_NAME };

  ITaskService *pTaskSvc;
  HRESULT hres = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskService, (LPVOID *)&pTaskSvc);
  if (FAILED(hres))
    return hres;

  CLEANUP_IF_FAILED(ITaskService_Connect(pTaskSvc, vEmptyByVal, vEmptyByVal, vEmptyByVal, vEmptyByVal));
  CLEANUP_IF_FAILED(ITaskService_GetFolder(pTaskSvc, rootName.bstr, &pTaskFolder));
  CLEANUP_IF_FAILED(ITaskFolder_GetTask(pTaskFolder, taskName.bstr, &pRegTask));
  hres = IRegisteredTask_Run(pRegTask, *pTaskArg, NULL); // pTaskArg ultimately references the TAG_EVT prefixed uuid string

cleanup:
  if (pRegTask)
    IRegisteredTask_Release(pRegTask);

  if (pTaskFolder)
    ITaskFolder_Release(pTaskFolder);

  ITaskService_Release(pTaskSvc);
  return hres;
}

static FORCE_INLINE HRESULT MakeIdentifierUnique(void)
{
  GUID uuid;
  const HRESULT hres = CoCreateGuid(&uuid);
  if (FAILED(hres))
    return hres;

  (void)StringFromGUID2(&uuid, identifier.bstr + 1, ARRAYSIZE(TAGGED_UUID_PATTERN) - 1);
  return S_OK;
}

static FORCE_INLINE HRESULT ConvertNumericArgs(const WCHAR **const pArg, DWORD *const pShow, DWORD *const pWait)
{
  WCHAR *endptr;
  *pShow = wcstoul(*pArg, &endptr, 0); // if the show and wait arguments are missing, endptr points to the begin of the command which makes the POINTS_TO_SEPARATOR() check fail
  *pWait = wcstoul(endptr, &endptr, 0); // if the wait argument is omitted, *pWait will default to 0 (FALSE) and endptr will remain the same
  *pArg = endptr;
  return (*pShow < 8 && POINTS_TO_SEPARATOR(*pArg)) ? S_OK : E_INVALIDARG;
}

static FORCE_INLINE HRESULT AsInvoker(const WCHAR *arg, const DWORD *const pLastError, const BYTE *const pProcParams)
{
  static VARIANT taskArg = { .vt = VT_BSTR, .bstrVal = identifier.bstr };

  HRESULT hres = CoInitialize(NULL);
  if (FAILED(hres))
    return hres;

  DWORD show, wait;
  CLEANUP_IF_FAILED(ConvertNumericArgs(&arg, &show, &wait));

  CLEANUP_IF_FAILED(MakeIdentifierUnique()); // append a uuid string to the TAG_FMO character in the identifier.bstr buffer
  SKIP_SEPARATORS(arg); // move the arg pointer to the first character of the command
  const WORD cmdLen = (WORD)(PROC_PARAM_VALUE_OF(cmdLnSize) / sizeof(WCHAR) - (WORD)(arg - PROC_PARAM_VALUE_OF(cmdLnBuf)) + 1); // remaining command line, +1 for the terminating null
  const DWORD dirLen = PROC_PARAM_VALUE_OF(curDirSize) / sizeof(WCHAR) + 1; // +1 for the terminating null
  CLEANUP_WITH_LAST_ERROR_IF(!(fmo = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)(sizeof(STARTUPINFOW) + (dirLen + cmdLen) * sizeof(WCHAR) + PROC_PARAM_VALUE_OF(envSize)), identifier.bstr))); // file mapping object backed by the system paging file
  CLEANUP_WITH_LAST_ERROR_IF(!(view = MapViewOfFile(fmo, FILE_MAP_WRITE, 0, 0, 0))); // "The initial contents of the pages in a file mapping object backed by the paging file are 0 (zero)." https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile

  STARTUPINFOW *const pSettings = (STARTUPINFOW *)view; // zero-initialized, see above
  WCHAR *strDest = (WCHAR *)(pSettings + 1);
  pSettings->cb = sizeof(STARTUPINFOW);
  pSettings->dwY = wait; // used to transfer the information about whether to wait until the elevated process has terminated, any non-zero value is equivalent to TRUE
  pSettings->dwXCountChars = cmdLen; // used to transfer the length of the command line to execute, incl. terminating null
  pSettings->dwYCountChars = dirLen; // used to transfer the path length of the current directory, incl. terminating null
  pSettings->dwFlags = STARTF_USESHOWWINDOW; // wShowWindow is used for process creation, while members like dwX* or dwY* are ignored and used for custom IPC
  pSettings->wShowWindow = (WORD)show;
  CopyMemory(strDest, arg, cmdLen * sizeof(WCHAR)); // command
  CopyMemory((strDest += cmdLen), PROC_PARAM_VALUE_OF(curDirBuf), PROC_PARAM_VALUE_OF(curDirSize)); // current directory, the terminator is maintained in the zero-initialized view memory
  CopyMemory(strDest + dirLen, PROC_PARAM_VALUE_OF(envBuf), PROC_PARAM_VALUE_OF(envSize)); // environment strings

  identifier.bstr[0] = TAG_EVT;
  CLEANUP_WITH_LAST_ERROR_IF(!(evt = CreateEventW(NULL, FALSE, FALSE, identifier.bstr)));
  CLEANUP_IF_FAILED(RunScheduledTask(&taskArg)); // taskArg statically references the identifier.bstr
  CLEANUP_WITH_LAST_ERROR_IF(WaitForSingleObject(evt, INFINITE) != WAIT_OBJECT_0);

  hres = *(HRESULT *)view;

cleanup:
  CoUninitialize();
  return hres;
}
#endif

#if REGION(admin branch)
static FORCE_INLINE HRESULT AsAdmin(const WCHAR *const arg, const DWORD *const pLastError)
{
  static PROCESS_INFORMATION procInf = { .hProcess = NULL };

  HRESULT hres = S_OK;
  CLEANUP_WITH_LAST_ERROR_IF(!(evt = OpenEventW(EVENT_MODIFY_STATE, FALSE, arg)));

  CopyMemory(identifier.bstr + 1, arg + 1, identifier.nbytes - sizeof(WCHAR)); // this app is not meant to modify kernel-created data, hence the copy; the successfully opened event has proven that arg does indeed point to the identifier
  CLEANUP_WITH_LAST_ERROR_IF(!(fmo = OpenFileMappingW(FILE_MAP_WRITE, FALSE, identifier.bstr)));
  CLEANUP_WITH_LAST_ERROR_IF(!(view = MapViewOfFile(fmo, FILE_MAP_WRITE, 0, 0, 0)));

  STARTUPINFOW *const pSettings = (STARTUPINFOW *)view;
  WCHAR *const cmd = (WCHAR *)(pSettings + 1);
  WCHAR *const dir = cmd + pSettings->dwXCountChars;
  CLEANUP_WITH_LAST_ERROR_IF(!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT, dir + pSettings->dwYCountChars, dir, pSettings, &procInf));
  CloseHandle(procInf.hThread);
  if (pSettings->dwY) // wait until the elevated process has terminated
  {
    CLEANUP_WITH_LAST_ERROR_IF(WaitForSingleObject(procInf.hProcess, INFINITE) != WAIT_OBJECT_0);
    CLEANUP_WITH_LAST_ERROR_IF(!GetExitCodeProcess(procInf.hProcess, (DWORD *)&hres));
  }

cleanup:
  CLOSE_IF_NOT_NULL(procInf.hProcess);
  if (view)
    *(HRESULT *)view = hres;

  if (evt)
    SetEvent(evt);

  return hres;
}
#endif

#if REGION(main)
static FORCE_INLINE const WCHAR *GetArgPtr(const WCHAR *cmdLn) // skip both the app name and the following separator(s), return a pointer to the first program argument in the command line
{
  static const char classes[] = "\3\0\0\0\0\0\0\0\0\2\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\2\0\1";

  for (;; ++cmdLn)
  {
    if (*cmdLn >= sizeof(classes) - 1) // shortcut for characters > '"'
      continue;

    switch (classes[*cmdLn]) // use the lookup table for the classification of characters '\0'..'"'
    {
    case 0: // character without special meaning here
      continue;
    case 1: // quotation mark, introducing a quoted (sub)string
      while (*++cmdLn != L'"') // find the complementary quotation mark; spaces and tabs are no separators in between
        if (*cmdLn == L'\0')
          return cmdLn;
      continue;
    case 2: // separator SP or HT
      SKIP_SEPARATORS(cmdLn); // move the pointer to the first argument
      return cmdLn;
    default: // class 3 for the string terminator NUL
      return cmdLn;
    }
  }
}

MAIN(void)
{
  FreeConsole(); // although undesirable, a window is needed for the Windows focus management; the compromise is to get rid of it after just a quick flash

  // retrieving data from the process memory via TEB is partially undocumented, however it greatly reduces the complexity of this app:
  // - we don't need to call GetCurrentDirectoryW() to get the required buffer size, once more GetCurrentDirectoryW() to copy the path into the buffer,
  //   GetEnvironmentStringsW() to get a copy of the environment block in an allocated buffer, FreeEnvironmentStringsW() to deallocate the buffer,
  //   wcslen() to get the size of the command line; further, GetCommandLineW() and several invocations of GetLastError() are unnecessary
  // - we avoid measuring the size of the environment block in an iteration that repeatedly calls wcslen()
  const TEB *const pTeb = NtCurrentTeb();
  const BYTE *const pProcParams = (const BYTE *)(pTeb->ProcessEnvironmentBlock->ProcessParameters); // BYTE* because we use byte offsets to read fields of RTL_USER_PROCESS_PARAMETERS
  const WCHAR *const arg = GetArgPtr(PROC_PARAM_VALUE_OF(cmdLnBuf)); // the command line buffer is null-terminated; this implementation detail leaks via assertion (NtCurrentTeb()->ProcessEnvironmentBlock->ProcessParameters->CommandLine.Buffer == GetCommandLineW())
  if (!*arg) // no argument received
    QUIT(E_INVALIDARG);

  const DWORD *const pLastError = (const DWORD *)((void *const *)pTeb + 13); // pointer to the TEB::LastErrorValue field, which is the source of the value that GetLastError() returns
  const HRESULT hres = (*arg != TAG_EVT) ?
                         AsInvoker(arg, pLastError, pProcParams) : // the user called the app
                         AsAdmin(arg, pLastError); // the scheduled task called the app elevated

  if (view)
    UnmapViewOfFile(view);

  CLOSE_IF_NOT_NULL(fmo);
  CLOSE_IF_NOT_NULL(evt);
  QUIT(hres);
}
#endif

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif
