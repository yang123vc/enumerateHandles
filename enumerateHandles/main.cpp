#include <stdio.h>
#include <Windows.h>
#include <Subauth.h>
#include "Psapi.h"

#include <vector>

using namespace std;

#define STATUS_INFO_LENGTH_MISMATCH 0xc0000004

#define SystemHandleInformation     0x10

#define ObjectBasicInformation 0x0
#define ObjectNameInformation  0x1
#define ObjectTypeInformation  0x2

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#endif

typedef NTSTATUS (NTAPI *_NtQuerySystemInformation)(
    ULONG  SystemInformationClass,
    PVOID  SystemInformation,
    ULONG  SystemInformationLength,
    PULONG ReturnLength
);

typedef NTSTATUS (NTAPI *_NtDuplicateObject)(
    HANDLE SourceProcessHandle,
    HANDLE SourceHandle,
    HANDLE TargetProcessHandle,
    PHANDLE TargetHandle,
    ACCESS_MASK DesiredAccess,
    ULONG Attributes,
    ULONG Options
);

typedef NTSTATUS (NTAPI *_NtQueryObject)(
    HANDLE ObjectHandle,
    ULONG ObjectInformationClass,
    PVOID ObjectInformation,
    ULONG ObjectInformationLength,
    PULONG ReturnLength
);

typedef struct _SYSTEM_HANDLE
{
    ULONG ProcessId;
    BYTE ObjectTypeNumber;
    BYTE Flags;
    USHORT Handle;
    PVOID Object;
    ACCESS_MASK GrantedAccess;
} 
SYSTEM_HANDLE, *PSYSTEM_HANDLE;

typedef struct _SYSTEM_HANDLE_INFORMATION
{
    ULONG HandleCount;
    SYSTEM_HANDLE Handles[1];
} 
SYSTEM_HANDLE_INFORMATION, *PSYSTEM_HANDLE_INFORMATION;

typedef enum _POOL_TYPE
{
    NonPagedPool,
    PagedPool,
    NonPagedPoolMustSucceed,
    DontUseThisType,
    NonPagedPoolCacheAligned,
    PagedPoolCacheAligned,
    NonPagedPoolCacheAlignedMustS
} 
POOL_TYPE, *PPOOL_TYPE;

typedef struct _OBJECT_TYPE_INFORMATION
{
    UNICODE_STRING Name;
    ULONG TotalNumberOfObjects;
    ULONG TotalNumberOfHandles;
    ULONG TotalPagedPoolUsage;
    ULONG TotalNonPagedPoolUsage;
    ULONG TotalNamePoolUsage;
    ULONG TotalHandleTableUsage;
    ULONG HighWaterNumberOfObjects;
    ULONG HighWaterNumberOfHandles;
    ULONG HighWaterPagedPoolUsage;
    ULONG HighWaterNonPagedPoolUsage;
    ULONG HighWaterNamePoolUsage;
    ULONG HighWaterHandleTableUsage;
    ULONG InvalidAttributes;
    GENERIC_MAPPING GenericMapping;
    ULONG ValidAccess;
    BOOLEAN SecurityRequired;
    BOOLEAN MaintainHandleCount;
    USHORT MaintainTypeList;
    POOL_TYPE PoolType;
    ULONG PagedPoolUsage;
    ULONG NonPagedPoolUsage;
} 
OBJECT_TYPE_INFORMATION, *POBJECT_TYPE_INFORMATION;

typedef struct _THREAD_CONTEXT
{
	HANDLE hDup;
	char   szFileName[MAX_PATH];
}
THREAD_CONTEXT, *PTHREAD_CONTEXT;

typedef struct
{
	char   szFileName[MAX_PATH];
	char   szProcessName[MAX_PATH];
	HANDLE FileHandle;
	ULONG  ProcessId;
}
HANDLE_INFO, *PHANDLE_INFO;

_NtQuerySystemInformation NtQuerySystemInformation = (_NtQuerySystemInformation)GetProcAddress( GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation" );
_NtDuplicateObject        NtDuplicateObject        = (_NtDuplicateObject)GetProcAddress( GetModuleHandleA("ntdll.dll"), "NtDuplicateObject" );
_NtQueryObject            NtQueryObject            = (_NtQueryObject)GetProcAddress( GetModuleHandleA("ntdll.dll"), "NtQueryObject" );


BOOL SetPrivilege(
    HANDLE  hToken,
    LPCTSTR lpPrivilege,
    BOOL    bEnablePrivilege
)
{
    // Initializing variables
    TOKEN_PRIVILEGES    tkp         = {0};
    LUID                luid        = {0};
    TOKEN_PRIVILEGES    tkpPrevious = {0};
    DWORD               cbPrevious  =  0;
 
    // Check the parameters passed to the function
    if( ( !hToken ) || ( !lpPrivilege ) )
	{
        return FALSE;
	}
 
    if( !LookupPrivilegeValue( NULL, lpPrivilege, &luid ) )
	{
        return FALSE;
	}

    tkp.PrivilegeCount           = 1;
    tkp.Privileges[0].Luid       = luid;
    tkp.Privileges[0].Attributes = 0;
 
    cbPrevious = sizeof( TOKEN_PRIVILEGES );
    AdjustTokenPrivileges( hToken, FALSE, &tkp, sizeof( TOKEN_PRIVILEGES ), &tkpPrevious, &cbPrevious );
    if( GetLastError() != ERROR_SUCCESS )
	{
        return FALSE;
	}
 
    tkpPrevious.PrivilegeCount      = 1;
    tkpPrevious.Privileges[0].Luid  = luid;

    if( bEnablePrivilege )
	{
        tkpPrevious.Privileges[0].Attributes |= (SE_PRIVILEGE_ENABLED);
	}
    else
	{
        tkpPrevious.Privileges[0].Attributes ^= (SE_PRIVILEGE_ENABLED & tkpPrevious.Privileges[0].Attributes);
	}
 
    AdjustTokenPrivileges( hToken, FALSE, &tkpPrevious, cbPrevious, NULL, NULL );
    if( GetLastError() != ERROR_SUCCESS )
	{
        return FALSE;
	}
 
    return TRUE;
}

 
//
//  Set debug privilege
//
BOOL SetDebugPrivilege( BOOL bEnable )
{
    HANDLE hToken = NULL;
 
    if( !OpenProcessToken( GetCurrentProcess( ), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken ) )
        return FALSE;
 
    // Enable/Disable Debug Privilege
    if( !SetPrivilege( hToken, SE_DEBUG_NAME, bEnable ) )
    {
        CloseHandle(hToken);
        
		return FALSE;
    }
 
    CloseHandle(hToken);
 
    return TRUE;
}

DWORD WINAPI GetObjectNameThread( LPVOID lpParam )
{
	PVOID          objectNameInfo;
    UNICODE_STRING objectName;
	NTSTATUS       status;

	ULONG returnLength;

	PTHREAD_CONTEXT pCtx = (PTHREAD_CONTEXT)lpParam;

	objectNameInfo = malloc(0x1000);
	if( objectNameInfo == NULL )
	{
		//printf( "[ERROR] : can't allocate memory : 0x%08X\n", GetLastError() );

		return -1;
	}

	status = 
		NtQueryObject(
			pCtx->hDup,
			ObjectNameInformation,
			objectNameInfo,
			0x1000,
			&returnLength
		);

	if( !NT_SUCCESS(status) )
	{
		//printf( "[ERROR] : NtQueryObject fails : 0x%08X\n", status );

		free( objectNameInfo );

		return -1;
	}

	objectName = *(PUNICODE_STRING)objectNameInfo;

	if( 
		objectName.Length /*&&
		wcsncmp( pObjectTypeInfo->Name.Buffer, L"handles.c", pObjectTypeInfo->Name.Length / 2 ) == 0*/
	)
	{
		wcstombs( pCtx->szFileName, objectName.Buffer, MAX_PATH - 1 );

		/*printf(
			"%wZ\n",
			&objectName
		);*/
	}

	free( objectNameInfo );

	return 0;
}

inline void CanonicalizeNtPathName( char *szNtPathName, char *szCanonicalized )
{
	char  szNtDriveName[MAX_PATH + 1] = {0};
	DWORD dwDrivesMask;

	strncpy( szCanonicalized, szNtPathName, MAX_PATH );
	CharLower( szCanonicalized );

	// Get the bit mask of drive letters
	dwDrivesMask = ::GetLogicalDrives();

	if( dwDrivesMask )
	{
		// Go through all possible letters from a to z
		for( int i = 0; i < 26; ++i )
		{
			// Check if the respective bit is set
			if( dwDrivesMask & (1 << i) )
			{
				char szDrive[10] = {0};

				sprintf( szDrive, "%c:", 'A' + i );

				// obtain nt drive name from logical drive name
				if( QueryDosDevice( szDrive, szNtDriveName, MAX_PATH ) != 0 )
				{
					CharLower( szNtDriveName );

					// found it
					if( strstr( szCanonicalized, szNtDriveName ) )
					{
						sprintf( szCanonicalized, "%s%s", szDrive, &szNtPathName[ strlen( szNtDriveName ) ] ); 
						break;
					}
				}
			}
		}
	}

	if( szCanonicalized[0] == '\\' )
	{
		char szTmp[MAX_PATH + 1] = { 0 },
				szWindowsDir[MAX_PATH + 1] = {0};

		strncpy( szTmp, szCanonicalized, MAX_PATH );

		GetWindowsDirectory( szWindowsDir, MAX_PATH );

		sprintf( szCanonicalized, "%c:%s", szWindowsDir[0], szTmp );
	}

}

BOOLEAN GetProcessName( DWORD processID, char *szName )
{ 
	BOOLEAN bResult;

	HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID );

	bResult = GetProcessImageFileName( hProcess, szName, MAX_PATH ) != 0;

	return bResult;
}

void GetSystemHandles( vector<HANDLE_INFO> &HandlesList )
{
	NTSTATUS status;
	HANDLE   hProcess = NULL;
	ULONG    CurrentProcessId = GetCurrentProcessId();

	SYSTEM_HANDLE_INFORMATION *SystemHandleInfo;
	ULONG SystemInfoLength = sizeof(SYSTEM_HANDLE_INFORMATION) + ( sizeof(SYSTEM_HANDLE) * 100 );

	SystemHandleInfo = (PSYSTEM_HANDLE_INFORMATION)VirtualAlloc( NULL, SystemInfoLength, MEM_COMMIT, PAGE_READWRITE );
	if( SystemHandleInfo == NULL )
	{
		//printf( "[ERROR] : can't allocate memory\n" );

		goto end;
	}

	//
	// If the buffer lenght is too small, NtQuerySystemInformation return STATUS_INFO_LENGTH_MISMATCH as error,
	// but it doesn't specify the correct size.
	//
	while( 
		( status = 
			NtQuerySystemInformation( 
				SystemHandleInformation,
				SystemHandleInfo,
				SystemInfoLength,
				NULL
			)
		) == STATUS_INFO_LENGTH_MISMATCH
	)
	{
		VirtualFree( SystemHandleInfo, SystemInfoLength, MEM_RELEASE );

		SystemInfoLength *= 2;
		SystemHandleInfo = (PSYSTEM_HANDLE_INFORMATION)VirtualAlloc( NULL, SystemInfoLength, MEM_COMMIT, PAGE_READWRITE );
		if( SystemHandleInfo == NULL )
		{
			//printf( "[ERROR] : can't reallocate memory\n" );

			goto end;
		}
	}
	
	if( !NT_SUCCESS(status) )
	{
		//printf( "[ERROR] : NtQuerySystemInformation fails!\n" );
		
		goto end;
	}

	//printf( "# Enumerating %d handles.\n", SystemHandleInfo->HandleCount );

	for( unsigned int i = 0; i < SystemHandleInfo->HandleCount; i++ )
	{
		POBJECT_TYPE_INFORMATION pObjectTypeInfo = NULL;
        
		DWORD  dwThread, dwWaitObject;
		HANDLE dupHandle = NULL;

		char szNTProcessName[MAX_PATH +1] = {0};

		SYSTEM_HANDLE SystemHandle = SystemHandleInfo->Handles[i];
		ULONG         ProcessId    =  SystemHandle.ProcessId;


		if( CurrentProcessId == ProcessId )
			continue;

		//
		// This handle is a named pipe!
		//
		/*if( SystemHandle.GrantedAccess == 0x0012019f )
		{
			continue;
		}*/

		GetProcessName( ProcessId, szNTProcessName );

		hProcess = OpenProcess( PROCESS_DUP_HANDLE, TRUE, ProcessId );
		if( hProcess )
		{
			status = DuplicateHandle( hProcess, (HANDLE)SystemHandle.Handle, GetCurrentProcess(), &dupHandle, 0, FALSE, DUPLICATE_SAME_ACCESS );

			//status = 
			//	NtDuplicateObject(
			//		hProcess,
			//		(HANDLE)SystemHandle.Handle,
			//		GetCurrentProcess(),
			//		&dupHandle,
			//		0,
			//		0,
			//		0
			//	);

			if( !NT_SUCCESS(status) )
			{
				//printf( "[ERROR] : DuplicateHandle fails : 0x%08X\n", status );

				goto exitLoop;
			}

			pObjectTypeInfo = (OBJECT_TYPE_INFORMATION *)malloc(0x1000);
			if( pObjectTypeInfo == NULL )
			{
				//printf( "[ERROR] : can't allocate memory for pObjectTypeInfo.\n" );

				goto exitLoop;
			}

			status = 
				NtQueryObject(
					dupHandle,
					ObjectTypeInformation,
					pObjectTypeInfo,
					0x1000,
					NULL
				);

			if( !NT_SUCCESS(status) )
			{
				//printf( "[ERROR] : NtQueryObject fails : 0x%08X\n", status );

				goto exitLoop;
			}

			//
			// Check if this is a file handle.
			//
			if( wcsncmp( pObjectTypeInfo->Name.Buffer, L"File", pObjectTypeInfo->Name.Length / 2 ) != 0 )
			{
				goto exitLoop;
			}

			PTHREAD_CONTEXT pThreadCtx = (PTHREAD_CONTEXT)HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(THREAD_CONTEXT) );

			pThreadCtx->hDup = dupHandle;
			ZeroMemory( pThreadCtx->szFileName, MAX_PATH );

			HANDLE hThread =
				CreateThread( 
					NULL,
					0,
					GetObjectNameThread,
					pThreadCtx,
					0,
					&dwThread
				);

			dwWaitObject = WaitForSingleObject( hThread, 50 );

			if( dwWaitObject == WAIT_TIMEOUT )
			{
				TerminateThread( hThread, 0 );
			}
			else
			{
				HANDLE_INFO hInfo;

				//ZeroMemory( hInfo, sizeof(HANDLE_INFO) );

				char szFileName[MAX_PATH]    = {0},
					 szProcessName[MAX_PATH] = {0};

				CanonicalizeNtPathName( pThreadCtx->szFileName, hInfo.szFileName );
				CanonicalizeNtPathName( szNTProcessName, hInfo.szProcessName );

				hInfo.ProcessId  = ProcessId;
				hInfo.FileHandle = (HANDLE)SystemHandle.Handle;

				//printf( "File : %s Pid : %d (%s)\n", szFileName, ProcessId, szProcessName );

				HandlesList.push_back(hInfo);
			}

			CloseHandle(hThread);

exitLoop:

			if( pObjectTypeInfo != NULL )
			{
				free( pObjectTypeInfo );
			}

			CloseHandle( dupHandle );
			CloseHandle( hProcess );

		}
		else
		{
			//printf( "[ERROR] : Can't open process : 0x%08X\n", GetLastError() );

			continue;
		}
	}
	 
end:
	if( SystemHandleInfo != NULL )
	{
		VirtualFree( SystemHandleInfo, SystemInfoLength, MEM_RELEASE );
	}
}

BOOLEAN UnlockHandle( HANDLE_INFO Handle )
{
	HANDLE   hProcess;
	HANDLE   dupHandle;
	NTSTATUS status;


	hProcess = OpenProcess( PROCESS_DUP_HANDLE, TRUE, Handle.ProcessId );
	if( hProcess )
	{
		status = DuplicateHandle( hProcess, Handle.FileHandle, GetCurrentProcess(), &dupHandle, 0, FALSE, DUPLICATE_CLOSE_SOURCE );
		if( !NT_SUCCESS(status) )
		{
			printf( "[ERROR] : DuplicateHandle fails : 0x%08X\n", status );

			return FALSE;
		}

		CloseHandle( dupHandle );
		CloseHandle( hProcess );

		return TRUE;
	}

	return FALSE;
}

void main( int argc, char **argv )
{
	vector<HANDLE_INFO> HandlesList;
	vector<HANDLE_INFO>::iterator i, end; 

	SetDebugPrivilege(TRUE);

	printf( "# Gathering all handles.\n" );

	GetSystemHandles( HandlesList );

	for( i = HandlesList.begin(), end = HandlesList.end(); i != end; i++ )
	{
		printf( "[-] File : %s Pid : %d (%s)\n", i->szFileName, i->ProcessId, i->szProcessName );


		/*if( stricmp( "C:\\Program Files (x86)\\Avetix\\db\\aspdb.avx", i->szFileName ) == 0 )
		{
			printf( "[-] UNLOCK File : %s Pid : %d (%s)\n", i->szFileName, i->ProcessId, i->szProcessName ); 
			UnlockHandle( *i );
		}*/
	}

	printf( "# Total handles retrieved : %d\n", HandlesList.size() );


	return;
}