#include "covertAudioConsole.h"

MyAudio* myaud = NULL;
Jack* myjack = NULL;

const char userDevicePath[] = "\\\\.\\CovertAudio";
static char portConn[][10] = { "Jack", "NoConn", "FixedFunc", "Both" };
static char upperLoc[][10] = { "External", "Internal", "Seperate", "Other" };
static char lowerLoc[][10] = { "N/A", "Rear", "Front", "Left", "Right", "Top", "Bottom" };
static char defaultDev[][10] = { "LineOut", "Speaker", "HPOut", "CD", "SPDIFOut",
								 "DigiOut", "ModemLine", "Handset", "LineIn", "AUX", "MicIn", "Telephony",
								 "SPDIFIn", "DigiIn", "Reserved", "Other" };
static char connType[][10] = { "Unknown", "1/8\"S/M", "1/4\"S/M", "ATAPI", "RCA", "Optical", "DigiOther", 
							   "AnlgOther", "DIN", "XLR/Pro", "Modem", "Combi", "Other" };
static char jackColor[][10] = { "Unknown", "Black", "Grey", "Blue", "Green", "Red", "Orange", "Yellow",
								"Purple", "Pink", "Reserved", "White", "Other" };

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);
const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

BYTE* makesine()
{
	BYTE* buf;
	DWORD offset, data;
	double db;
	DWORD bytes = (*myaud).rpwfx->wBitsPerSample / 8;
	buf = (BYTE*)malloc((*myaud).outBufSz * bytes);
	memset(buf, 0, (*myaud).outBufSz * bytes);

	for (int i = 0; i < (*myaud).outBufSz / 8; i++)
	{
		for (int j = 0; j < 8; j++)
		{
			db = (double)8 * sin(M_PI / (double)8 * (double)(j * 2)) + (double)8;
			offset = (int)db;
			if (offset == 16)
				offset = 15;
			data = 1 << offset;
			memcpy(&buf[((8 * i) + j) * bytes], &data, bytes / 2);
			memcpy(&buf[((8 * i) + j) * bytes] + (bytes / 2), &data, bytes / 2);
		}
	}

	return buf;
}

static int setData(BYTE* buffer, UINT32 size)
{
	DWORD amount, bytes = (*myaud).rpwfx->wBitsPerSample / 8;
	BYTE* buf = (*myaud).outBuffer + ((*myaud).owi * bytes);

	if ((*myaud).ovn >= (*myaud).outBufSz)
	{
		printf("[setData] : outBuffer is full\n");
		return 0;
	}

	if ((*myaud).owi + size > (*myaud).outBufSz)
	{
		amount = (*myaud).outBufSz - (*myaud).owi;
		memcpy(buf, buffer, amount * bytes);
		buf = buffer + (amount * bytes);
		memcpy((*myaud).outBuffer, buf, (size - amount) * bytes);
		(*myaud).owi = size - amount;
	}
	else
	{
		memcpy((*myaud).outBuffer, buffer, size * bytes);
		(*myaud).owi = ((*myaud).owi + size) % (*myaud).outBufSz;
	}

	(*myaud).ovn += size;
	return size;
}

static void loadData(UINT32 vsize, BYTE* buffer, DWORD* flags)
{
	DWORD amount, bytes = (*myaud).rpwfx->wBitsPerSample / 8;
	BYTE* buf = (*myaud).outBuffer + ((*myaud).ori * bytes);

	if ((*myaud).ovn <= 0)
	{
		*flags = AUDCLNT_BUFFERFLAGS_SILENT;
		return;
	}

	if (vsize > (*myaud).ovn)
	{
		if ((*myaud).ori + (*myaud).ovn > (*myaud).outBufSz)
		{
			amount = (*myaud).outBufSz - (*myaud).ori;
			memcpy(buffer, buf, amount * bytes);
			buf = buffer + (amount * bytes);
			memcpy(buf, (*myaud).outBuffer, ((*myaud).ovn - amount) * bytes);
			(*myaud).ori = (*myaud).ovn - amount; 
		}
		else
		{
			memcpy(buffer, buf, (*myaud).ovn * bytes);
			(*myaud).ori = ((*myaud).ori + (*myaud).ovn) % (*myaud).outBufSz;
		}
		(*myaud).ovn = 0;
	}
	else
	{
		if ((*myaud).ori + (*myaud).ovn > (*myaud).outBufSz)
		{
			amount = (*myaud).outBufSz - vsize;
			memcpy(buffer, buf, amount * bytes);
			buf = buffer + (amount * bytes);
			memcpy(buf, (*myaud).outBuffer, (vsize - amount) * bytes);
			(*myaud).ori = vsize - amount;
		}
		else
		{
			memcpy(buffer, buf, vsize * bytes);
			(*myaud).ori = ((*myaud).ori + vsize) % (*myaud).outBufSz;
		}
		(*myaud).ovn -= vsize;
	}
}

HRESULT recordAudio(IAudioClient* cClient)
{
	HRESULT hr;
	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	REFERENCE_TIME hnsActualDuration;
	UINT32 numFramesAvailable;
	IAudioCaptureClient* pCapture= NULL;
	UINT32 packetLength = 0;
	BOOL bDone = FALSE;
	BYTE* pData;
	DWORD flags;

	hr = cClient->GetService(IID_IAudioCaptureClient, (void**)&pCapture);
	EXIT_ON_ERROR(hr);

	hnsActualDuration = (double)REFTIMES_PER_SEC * (*myaud).inBufSz / (*myaud).cpwfx->nSamplesPerSec;

	hr = cClient->Start();
	EXIT_ON_ERROR(hr);

	while (bDone == FALSE)
	{
		Sleep(hnsActualDuration / REFTIMES_PER_MILLISEC / 2);

		hr = pCapture->GetNextPacketSize(&packetLength);
		EXIT_ON_ERROR(hr);

		while (packetLength != 0)
		{
			hr = pCapture->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
			EXIT_ON_ERROR(hr);

			if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
				pData = NULL;

			

			hr = pCapture->ReleaseBuffer(numFramesAvailable);
			EXIT_ON_ERROR(hr);

			hr = pCapture->GetNextPacketSize(&packetLength);
			EXIT_ON_ERROR(hr);
		}
	}
	hr = pCapture->GetNextPacketSize(&packetLength);
	EXIT_ON_ERROR(hr);

Exit :
	SAFE_RELEASE(pCapture);
	return hr;
}

HRESULT playAudio(IAudioClient* rClient)
{
	HRESULT hr;
	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	REFERENCE_TIME hnsActualDuration;
	IAudioRenderClient* pRender = NULL;
	UINT32 numFramesAvailable;
	UINT32 numFramesPadding;
	BYTE* pData;
	DWORD flags = 0;

	hr = rClient->GetService(IID_IAudioRenderClient, (void**)&pRender);
	EXIT_ON_ERROR(hr);

	hr = pRender->GetBuffer((*myaud).outBufSz, &pData);
	EXIT_ON_ERROR(hr);

	loadData((*myaud).outBufSz, pData, &flags);

	hr = pRender->ReleaseBuffer((*myaud).outBufSz, flags);
	EXIT_ON_ERROR(hr);

	hnsActualDuration = (double)REFTIMES_PER_SEC * (*myaud).outBufSz / (*myaud).rpwfx->nSamplesPerSec;
	
	hr = rClient->Start();
	EXIT_ON_ERROR(hr);

	while (flags != AUDCLNT_BUFFERFLAGS_SILENT)
	{
		Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));

		hr = rClient->GetCurrentPadding(&numFramesPadding);
		EXIT_ON_ERROR(hr);

		numFramesAvailable = (*myaud).outBufSz - numFramesPadding;

		hr = pRender->GetBuffer(numFramesAvailable, &pData);
		EXIT_ON_ERROR(hr);

		loadData(numFramesAvailable, pData, &flags);

		hr = pRender->ReleaseBuffer(numFramesAvailable, flags);
		EXIT_ON_ERROR(hr);
	}
	Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));

	hr = rClient->Stop();
	EXIT_ON_ERROR(hr);

Exit:
	SAFE_RELEASE(pRender);
	return hr;
}

static int doRetask(HANDLE hDeviceFile, DWORD datatype)
{
	BOOL opStatus = TRUE;
	JackInfo inBuffer, outBuffer;
	DWORD bytesRead;

	inBuffer.nid = myjack->nid;
	inBuffer.info = datatype;

	opStatus = DeviceIoControl(hDeviceFile, (DWORD)IOCTL_DO_RETASK, 
		&inBuffer, sizeof(JackInfo), &outBuffer, sizeof(JackInfo), &bytesRead, NULL);
	if (opStatus == FALSE)
	{
		printf("[initRetask] : IOCTL_DO_RETASK failed\n");
		return -1;
	}

	myjack->defaultDev = outBuffer.info >> 4;
	myjack->isOut = (outBuffer.info >> 1) & 1;
	myjack->isIn = outBuffer.info & 1;

	printf("[doRetask] : Jack #%X, DefaultDev %s, IsOut %s, IsIn %s\n", myjack->nid, 
		defaultDev[myjack->defaultDev], myjack->isOut ? "Y" : "N", myjack->isIn ? "Y" : "N");

	return 0;
}

static int initRetask(HANDLE hDeviceFile, IAudioClient* rClient, IAudioClient* cClient, DWORD vol)
{
	BOOL opStatus = TRUE;
	char* outBuffer = NULL;
	DWORD bytesRead, vid, rid, i, jacknum, oldnum; 
	DWORD nBufferSize = sizeof(JackInfo) * MAX_WIDGET + 8;
	JackInfo *bufRead, *best, *cur, inBuffer;

	HRESULT hr;
	WAVEFORMATEX *rpwfx = NULL, *cpwfx = NULL;
	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;

	outBuffer = (char*)malloc(nBufferSize);

	opStatus = DeviceIoControl(hDeviceFile, (DWORD)IOCTL_INIT_RETASK, NULL, 0,
		(LPVOID)outBuffer, nBufferSize, &bytesRead, NULL);
	if (opStatus == FALSE)
	{
		printf("[initRetask] : IOCTL_INIT_RETASK failed\n");
		return -1;
	}

	vid = ((DWORD*)outBuffer)[0];
	rid = ((DWORD*)outBuffer)[1];
	bufRead = &((JackInfo*)outBuffer)[1];
	jacknum = (bytesRead - 8) / sizeof(JackInfo);

	printf("[initRetask] : Vendor Id %X, Revision Id %X\n", vid, rid);
	
	best = cur = bufRead;
	for (i = 0; i < jacknum; i++)
	{	
		cur = &bufRead[i];
		if (best != cur)
		{
			if (!(best->info & 1) && cur->info & 1)
				best = cur;
			else if ((best->info & (3 << 28)) && !(cur->info & (3 << 28)))
				best = cur;
			else if ((best->info & (0xf << 20) >= 8) && (cur->info & (0xf << 20) < 8))
				best = cur;
			else if ((best->info & (0xf << 24) != 2) && (cur->info & (0xf << 24) == 2))
				best = cur;
		}
	}
	myjack = (Jack*)malloc(sizeof(Jack));
	memset(myjack, 0, sizeof(Jack));

	myjack->nid = best->nid;
	myjack->portConn = best->info >> 30;
	myjack->upperLoc = (best->info >> 28) & 3;
	myjack->lowerLoc = (best->info >> 24) & 0xf;
	myjack->defaultDev = (best->info >> 20) & 0xf;
	myjack->connType = (best->info >> 16) & 0xf;
	myjack->color = (best->info >> 12) & 0xf;
	myjack->isDetect = best->info & 1;

	printf("[initRetask] : Retaskable jack #%X(%s)\n", myjack->nid,
		myjack->isDetect ? "Detected" : "Not Detected");
	printf("[initRetask] : PortConn %s, Loc %s %s, DefaultDev %s, ConnType %s, JackColor %s\n",
		portConn[myjack->portConn], upperLoc[myjack->upperLoc],
		lowerLoc[myjack->lowerLoc], defaultDev[myjack->defaultDev],
		connType[myjack->connType], jackColor[myjack->color]);

	inBuffer.nid = myjack->nid;
	inBuffer.info = vol;

	hr = rClient->GetMixFormat(&rpwfx);
	EXIT_ON_ERROR(hr);
	hr = cClient->GetMixFormat(&cpwfx);
	EXIT_ON_ERROR(hr);

	hr = rClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsRequestedDuration, 0, rpwfx, NULL);
	EXIT_ON_ERROR(hr);
	hr = cClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsRequestedDuration, 0, cpwfx, NULL);
	EXIT_ON_ERROR(hr);

	myaud = (MyAudio*)malloc(sizeof(MyAudio));
	memset(myaud, 0, sizeof(MyAudio));

	myaud->rpwfx = rpwfx;
	myaud->cpwfx = cpwfx;

	hr = rClient->GetBufferSize(&myaud->outBufSz);
	hr = cClient->GetBufferSize(&myaud->inBufSz);

	opStatus = DeviceIoControl(hDeviceFile, (DWORD)IOCTL_PREPARE_RETASK, 
		&inBuffer, sizeof(JackInfo), (LPVOID)outBuffer, nBufferSize, &bytesRead, NULL);
	if (opStatus == FALSE)
	{
		printf("[initRetask] : IOCTL_PREPARE_RETASK failed\n");
		return -1;
	}

	bufRead = (JackInfo*)outBuffer;

	jacknum = bufRead->info;
	printf("[initRetask] : Out path ");
	for (i = 0; i < jacknum; i++)
	{
		printf("#%X", bufRead[i].nid);

		if (i != jacknum - 1)
			printf(" -(out)-> ");
		else
			printf("(Stream #%X)\n", bufRead[i].info);
	}
	oldnum = jacknum;

	for (i = 0; i < bytesRead / sizeof(JackInfo) - oldnum; i++)
	{
		jacknum = bufRead[oldnum].info >> 4;
		printf("[initRetask] : In path(#%d) ", i);
		for (int j = 0; j < jacknum; j++)
		{
			printf("#%X", bufRead[oldnum + j].nid);

			if (j == 0)
				printf("(Stream #%X)", bufRead[oldnum + j].info & 0xf);
			if (j != jacknum - 1)
				printf(" -(in)-> ");
			else
				printf("\n");
		}
		oldnum = oldnum + jacknum;
	}

Exit :
	if (outBuffer != NULL)
		free(outBuffer);

	return 0;
}

int setDeviceHandle(HANDLE* pHandle)
{
	*pHandle = CreateFileA(userDevicePath, GENERIC_READ | GENERIC_WRITE,
		0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (*pHandle == INVALID_HANDLE_VALUE)
	{
		printf("[setDeviceHandle] : CreateFileA() failed\n");
		return (STATUS_FAILURE);
	}

	return STATUS_SUCCESS;
}

int main(int argc, char* argv[])
{
	int retCode = STATUS_SUCCESS;
	HANDLE hDeviceFile = INVALID_HANDLE_VALUE;
	
	HRESULT hr;
	IMMDeviceEnumerator* pEnumerator = NULL;
	IMMDevice *pRenderDevice, *pCaptureDevice = NULL;
	IAudioClient *pAudioRenderClient = NULL, *pAudioCaptureClient = NULL;

	retCode = setDeviceHandle(&hDeviceFile);
	if (retCode != STATUS_SUCCESS)
	{ 
		printf("[main] : setDeviceHandle() failed\n");
		return (retCode); 
	}
	
	CoInitialize(NULL);

	hr = CoCreateInstance(CLSID_MMDeviceEnumerator,
		NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator);
	EXIT_ON_ERROR(hr);

	hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pRenderDevice);
	EXIT_ON_ERROR(hr);
	hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pCaptureDevice);
	EXIT_ON_ERROR(hr);

	hr = pRenderDevice->
		Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioRenderClient);
	EXIT_ON_ERROR(hr);
	hr = pCaptureDevice->
		Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioCaptureClient);
	EXIT_ON_ERROR(hr);

	if (initRetask(hDeviceFile, pAudioRenderClient, pAudioCaptureClient, 0x3f) < 0)
	{
		printf("[main] : initRetask() failed\n");
		return -1;
	}

	myaud->outBuffer = (uchar*)malloc(myaud->rpwfx->wBitsPerSample * myaud->outBufSz);
	myaud->inBuffer = (uchar*)malloc(myaud->cpwfx->wBitsPerSample * myaud->inBufSz);

	int type;
	BYTE* buf;
	while (1)
	{
		printf("insert type(old 0, in 1, out 2, play 3, exit 4) : ");
		scanf("%d", &type);

		if (type == 3)
		{
			buf = makesine();
			setData(buf, (*myaud).outBufSz);
			playAudio(pAudioRenderClient);
		}
		else if (type == 4)
			break;
		else
			doRetask(hDeviceFile, type);
	}

Exit :
	CoTaskMemFree(myaud->rpwfx);
	CoTaskMemFree(myaud->cpwfx);

	if (myjack != NULL)
		free(myjack);

	if (myaud != NULL)
	{
		if (myaud->inBuffer != NULL)
			free(myaud->inBuffer);
		if (myaud->outBuffer != NULL)
			free(myaud->outBuffer);

		free(myaud);
	}

	SAFE_RELEASE(pEnumerator);
	SAFE_RELEASE(pRenderDevice);
	SAFE_RELEASE(pCaptureDevice);
	SAFE_RELEASE(pAudioRenderClient);
	SAFE_RELEASE(pAudioCaptureClient);
	CoUninitialize();
	CloseHandle(hDeviceFile);
	return retCode;
}