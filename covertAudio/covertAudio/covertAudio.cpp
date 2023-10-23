#include "covertAudio.h"

myAudio* myaud = NULL;
JackRetask* myretask = NULL;

const WCHAR DeviceNameBuffer[] = L"\\Device\\CovertAudio";
const WCHAR DeviceLinkBuffer[] = L"\\DosDevices\\CovertAudio";
PDEVICE_OBJECT covertAudioDeviceObj;

JackInfo* enumJack(uint* num)
{
	Widget* cur;
	uint temp = 0, idx = 0;
	uint sendSize = (*myaud).codec.fgroup->nPins * sizeof(JackInfo) + 8;
	JackInfo* jackinfo = (JackInfo*)ExAllocatePool(NonPagedPool, sendSize);
	JackInfo* bufWrite;

	if (jackinfo == NULL)
		return NULL;

	((uint*)jackinfo)[0] = (*myaud).codec.vid;
	((uint*)jackinfo)[1] = (*myaud).codec.rid;
	bufWrite = &((JackInfo*)jackinfo)[1];
	
	for (uint i = (*myaud).codec.fgroup->base; 
		i < (*myaud).codec.fgroup->base + (*myaud).codec.fgroup->nWidgets; i++)
	{
		if ((*myaud).codec.widgets[i]->type == Wpin 
			&& (*myaud).codec.widgets[i]->portConn == 0)
		{
			cur = (*myaud).codec.widgets[i];
			if (cur->isInCap && cur->isOutCap && cur->isDetectCap)
			{
				bufWrite[idx].nid = cur->id.nid;
				DbgPrint("NID : %X\n", cur->id.nid);
				temp |= cur->portConn << 30;
				temp |= cur->upperLoc << 28;
				temp |= cur->lowerLoc << 24;
				temp |= cur->defaultDev.type[oldData] << 20;
				temp |= cur->connType << 16;
				temp |= cur->color << 12;
				temp |= cur->isDetect;

				bufWrite[idx].info = temp;
				idx++;
			}
		}
	}
	*num = idx;
	return jackinfo;
}

void searchHDA()
{
	ulong config_address;
	ulong ulData;
	LARGE_INTEGER bar, addr, strm;
	uint i, n, size, offset;
	Stream* cur;
	static int cmdbufsize[] = {2, 16, 256, 2048};

	bar.QuadPart = 0;
	KIRQL irql = RaiseIRQL();
	PKDPC dpcPtr = AcquireLock();

	for (i = 0; i < UWORD_MAX; i++)
	{
		config_address = (1ul << 31) | (i << 8) | 0x08;
		WRITE_PORT_ULONG((PULONG)0xcf8, config_address);
		ulData = READ_PORT_ULONG((PULONG)0xcfc);

		if ((ulData >> 8) == 0x40300)
		{
			config_address = (1ul << 31) | (i << 8) | 0x10;
			WRITE_PORT_ULONG((PULONG)0xcf8, config_address);
			bar.QuadPart = READ_PORT_ULONG((PULONG)0xcfc);
			break;
		}
	}

	if (bar.QuadPart == 0)
	{
		DBG_TRACE("searchHDA", "There is no audio device on PCI configuration space");
		return;
	}
	else
	{
		bar.QuadPart &= 0xfffffff0;
		DbgPrint("[searchHDA] : Bus %02X, Device %02X, Function %02X\n", 
			i >> 8, (i & 0xf8) >> 3, i & 7);

		DBG_PRINT3("[searchHDA] : Class code %X, Base address %X\n", ulData >> 8, bar);
	}

	myaud = (myAudio*)ExAllocatePool(NonPagedPool, sizeof(myAudio));
	memset(myaud, 0, sizeof(myAudio));

	(*myaud).mem = (uchar*)MmMapIoSpace(bar, HDA_REGISTER_SIZE, MmNonCached);
	if ((*myaud).mem != NULL)
	{
		(*myaud).size = HDA_REGISTER_SIZE;
		DBG_PRINT3("[searchHDA] : HDA register set(VA %X, PA %X) was mapped\n", (*myaud).mem, bar);
	}

	(*myaud).oss = (csr16((*myaud).mem, Gcap) & Oss) >> 12;
	(*myaud).iss = (csr16((*myaud).mem, Gcap) & Iss) >> 8;
	(*myaud).bss = (csr16((*myaud).mem, Gcap) & Bss) >> 4;

	DbgPrint("[searchHDA] : Iss %X, Oss %X, Bss %X\n", (*myaud).iss, (*myaud).oss, (*myaud).bss);

	strm.QuadPart = bar.QuadPart + 0x80;
	(*myaud).strm = (uchar*)MmMapIoSpace(strm, 
		HDA_STREAMDESC_SIZE * ((*myaud).oss + (*myaud).iss + (*myaud).bss), MmNonCached);

	(*myaud).istream = (Stream*)ExAllocatePool(NonPagedPool, sizeof(Stream) * (*myaud).iss);
	memset((*myaud).istream, 0, sizeof(Stream) * (*myaud).iss);

	(*myaud).ostream = (Stream*)ExAllocatePool(NonPagedPool, sizeof(Stream) * (*myaud).oss);
	memset((*myaud).ostream, 0, sizeof(Stream) * (*myaud).oss);

	(*myaud).bstream = (Stream*)ExAllocatePool(NonPagedPool, sizeof(Stream) * (*myaud).bss);
	memset((*myaud).bstream, 0, sizeof(Stream) * (*myaud).bss);

	getStream();

	if (csr8((*myaud).mem, Corbctl) & Corbdma == Corbdma)
	{
		size = csr8((*myaud).mem, Corbsz);
		n = cmdbufsize[size & 3];
		addr.LowPart = csr32((*myaud).mem, Corblbase);
		addr.HighPart = csr32((*myaud).mem, Corbubase);
		(*myaud).corb = (ulong*)MmMapIoSpace(addr, n * 4, MmNonCached);
		(*myaud).corbsize = n;

		DBG_PRINT2("[searchHDA] : CORB(VA %X, PA %X) was mapped\n", (*myaud).corb, addr);
	}

	if (csr8((*myaud).mem, Rirbctl) & Rirbdma == Rirbdma)
	{
		size = csr8((*myaud).mem, Rirbsz);
		n = cmdbufsize[size & 3];
		addr.LowPart = csr32((*myaud).mem, Rirblbase);
		addr.HighPart = csr32((*myaud).mem, Rirbubase);
		(*myaud).rirb = (ulong*)MmMapIoSpace(addr, n * 8, MmNonCached);
		(*myaud).rirbsize = n;

		DBG_PRINT2("[searchHDA] : RIRB(VA %X, PA %X) was mapped\n", (*myaud).rirb, addr);
	}

	ReleaseLock(dpcPtr);
	LowerIRQL(irql);
}

NTSTATUS RegisterDriverDeviceLink()
{
	NTSTATUS ntStatus;
	UNICODE_STRING unicodeString;
	UNICODE_STRING unicodeLinkString;

	RtlInitUnicodeString(&unicodeString, DeviceNameBuffer);
	RtlInitUnicodeString(&unicodeLinkString, DeviceLinkBuffer);
	ntStatus = IoCreateSymbolicLink(&unicodeLinkString, &unicodeString);

	return (ntStatus);
}

NTSTATUS RegisterDriverDeviceName(PDRIVER_OBJECT DriverObject)
{
	NTSTATUS ntStatus;
	UNICODE_STRING unicodeString;

	RtlInitUnicodeString(&unicodeString, DeviceNameBuffer);
	ntStatus = IoCreateDevice(DriverObject, 0, &unicodeString, FILE_DEVICE_SOUND, 
		0, TRUE, &covertAudioDeviceObj);

	return (ntStatus);
}

NTSTATUS dispatchIOControl(PDEVICE_OBJECT DeviceObject, PIRP IRP)
{
	PIO_STACK_LOCATION irpStack;
	PVOID inputBuffer;
	PVOID outputBuffer;
	ULONG inBufferLength;
	ULONG outBufferLength;
	ULONG ioctrlcode;
	NTSTATUS ntStatus;
	uint i;

	ntStatus = STATUS_SUCCESS;
	((*IRP).IoStatus).Status = STATUS_SUCCESS;
	((*IRP).IoStatus).Information = 0;

	inputBuffer = (*IRP).AssociatedIrp.SystemBuffer;
	outputBuffer = (*IRP).AssociatedIrp.SystemBuffer;

	irpStack = IoGetCurrentIrpStackLocation(IRP);
	inBufferLength = (*irpStack).Parameters.DeviceIoControl.InputBufferLength;
	outBufferLength = (*irpStack).Parameters.DeviceIoControl.OutputBufferLength;
	ioctrlcode = (*irpStack).Parameters.DeviceIoControl.IoControlCode;

	DBG_TRACE("dispatchIOControl", "Received a command");

	switch (ioctrlcode)
	{
	case IOCTL_INIT_RETASK :
	{
		DBG_TRACE("dispatchIOControl", "IOCTL_ENUM_JACK was received");
		uint jacknum;

		JackInfo* jackinfo = enumJack(&jacknum);
		if (jackinfo == NULL)
		{
			DBG_TRACE("dispatchIOControl", "Can't enumerate jacks");
			ntStatus = STATUS_DATA_ERROR;
			break;
		}

		memcpy(outputBuffer, (PVOID)jackinfo, sizeof(JackInfo) * jacknum + 8);
		((*IRP).IoStatus).Information = sizeof(JackInfo) * jacknum + 8;

		if (myretask == NULL)
			myretask = (JackRetask*)ExAllocatePool(NonPagedPool, sizeof(JackRetask));
		memset(myretask, 0, sizeof(JackRetask));

		KIRQL irql = RaiseIRQL();
		PKDPC dpcPtr = AcquireLock();

		getStream();

		ReleaseLock(dpcPtr);
		LowerIRQL(irql);

		myretask->oformer = 0;
		for (i = 0; i < (*myaud).oss; i++)
			if ((*myaud).ostream[i].run == 1)
				myretask->oformer |= (1 << i);

		myretask->iformer = 0;
		for (i = 0; i < (*myaud).iss; i++)
			if ((*myaud).istream[i].run == 1)
				myretask->iformer |= (1 << i);

		ExFreePool(jackinfo);
		break;
	}
	case IOCTL_PREPARE_RETASK:
	{
		DBG_TRACE("dispatchIOControl", "IOCTL_PREPARE_JACK was received");
		JackInfo* jack = (JackInfo*)inputBuffer;

		myretask->jack = (*myaud).codec.widgets[jack->nid];
		myretask->vol = jack->info;

		KIRQL irql = RaiseIRQL();
		PKDPC dpcPtr = AcquireLock();

		getStream();

		for (i = (*myaud).codec.fgroup->base;
			i < (*myaud).codec.fgroup->base + (*myaud).codec.fgroup->nWidgets; i++)
		{
			if ((*myaud).codec.widgets[i]->type == Wain)
				getWidget((*myaud).codec.widgets[i], oldData);
		}

		ReleaseLock(dpcPtr);
		LowerIRQL(irql);

		myretask->olatter = 0;
		for (i = 0; i < (*myaud).oss; i++)
			if ((*myaud).ostream[i].run == 1)
				myretask->olatter |= (1 << i);

		myretask->ilatter = 0;
		for (i = 0; i < (*myaud).iss; i++)
			if ((*myaud).istream[i].run == 1)
				myretask->ilatter |= (1 << i);

		ulong oresult = myretask->oformer ^ myretask->olatter;
		ulong iresult = myretask->iformer ^ myretask->ilatter;

		for (i = 0; i < (*myaud).oss; i++)
			if ((oresult >> i) & 1)
				myretask->ostrm = &(*myaud).ostream[i];

		if (myretask->ostrm == NULL)
		{
			DBG_TRACE("dispatchIOControl", "Out stream not found");
			ntStatus = STATUS_DATA_ERROR;
			break;
		}
		else
			DbgPrint("[dispatchIOControl] : Out stream #%X found\n", myretask->ostrm->strm);

		for (i = 0; i < (*myaud).iss; i++)
			if ((iresult >> i) & 1)
				myretask->nistrm++;

		myretask->istrm = (Stream**)ExAllocatePool(NonPagedPool, sizeof(Stream*) * myretask->nistrm);
		uint idx = 0;
		for (i = 0; i < (*myaud).iss; i++)
		{
			if ((iresult >> i) & 1)
			{
				myretask->istrm[idx] = &(*myaud).istream[i];
				DbgPrint("[dispatchIOControl] : In stream #%X found\n", myretask->istrm[idx++]->strm);
			}
		}
		if (idx == 0)
		{
			DBG_TRACE("dispatchIOControl", "In stream not found");
			ntStatus = STATUS_DATA_ERROR;
			break;
		}

		myretask->nain = myretask->nistrm;
		myretask->ain = (Widget**)ExAllocatePool(NonPagedPool, sizeof(Widget*) * myretask->nain);
		idx = 0;

		for (i = (*myaud).codec.fgroup->base;
			i < (*myaud).codec.fgroup->base + (*myaud).codec.fgroup->nWidgets; i++)
		{
			if ((*myaud).codec.widgets[i]->type == Wain &&
				(*myaud).codec.widgets[i]->strm.type[oldData] != 0)
			{
				for (uint j = 0; j < myretask->nistrm; j++)
				{
					if ((*myaud).codec.widgets[i]->strm.type[oldData] == myretask->istrm[j]->strm)
					{
						myretask->ain[j] = (*myaud).codec.widgets[i];
						DbgPrint("[dispatchIOControl] : Input converter #%X(In stream #%X) found\n",
							myretask->ain[j]->id.nid, myretask->istrm[j]->strm);
						idx++;
					}
				}
			}
		}
		if (idx != myretask->nistrm)
		{
			DBG_TRACE("dispatchIOControl", "Input converter not found");
			ntStatus = STATUS_DATA_ERROR;
			break;
		}

		irql = RaiseIRQL();
		dpcPtr = AcquireLock();

		if (initRetask(myretask->jack, NULL, outData, myretask->vol) < 0)
		{
			DBG_TRACE("dispatchIOControl", "Can't initiate out path");
			ntStatus = STATUS_DATA_ERROR;
			break;
		}
		myretask->aout = myretask->jack->end;

		for (i = 0; i < myretask->nain; i++)
		{
			if (initRetask(myretask->ain[i], myretask->jack, inData, myretask->vol) < 0)
			{
				DBG_TRACE("dispatchIOControl", "Can't initiate in path");
				ntStatus = STATUS_DATA_ERROR;
				break;
			}
		}

		ReleaseLock(dpcPtr);
		LowerIRQL(irql);

		uint n = 0, oldn = 0;
		Widget* cur;
		jack = (JackInfo*)ExAllocatePool(NonPagedPool,
			(*myaud).codec.fgroup->nWidgets * sizeof(JackInfo));

		cur = myretask->jack;
		while (cur)
		{
			jack[n].nid = cur->id.nid;

			if (cur->type == Waout)
				jack[n].info = cur->strm.type[oldData];

			n++;
			cur = cur->out;
		}
		jack[0].info = n;
		oldn = n;

		for (i = 0; i < myretask->nain; i++)
		{
			cur = myretask->ain[i];
			while (cur)
			{
				jack[n].nid = cur->id.nid;

				if (cur->type == Wain)
					jack[n].info = cur->strm.type[oldData];

				n++;
				cur = cur->in;
			}
			jack[oldn].info |= (n - oldn) << 4;
			oldn = n;
		}

		memcpy(outputBuffer, (PVOID)jack, sizeof(JackInfo) * n);
		((*IRP).IoStatus).Information = sizeof(JackInfo) * n;

		ExFreePool(jack);
		break;
	}
	case IOCTL_DO_RETASK:
	{
		JackInfo *jack = (JackInfo*)inputBuffer, outinfo = {0, };
		uint datatype = jack->info, i;

		KIRQL irql = RaiseIRQL();
		PKDPC dpcPtr = AcquireLock();

		if (datatype == outData)
		{
			doRetask(myretask->jack, outData, 
				myretask->ostrm->strm, myretask->aout->lowChan.type[oldData]);
		}
		else if (datatype == inData)
		{
			for (i = 0; i < myretask->nain; i++)
				doRetask(myretask->ain[i], inData, 
					myretask->istrm[i]->strm, myretask->ain[i]->lowChan.type[oldData]);
		}
		else if (datatype == oldData)
		{
			restoreRetask(myretask->jack, outData);

			for (i = 0; i < myretask->nain; i++)
				restoreRetask(myretask->ain[i], inData);
		}
		
		getWidget(myretask->jack, datatype);
		ReleaseLock(dpcPtr);
		LowerIRQL(irql);

		outinfo.nid = jack->nid;
		outinfo.info |= myretask->jack->defaultDev.type[datatype] << 4;
		outinfo.info |= myretask->jack->isOut.type[datatype] << 1;
		outinfo.info |= myretask->jack->isIn.type[datatype];

		memcpy(outputBuffer, (PVOID)&outinfo, sizeof(JackInfo));
		((*IRP).IoStatus).Information = sizeof(JackInfo);
		break;
	}
	}

	IoCompleteRequest(IRP, IO_NO_INCREMENT);
	return (ntStatus);
}

NTSTATUS defaultDispatch(PDEVICE_OBJECT DeviceObject, PIRP IRP)
{
	((*IRP).IoStatus).Status = STATUS_SUCCESS;
	((*IRP).IoStatus).Information = 0;
	IoCompleteRequest(IRP, IO_NO_INCREMENT);

	return (STATUS_SUCCESS);
}

VOID Unload(PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	DBG_TRACE("Unload", "Received signal to unload the driver");

	uint i;
	UNICODE_STRING unicodeString;

	if (covertAudioDeviceObj != NULL)
	{
		DBG_TRACE("Unload", "Unregistering driver's symbolic link");
		RtlInitUnicodeString(&unicodeString, DeviceLinkBuffer);
		IoDeleteSymbolicLink(&unicodeString);

		DBG_TRACE("Unload", "Unregistering driver's device name");
		IoDeleteDevice(covertAudioDeviceObj);
	}

	//restoreRetask((*myaud).codec.widgets[0x18], outData);
	//restoreRetask((*myaud).codec.widgets[8], inData);
	//restoreRetask((*myaud).codec.widgets[9], inData);

	if (myaud != NULL)
	{
		if ((*myaud).corb != NULL)
		{
			MmUnmapIoSpace((*myaud).corb, (*myaud).corbsize * 4);
			DBG_TRACE("Unload", "CORB was unmapped");
		}

		if ((*myaud).rirb != NULL)
		{
			MmUnmapIoSpace((*myaud).rirb, (*myaud).rirbsize * 8);
			DBG_TRACE("Unload", "RIRB was unmapped");
		}

		if ((*myaud).strm != NULL)
		{
			MmUnmapIoSpace((*myaud).strm, 
				HDA_STREAMDESC_SIZE * ((*myaud).oss + (*myaud).iss + (*myaud).bss));
			DBG_TRACE("Unload", "Stream Descriptors were unmapped");
		}

		if ((*myaud).mem != NULL)
		{
			MmUnmapIoSpace((*myaud).mem, HDA_REGISTER_SIZE);
			DBG_TRACE("Unload", "HDA register set was unmapped");
		}

		Widget** freeConn = NULL;
		Widget* freeWidgt = NULL;
		Amp* freeAmp = NULL;

		for (i = (*myaud).codec.fgroup->base; 
			i < (*myaud).codec.fgroup->base + (*myaud).codec.fgroup->nWidgets; i++)
		{
			freeWidgt = (*myaud).codec.widgets[i];
			freeConn = freeWidgt->list;
			freeAmp = freeWidgt->inAmp;

			if (freeConn != NULL)
				ExFreePool(freeConn);

			if (freeAmp != NULL)
				ExFreePool(freeAmp);

			ExFreePool(freeWidgt);
		}
		DBG_TRACE("Unload", "All widgets were released");

		Funcgroup* freeFg = (*myaud).codec.fgroup;
		Funcgroup* temp = NULL;

		for (;;)
		{
			if (freeFg == NULL)
			{
				DBG_TRACE("Unload", "All function groups were released");
				break;
			}
			temp = freeFg->next;
			ExFreePool(freeFg);
			freeFg = temp;
		}

		ExFreePool((*myaud).istream);
		ExFreePool((*myaud).ostream);
		ExFreePool((*myaud).bstream);
		DBG_TRACE("Unload", "All stream descriptors were released");

		if (myretask != NULL)
		{
			if (myretask->istrm != NULL)
				ExFreePool(myretask->istrm);

			if (myretask->ain != NULL)
				ExFreePool(myretask->ain);
			
			ExFreePool(myretask);
		}

		if (myaud != NULL)
			ExFreePool(myaud);
		DBG_TRACE("Unload", "MyAudio was released");
	}
	return;
}

#ifdef __cplusplus
extern "C"{
#endif

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING regPath)
{
	UNREFERENCED_PARAMETER(regPath);
	DBG_TRACE("DriverEntry", "Driver has been loaded");

	for (uint i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
	{
		(*DriverObject).MajorFunction[i] = defaultDispatch;
	}

	(*DriverObject).MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatchIOControl;
	(*DriverObject).DriverUnload = Unload;

	NTSTATUS ntStatus;

	DBG_TRACE("DriverEntry", "Registering driver's device name");
	ntStatus = RegisterDriverDeviceName(DriverObject);
	if (!NT_SUCCESS(ntStatus))
	{
		DBG_TRACE("DriverEntry", "Failed to create device");
		return ntStatus;
	}

	DBG_TRACE("DriverEntry", "Registering driver's symbolic link");
	ntStatus = RegisterDriverDeviceLink();
	if (!NT_SUCCESS(ntStatus))
	{
		DBG_TRACE("DriverEntry", "Failed to create symbolic link");
		return ntStatus;
	}

	searchHDA();
	if (myaud == NULL)
	{
		DBG_TRACE("DriverEntry", "myAudio structure is not allocated");
		return (STATUS_FAILED_DRIVER_ENTRY);
	}

	KIRQL irql = RaiseIRQL();
	PKDPC dpcPtr = AcquireLock();

	int status = enumDevice();

	ReleaseLock(dpcPtr);
	LowerIRQL(irql);

	if (status == 0)
	{
		DBG_TRACE("DriverEntry", "Codec was found");
	}
	else
		DBG_TRACE("DriverEntry", "Codec was not found");

	//initRetask((*myaud).codec.widgets[0x18], NULL, outData, 0x3f);
	//doRetask((*myaud).codec.widgets[0x18], outData, 4, 0);
	//initRetask((*myaud).codec.widgets[8], (*myaud).codec.widgets[0x18], inData, 0x3f);
	//initRetask((*myaud).codec.widgets[9], (*myaud).codec.widgets[0x18], inData, 0x3f);
	//doRetask((*myaud).codec.widgets[8], inData, 2, 0);
	//doRetask((*myaud).codec.widgets[9], inData, 1, 0);

	return (STATUS_SUCCESS);
}

#ifdef __cplusplus
}
#endif

