#include "iocpsock.h"

Tcl_ObjCmdProc	Iocp_StatsObjCmd;

/* Globals */
HMODULE iocpModule = NULL;

/* A mess of stuff to make sure we get a good binary. */
#ifdef _MSC_VER
    // Only do this when MSVC++ is compiling us.
#   if defined(USE_TCL_STUBS)
	// Mark this .obj as needing tcl's Stubs library.
#	pragma comment(lib, "tclstub" \
		STRINGIFY(JOIN(TCL_MAJOR_VERSION,TCL_MINOR_VERSION)) ".lib")
#	if !defined(_MT) || !defined(_DLL) || defined(_DEBUG)
	    // This fixes a bug with how the Stubs library was compiled.
	    // The requirement for msvcrt.lib from tclstubXX.lib should
	    // be removed.
#	    pragma comment(linker, "-nodefaultlib:msvcrt.lib")
#	endif
#   elif !defined(STATIC_BUILD)
	// Mark this .obj needing the import library
#	pragma comment(lib, "tcl" \
		STRINGIFY(JOIN(TCL_MAJOR_VERSION,TCL_MINOR_VERSION)) ".lib")
#   endif
#   pragma comment (lib, "user32.lib")
#   pragma comment (lib, "kernel32.lib")
#   pragma comment (lib, "ws2_32.lib")
#endif


#if !defined(STATIC_BUILD)
BOOL APIENTRY 
DllMain (HANDLE hModule, DWORD dwReason, LPVOID lpReserved)
{   
    if (dwReason == DLL_PROCESS_ATTACH) {
	/* don't call DLL_THREAD_ATTACH; I don't care to know. */
	DisableThreadLibraryCalls(hModule);
	iocpModule = hModule;
    }
    return TRUE;
}
#endif

char *
GetSysMsg(DWORD id)
{
    int chars;
    static char sysMsgSpace[512];

    chars = wsprintf(sysMsgSpace, "[%u] ", id);
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |FORMAT_MESSAGE_IGNORE_INSERTS |
	    FORMAT_MESSAGE_MAX_WIDTH_MASK, 0L, id, 0, sysMsgSpace+chars, (512-chars),
	    0);
    return sysMsgSpace;
}

Tcl_Obj *
GetSysMsgObj(DWORD id)
{
    return Tcl_NewStringObj(GetSysMsg(id), -1);
}

static const char *HtmlStart = "<tr><td>";
static const char *HtmlMiddle = "</td><td>";
static const char *HtmlEnd = "</td></tr>\n";

int
Iocp_StatsObjCmd (
    ClientData notUsed,			/* Not used. */
    Tcl_Interp *interp,			/* Current interpreter. */
    int objc,				/* Number of arguments. */
    Tcl_Obj *CONST objv[])		/* Argument objects. */
{
    Tcl_Obj *newResult;
    char buf[TCL_INTEGER_SPACE];
    int useHtml = 0;
    const char *Start = "", *Middle = " ", *End = "\n";

    if (objc == 2) {
	if (Tcl_GetBooleanFromObj(interp, objv[1], &useHtml) == TCL_ERROR) {
	    return TCL_ERROR;
	}
    }

    if (useHtml) {
	Start = HtmlStart;
	Middle = HtmlMiddle;
	End = HtmlEnd;
    }

    newResult = Tcl_NewObj();
    Tcl_AppendStringsToObj(newResult, Start, "Sockets open:", Middle, 0L);
    TclFormatInt(buf, StatOpenSockets);
    Tcl_AppendStringsToObj(newResult, buf, End, Start, "AcceptEx calls that returned an error:", Middle, 0L);
    TclFormatInt(buf, StatFailedAcceptExCalls);
    Tcl_AppendStringsToObj(newResult, buf, End, Start, "Unreplaced AcceptEx calls:", Middle, 0L);
    TclFormatInt(buf, StatFailedReplacementAcceptExCalls);
    Tcl_AppendStringsToObj(newResult, buf, End, Start, "General pool bytes in use:", Middle, 0L);
    TclFormatInt(buf, StatGeneralBytesInUse);
    Tcl_AppendStringsToObj(newResult, buf, End, Start, "Special pool bytes in use:", Middle, 0L);
    TclFormatInt(buf, StatSpecialBytesInUse);
    Tcl_AppendStringsToObj(newResult, buf, End, 0L);
    Tcl_SetObjResult(interp, newResult);
    return TCL_OK;
}

/* Set the EXTERN macro to export declared functions. */
#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT


/* This is the entry made to the dll (or static library) from Tcl. */
EXTERN int
Iocpsock_Init (Tcl_Interp *interp)
{
    Tcl_Obj *result;

#ifdef USE_TCL_STUBS
    if (Tcl_InitStubs(interp, "8.3", 0) == NULL) {
	return TCL_ERROR;
    }
#endif

    Tcl_MutexLock(&initLock);

    if (HasSockets(interp) == TCL_ERROR) {
	Tcl_MutexUnlock(&initLock);
	return TCL_ERROR;
    }
    result = Tcl_GetObjResult(interp);
    if (LOBYTE(winSock.wVersionLoaded) != 2) {
	Tcl_AppendObjToObj(result,
		Tcl_NewStringObj("Bad Winsock interface.  Need 2.x, but got ", -1));
	Tcl_AppendObjToObj(result, Tcl_NewDoubleObj(LOBYTE(winSock.wVersionLoaded)
		+ ((double)HIBYTE(winSock.wVersionLoaded)/10)));
	Tcl_AppendResult(interp, " instead.", NULL);
	winSock.WSACleanup();
	FreeLibrary(winSock.hModule);
	winSock.hModule = NULL;
	initialized = 0;
	Tcl_MutexUnlock(&initLock);
	return TCL_ERROR;
    }

    Tcl_MutexUnlock(&initLock);

    Tcl_CreateObjCommand(interp, "socket2", Iocp_SocketObjCmd, 0L, 0L);
    Tcl_CreateObjCommand(interp, "iocp_stats", Iocp_StatsObjCmd, 0L, 0L);
    Tcl_PkgProvide(interp, "Iocpsock", IOCPSOCK_VERSION);
    return TCL_OK;
}
