#include "covertAudio.h"

volatile ulong LockAcquired = 0;
volatile ulong nCPUsLocked = 0;

void lockRoutine(PKDPC dpc, PVOID context, PVOID arg1, PVOID arg2)
{
	UNREFERENCED_PARAMETER(dpc);
	UNREFERENCED_PARAMETER(context);
	UNREFERENCED_PARAMETER(arg1);
	UNREFERENCED_PARAMETER(arg2);

	DBG_PRINT2("[lockRoutine] : beginCPU[%u]", KeGetCurrentProcessorNumber());
	InterlockedIncrement((LONG*)&nCPUsLocked);

	while (InterlockedCompareExchange((LONG*)&LockAcquired, 1, 1) == 0)
	{
		__asm
		{
			nop;
		}
	}

	InterlockedDecrement((LONG*)&nCPUsLocked);
	DBG_PRINT2("[lockRoutine] : endCPU[%u]", KeGetCurrentProcessorNumber());
	return;
}

NTSTATUS ReleaseLock(PVOID dpcPtr)
{
	InterlockedIncrement((LONG*)&LockAcquired);

	InterlockedCompareExchange((LONG*)&nCPUsLocked, 0, 0);
	while (nCPUsLocked != 0)
	{
		__asm
		{
			nop;
		}
		InterlockedCompareExchange((LONG*)&nCPUsLocked, 0, 0);
	}
	if (dpcPtr != NULL)
		ExFreePool(dpcPtr);
	return (STATUS_SUCCESS);
}

PKDPC AcquireLock()
{
	PKDPC dpcArray;
	ulong cpuID;
	ulong nOtherCPUs;

	if (KeGetCurrentIrql() != DISPATCH_LEVEL)
		return NULL;

	InterlockedAnd((LONG*)&LockAcquired, 0);
	InterlockedAnd((LONG*)&nCPUsLocked, 0);

	dpcArray = (PKDPC)ExAllocatePool(NonPagedPool, KeNumberProcessors * sizeof(KDPC));
	if (dpcArray == NULL){ return (NULL); };

	cpuID = KeGetCurrentProcessorNumber();

	for (uint i = 0; i < KeNumberProcessors; i++)
	{
		PKDPC dpcPtr = &(dpcArray[i]);
		if (i != cpuID)
		{
			KeInitializeDpc(dpcPtr, lockRoutine, NULL);
			KeSetTargetProcessorDpc(dpcPtr, i);
			KeInsertQueueDpc(dpcPtr, NULL, NULL);
		}
	}

	nOtherCPUs = KeNumberProcessors - 1;
	InterlockedCompareExchange((LONG*)&nCPUsLocked, nOtherCPUs, nOtherCPUs);
	while (nCPUsLocked != nOtherCPUs)
	{
		__asm
		{
			nop;
		}
		InterlockedCompareExchange((LONG*)&nCPUsLocked, nOtherCPUs, nOtherCPUs);
	}
	return (dpcArray);
}

KIRQL RaiseIRQL()
{
	KIRQL curr;
	KIRQL prev;

	curr = KeGetCurrentIrql();
	prev = curr;
	if (curr < DISPATCH_LEVEL)
		KeRaiseIrql(DISPATCH_LEVEL, &prev);

	return prev;
}

void LowerIRQL(KIRQL prev)
{
	KeLowerIrql(prev);
	return;
}