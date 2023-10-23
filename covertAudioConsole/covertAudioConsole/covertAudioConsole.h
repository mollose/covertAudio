#include <stdio.h>
#include "windows.h"
#define _USE_MATH_DEFINES
#include <cmath>
#include <string>

#include <Mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <Audioclient.h>
#include <Audiopolicy.h>

#define IOCTL_INIT_RETASK \
	CTL_CODE(FILE_DEVICE_SOUND, 0x801, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_PREPARE_RETASK \
	CTL_CODE(FILE_DEVICE_SOUND, 0x802, METHOD_BUFFERED, FILE_READ_DATA|FILE_WRITE_DATA)
#define IOCTL_DO_RETASK \
	CTL_CODE(FILE_DEVICE_SOUND, 0x803, METHOD_BUFFERED, FILE_WRITE_DATA)

#define STATUS_SUCCESS 0
#define STATUS_FAILURE -1
#define MAX_WIDGET 256

#define REFTIMES_PER_SEC 10000000
#define REFTIMES_PER_MILLISEC 10000

#define EXIT_ON_ERROR(hres) \
	if(FAILED(hres)) {goto Exit;}
#define SAFE_RELEASE(punk) \
	if((punk) != NULL) {(punk)->Release(); (punk) = NULL;}

typedef unsigned char uchar;

enum
{
	oldData = 0,
	inData = 1,
	outData = 2,
};

struct Jack
{
	DWORD nid;

	uchar portConn;
	uchar upperLoc;
	uchar lowerLoc;
	uchar defaultDev;
	uchar connType;
	uchar color;

	bool isOut;
	bool isIn;
	bool isDetect;
};

struct JackInfo
{
	DWORD nid, info;
};

struct MyAudio
{
	WAVEFORMATEX *rpwfx, *cpwfx;
	UINT32 outBufSz, inBufSz;

	uchar* outBuffer;
	DWORD owi, ori, ovn;
	uchar* inBuffer;
	DWORD iwi, iri, ivn;
};