// This file copies the generic aspect of socket I/O from the core for
// our personal use during testing.  100% COPIED.

// IOW, Nothing interesting in here; go read iocpsock_lolevel.c instead.


#include "iocpsock.h"

/*
 * Callback structure for accept callback in a TCP server.
 */

typedef struct AcceptCallback {
    char *script;			/* Script to invoke. */
    Tcl_Interp *interp;			/* Interpreter in which to run it. */
} AcceptCallback;


/* local protos */
static void	AcceptCallbackProc (ClientData callbackData,
	            Tcl_Channel chan, char *address, int port);
static void	RegisterTcpServerInterpCleanup (Tcl_Interp *interp,
	            AcceptCallback *acceptCallbackPtr);
static void	IocpAcceptCallbacksDeleteProc (
		    ClientData clientData, Tcl_Interp *interp);
static void	IocpServerCloseProc (ClientData callbackData);
static void	UnregisterTcpServerInterpCleanupProc (
		    Tcl_Interp *interp, AcceptCallback *acceptCallbackPtr);


/*
 *----------------------------------------------------------------------
 *
 * Iocp_SocketObjCmd --
 *
 *	This procedure is invoked to process the "socket" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Creates a socket based channel.
 *
 *----------------------------------------------------------------------
 */

int
Iocp_SocketObjCmd(notUsed, interp, objc, objv)
    ClientData notUsed;			/* Not used. */
    Tcl_Interp *interp;			/* Current interpreter. */
    int objc;				/* Number of arguments. */
    Tcl_Obj *CONST objv[];		/* Argument objects. */
{
    static CONST char *socketOptions[] = {
	"-async", "-myaddr", "-myport","-server", "-protocol", "-lookup", NULL
    };
    enum socketOptions {
	SKT_ASYNC, SKT_MYADDR, SKT_MYPORT, SKT_SERVER, SKT_PROTO, SKT_LOOKUP
    };
    int optionIndex, a, server;
    char *arg, *copyScript, *host, *script;
    char *myaddr = NULL;
    CONST char *myPortName = NULL;
    CONST char *portName = NULL;
    int async = 0;
    Tcl_Channel chan;
    AcceptCallback *acceptCallbackPtr;

    server = 0;
    script = NULL;

    for (a = 1; a < objc; a++) {
	arg = Tcl_GetString(objv[a]);
	if (arg[0] != '-') {
	    break;
	}
	if (Tcl_GetIndexFromObj(interp, objv[a], socketOptions,
		"option", TCL_EXACT, &optionIndex) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch ((enum socketOptions) optionIndex) {
	    case SKT_ASYNC: {
                if (server == 1) {
                    Tcl_AppendResult(interp,
                            "cannot set -async option for server sockets", NULL);
                    return TCL_ERROR;
                }
                async = 1;
		break;
	    }
	    case SKT_MYADDR: {
		a++;
                if (a >= objc) {
		    Tcl_AppendResult(interp,
			    "no argument given for -myaddr option", NULL);
		    return TCL_ERROR;
		}
                myaddr = Tcl_GetString(objv[a]);
		break;
	    }
	    case SKT_MYPORT: {
		a++;
                if (a >= objc) {
		    Tcl_AppendResult(interp,
			    "no argument given for -myport option", NULL);
		    return TCL_ERROR;
		}
		myPortName = Tcl_GetString(objv[a]);
		break;
	    }
	    case SKT_SERVER: {
                if (async == 1) {
                    Tcl_AppendResult(interp,
                            "cannot set -async option for server sockets", NULL);
                    return TCL_ERROR;
                }
		server = 1;
		a++;
		if (a >= objc) {
		    Tcl_AppendResult(interp,
			    "no argument given for -server option", NULL);
		    return TCL_ERROR;
		}
                script = Tcl_GetString(objv[a]);
		break;
	    }
	    case SKT_PROTO: {
		a++;
                if (a >= objc) {
		    Tcl_AppendResult(interp,
			    "no argument given for -protocol option", NULL);
		    return TCL_ERROR;
		}
		/* add code here */
		break;
	    }
	    case SKT_LOOKUP: {
		a++;
                if (a >= objc) {
		    Tcl_AppendResult(interp,
			    "no argument given for -lookup option", NULL);
		    return TCL_ERROR;
		}
		/* add code here */
		break;
	    }
	    default: {
		Tcl_Panic("Iocp_SocketObjCmd: bad option index to SocketOptions");
	    }
	}
    }
    if (server) {
        host = myaddr;		/* NULL implies INADDR_ANY */
	if (myPortName != NULL) {
	    Tcl_AppendResult(interp, "Option -myport is not valid for servers",
		    NULL);
	    return TCL_ERROR;
	}
    } else if (a < objc) {
	host = Tcl_GetString(objv[a]);
	a++;
    } else {
wrongNumArgs:
	Tcl_AppendResult(interp, "wrong # args: should be either:\n",
		Tcl_GetString(objv[0]),
                " ?-protocol type? ?-myaddr addr? ?-myport myport? ?-async? host port\n",
		Tcl_GetString(objv[0]),
                " -server command ?-protocol type? ?-myaddr addr? port\n",
		Tcl_GetString(objv[0]),
		" -lookup host ?-protocol type? ?-command command?", NULL);
        return TCL_ERROR;
    }

    if (a == objc-1) {
	portName = Tcl_GetString(objv[a]);
    } else {
	goto wrongNumArgs;
    }

    if (server) {
        acceptCallbackPtr = (AcceptCallback *) ckalloc((unsigned)
                sizeof(AcceptCallback));
        copyScript = ckalloc((unsigned) strlen(script) + 1);
        strcpy(copyScript, script);
        acceptCallbackPtr->script = copyScript;
        acceptCallbackPtr->interp = interp;
        chan = Iocp_OpenTcpServer(interp, portName, host, AcceptCallbackProc,
                (ClientData) acceptCallbackPtr);
        if (chan == (Tcl_Channel) NULL) {
            ckfree(copyScript);
            ckfree((char *) acceptCallbackPtr);
            return TCL_ERROR;
        }

        /*
         * Register with the interpreter to let us know when the
         * interpreter is deleted (by having the callback set the
         * acceptCallbackPtr->interp field to NULL). This is to
         * avoid trying to eval the script in a deleted interpreter.
         */

        RegisterTcpServerInterpCleanup(interp, acceptCallbackPtr);

        /*
         * Register a close callback. This callback will inform the
         * interpreter (if it still exists) that this channel does not
         * need to be informed when the interpreter is deleted.
         */

        Tcl_CreateCloseHandler(chan, IocpServerCloseProc,
                (ClientData) acceptCallbackPtr);
    } else {
        chan = Iocp_OpenTcpClient(interp, portName, host, myaddr, myPortName, async);
        if (chan == (Tcl_Channel) NULL) {
            return TCL_ERROR;
        }
    }
    Tcl_RegisterChannel(interp, chan);            
    Tcl_AppendResult(interp, Tcl_GetChannelName(chan), (char *) NULL);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * IocpServerCloseProc --
 *
 *	This callback is called when the TCP server channel for which it
 *	was registered is being closed. It informs the interpreter in
 *	which the accept script is evaluated (if that interpreter still
 *	exists) that this channel no longer needs to be informed if the
 *	interpreter is deleted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	In the future, if the interpreter is deleted this channel will
 *	no longer be informed.
 *
 *----------------------------------------------------------------------
 */

static void
IocpServerCloseProc(callbackData)
    ClientData callbackData;	/* The data passed in the call to
                                 * Tcl_CreateCloseHandler. */
{
    AcceptCallback *acceptCallbackPtr;
    				/* The actual data. */

    acceptCallbackPtr = (AcceptCallback *) callbackData;
    if (acceptCallbackPtr->interp != (Tcl_Interp *) NULL) {
        UnregisterTcpServerInterpCleanupProc(acceptCallbackPtr->interp,
                acceptCallbackPtr);
    }
    Tcl_EventuallyFree((ClientData) acceptCallbackPtr->script, TCL_DYNAMIC);
    ckfree((char *) acceptCallbackPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * RegisterTcpServerInterpCleanup --
 *
 *	Registers an accept callback record to have its interp
 *	field set to NULL when the interpreter is deleted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When, in the future, the interpreter is deleted, the interp
 *	field of the accept callback data structure will be set to
 *	NULL. This will prevent attempts to eval the accept script
 *	in a deleted interpreter.
 *
 *----------------------------------------------------------------------
 */

static void
RegisterTcpServerInterpCleanup(interp, acceptCallbackPtr)
    Tcl_Interp *interp;		/* Interpreter for which we want to be
                                 * informed of deletion. */
    AcceptCallback *acceptCallbackPtr;
    				/* The accept callback record whose
                                 * interp field we want set to NULL when
                                 * the interpreter is deleted. */
{
    Tcl_HashTable *hTblPtr;	/* Hash table for accept callback
                                 * records to smash when the interpreter
                                 * will be deleted. */
    Tcl_HashEntry *hPtr;	/* Entry for this record. */
    int New;			/* Is the entry new? */

    hTblPtr = (Tcl_HashTable *) Tcl_GetAssocData(interp,
            "iocpTCPAcceptCallbacks",
            NULL);
    if (hTblPtr == (Tcl_HashTable *) NULL) {
        hTblPtr = (Tcl_HashTable *) ckalloc((unsigned) sizeof(Tcl_HashTable));
        Tcl_InitHashTable(hTblPtr, TCL_ONE_WORD_KEYS);
        (void) Tcl_SetAssocData(interp, "iocpTCPAcceptCallbacks",
                IocpAcceptCallbacksDeleteProc, (ClientData) hTblPtr);
    }
    hPtr = Tcl_CreateHashEntry(hTblPtr, (char *) acceptCallbackPtr, &New);
    if (!New) {
        Tcl_Panic("RegisterTcpServerCleanup: damaged accept record table");
    }
    Tcl_SetHashValue(hPtr, (ClientData) acceptCallbackPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * IocpAcceptCallbacksDeleteProc --
 *
 *	Assocdata cleanup routine called when an interpreter is being
 *	deleted to set the interp field of all the accept callback records
 *	registered with	the interpreter to NULL. This will prevent the
 *	interpreter from being used in the future to eval accept scripts.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deallocates memory and sets the interp field of all the accept
 *	callback records to NULL to prevent this interpreter from being
 *	used subsequently to eval accept scripts.
 *
 *----------------------------------------------------------------------
 */

	/* ARGSUSED */
static void
IocpAcceptCallbacksDeleteProc(clientData, interp)
    ClientData clientData;	/* Data which was passed when the assocdata
                                 * was registered. */
    Tcl_Interp *interp;		/* Interpreter being deleted - not used. */
{
    Tcl_HashTable *hTblPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch hSearch;
    AcceptCallback *acceptCallbackPtr;

    hTblPtr = (Tcl_HashTable *) clientData;
    for (hPtr = Tcl_FirstHashEntry(hTblPtr, &hSearch);
             hPtr != (Tcl_HashEntry *) NULL;
             hPtr = Tcl_NextHashEntry(&hSearch)) {
        acceptCallbackPtr = (AcceptCallback *) Tcl_GetHashValue(hPtr);
        acceptCallbackPtr->interp = (Tcl_Interp *) NULL;
    }
    Tcl_DeleteHashTable(hTblPtr);
    ckfree((char *) hTblPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * UnregisterTcpServerInterpCleanupProc --
 *
 *	Unregister a previously registered accept callback record. The
 *	interp field of this record will no longer be set to NULL in
 *	the future when the interpreter is deleted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prevents the interp field of the accept callback record from
 *	being set to NULL in the future when the interpreter is deleted.
 *
 *----------------------------------------------------------------------
 */

static void
UnregisterTcpServerInterpCleanupProc(interp, acceptCallbackPtr)
    Tcl_Interp *interp;		/* Interpreter in which the accept callback
                                 * record was registered. */
    AcceptCallback *acceptCallbackPtr;
    				/* The record for which to delete the
                                 * registration. */
{
    Tcl_HashTable *hTblPtr;
    Tcl_HashEntry *hPtr;

    hTblPtr = (Tcl_HashTable *) Tcl_GetAssocData(interp,
            "iocpTCPAcceptCallbacks", NULL);
    if (hTblPtr == (Tcl_HashTable *) NULL) {
        return;
    }
    hPtr = Tcl_FindHashEntry(hTblPtr, (char *) acceptCallbackPtr);
    if (hPtr == (Tcl_HashEntry *) NULL) {
        return;
    }
    Tcl_DeleteHashEntry(hPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * AcceptCallbackProc --
 *
 *	This callback is invoked by the TCP channel driver when it
 *	accepts a new connection from a client on a server socket.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Whatever the script does.
 *
 *----------------------------------------------------------------------
 */

static void
AcceptCallbackProc(callbackData, chan, address, port)
    ClientData callbackData;		/* The data stored when the callback
                                         * was created in the call to
                                         * Tcl_OpenTcpServer. */
    Tcl_Channel chan;			/* Channel for the newly accepted
                                         * connection. */
    char *address;			/* Address of client that was
                                         * accepted. */
    int port;				/* Port of client that was accepted. */
{
    AcceptCallback *acceptCallbackPtr;
    Tcl_Interp *interp;
    char *script;
    char portBuf[TCL_INTEGER_SPACE];
    int result;

    acceptCallbackPtr = (AcceptCallback *) callbackData;

    /*
     * Check if the callback is still valid; the interpreter may have gone
     * away, this is signalled by setting the interp field of the callback
     * data to NULL.
     */
    
    if (acceptCallbackPtr->interp != (Tcl_Interp *) NULL) {

        script = acceptCallbackPtr->script;
        interp = acceptCallbackPtr->interp;
        
        Tcl_Preserve((ClientData) script);
        Tcl_Preserve((ClientData) interp);

	TclFormatInt(portBuf, port);
        Tcl_RegisterChannel(interp, chan);

        /*
         * Artificially bump the refcount to protect the channel from
         * being deleted while the script is being evaluated.
         */

        Tcl_RegisterChannel((Tcl_Interp *) NULL,  chan);
        
        result = Tcl_VarEval(interp, script, " ", Tcl_GetChannelName(chan),
                " ", address, " ", portBuf, (char *) NULL);
        if (result != TCL_OK) {
            Tcl_BackgroundError(interp);
	    Tcl_UnregisterChannel(interp, chan);
        }

        /*
         * Decrement the artificially bumped refcount. After this it is
         * not safe anymore to use "chan", because it may now be deleted.
         */

        Tcl_UnregisterChannel((Tcl_Interp *) NULL, chan);
        
        Tcl_Release((ClientData) interp);
        Tcl_Release((ClientData) script);
    } else {

        /*
         * The interpreter has been deleted, so there is no useful
         * way to utilize the client socket - just close it.
         */

        Tcl_Close((Tcl_Interp *) NULL, chan);
    }
}
