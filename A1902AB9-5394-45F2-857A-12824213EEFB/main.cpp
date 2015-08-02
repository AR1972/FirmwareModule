//
/* This program is free software. It comes without any warranty, to
* the extent permitted by applicable law. You can redistribute it
* and/or modify it under the terms of the Do What The Fuck You Want
* To Public License, Version 2, as published by Sam Hocevar. See
* http://sam.zoy.org/wtfpl/COPYING for more details. */
//
#include <Uefi.h>
#include <Protocol\AcpiSupport.h>
#include <Protocol\LegacyRegion.h>
#include <Protocol\LegacyBios.h>
#include <Guid\EventGroup.h>
#include <Guid\Acpi.h>
#include "main.h"
//
// 0 = do not touch (some firmware's update OemId's & OemTableId's automatically)
// 1 = copy OemId + OemTableId from SLIC to RSDP, RSDT, XSDT
// 2 = copy OemId + OemTableId from SLIC to all ACPI tables
//
#define PATCH_TABLES 0
//
// 1 = copy SLP 1.0 string to legacy memory region
//
#define SLP_INJECT 1
//
INTN
CompareMem (
    IN VOID     *Dest,
    IN VOID     *Src,
    IN UINTN    len
    )
{
    CHAR8    *d, *s;

    d = (CHAR8*)Dest;
    s = (CHAR8*)Src;
    while (len--) {
        if (*d != *s) {
            return *d - *s;
        }

        d += 1;
        s += 1;
    }

    return 0;
}
//
VOID *
	FindAcpiRsdPtr (
	VOID
	)
	/*
	Description:
		finds the RSDP in low memory
	*/
{
	UINTN Address;
	UINTN Index;
	//
	// First Seach 0x0e0000 - 0x0fffff for RSD Ptr
	//
	for (Address = 0xe0000; Address < 0xfffff; Address += 0x10) {
		if (*(UINT64 *)(Address) == ACPI_RSDP_SIG) {
			return (VOID *)Address;
		}
	}
	//
	// Search EBDA
	//
	Address = (*(UINT16 *)(UINTN)(EBDA_BASE_ADDRESS)) << 4;
	for (Index = 0; Index < 0x400 ; Index += 16) {
		if (*(UINT64 *)(Address + Index) == ACPI_RSDP_SIG) {
			return (VOID *)Address;
		}
	}
	return NULL;
}
//
UINT8
	ComputeChecksum (
	IN      CONST UINT8              *Buffer,
	IN      UINTN                     Length
	)
	/*
	Description:
		Compute byte checksum on buffer of given length.

	Arguments:
		Buffer		- Pointer to buffer to compute checksum
		Length		- Number of bytes to checksum

	Returns:
		Checksum	- Checksum of buffer
	*/
{
  UINT8     Sum;
  UINTN     Count;

  //ASSERT (Buffer != NULL);
  //ASSERT (Length <= (MAX_ADDRESS - ((UINTN) Buffer) + 1));

  for (Sum = 0, Count = 0; Count < Length; Count++) {
    Sum = (UINT8) (Sum + *(Buffer + Count));
  }
  
  return -Sum;
}
//
INTN
CompareGuid (
    IN EFI_GUID     *Guid1,
    IN EFI_GUID     *Guid2
    )
/*++

Routine Description:

    Compares to GUIDs

Arguments:

    Guid1       - guid to compare
    Guid2       - guid to compare

Returns:
    = 0     if Guid1 == Guid2

--*/
{
    INT32       *g1, *g2, r;

    //
    // Compare 32 bits at a time
    //

    g1 = (INT32 *) Guid1;
    g2 = (INT32 *) Guid2;

    r  = g1[0] - g2[0];
    r |= g1[1] - g2[1];
    r |= g1[2] - g2[2];
    r |= g1[3] - g2[3];

    return r;
}
//
//
EFI_STATUS
	GetSystemConfigurationTable (
	IN EFI_GUID *TableGuid,
	IN OUT VOID **Table
	)
	/*
	Description:
		Function returns a system configuration table that is stored in the 
		EFI System Table based on the provided GUID.

	Arguments:
		TableGuid        - A pointer to the table's GUID type.

		Table            - On exit, a pointer to a system configuration table.

	Returns:

		EFI_SUCCESS      - A configuration table matching TableGuid was found

		EFI_NOT_FOUND    - A configuration table matching TableGuid was not found
	*/
{
	UINTN Index;
	//
	for (Index = 0; Index < ST->NumberOfTableEntries; Index++) {
		if (CompareGuid (TableGuid, &(ST->ConfigurationTable[Index].VendorGuid)) == 0) {
			*Table = ST->ConfigurationTable[Index].VendorTable;
			return EFI_SUCCESS;
		}
	}
	return EFI_NOT_FOUND;
}
//
//
EFI_STATUS
	LegacyUnlock (
	VOID
	)
{
	EFI_STATUS Status = EFI_PROTOCOL_ERROR;
	EFI_LEGACY_REGION_PROTOCOL* LegacyRegionProtocol;
	UINT32 Granularity = 0;
	if(BS->LocateProtocol(&LegacyRegionGuid, NULL, (VOID **) &LegacyRegionProtocol) == EFI_SUCCESS) {
		if (LegacyRegionProtocol != NULL) {
			Status = LegacyRegionProtocol->UnLock(LegacyRegionProtocol, 0xF0000, 0xFFFF, &Granularity);
		}
	}
	return Status;
}
//
EFI_STATUS
	LegacyLock (
	VOID
	)
{
	EFI_STATUS Status = EFI_PROTOCOL_ERROR;
	EFI_LEGACY_REGION_PROTOCOL* LegacyRegionProtocol;
	UINT32 Granularity = 0;
	if(BS->LocateProtocol(&LegacyRegionGuid, NULL, (VOID **) &LegacyRegionProtocol) == EFI_SUCCESS) {
		if (LegacyRegionProtocol != NULL) {
			Status = LegacyRegionProtocol->Lock(LegacyRegionProtocol, 0xF0000, 0xFFFF, &Granularity);
		}
	}
	return Status;
}
//
VOID
	Main(
	IN EFI_EVENT Event,
	IN VOID *Context
	)
{
	EFI_ACPI_SUPPORT_PROTOCOL *AcpiSupportProtocol = 0;
	EFI_LEGACY_BIOS_PROTOCOL *LegacyBiosProtocol = 0;
	UINT64 Handel = 0;
	UINTN DataSize = 0;
	//EFI_TPL OldTpl;
	EFI_STATUS Status = EFI_UNSUPPORTED;
	Rsdp20Tbl_t *RsdpTable = 0;
	Rsdp20Tbl_t *LegacyRsdpTable = 0;
	RsdtTbl_t *RsdtTable = 0;
	XsdtTbl_t *XsdtTable = 0;
	SlicTbl_t *SlicTable = 0;
	AcpiTbl_t *AcpiTable = 0;
	VOID *LegacyAddress = 0;
	UINT8 SlpString[0x20] = { 0 };
	UINTN i = 0;
	//
	// code starts
	//
	Data = 1;
	Size = sizeof(Data);
	//
	// set failsafe byte to 1, if main fails to complete the module will be disabled
	//
	RS->SetVariable(FailSafeName, &FailSafeGuid, Attributes, Size, &Data);
	//
	SlicTable = (SlicTbl_t *) SLIC;
	//
	// add Marker and Public Key to empty SLIC
	//
	DataSize = sizeof(Marker_t);
	if (RS->GetVariable(OaMarkerName, &OaMarkerGuid, 0, &DataSize, &SlicTable->Marker) != EFI_SUCCESS ||
		DataSize != sizeof(Marker_t)) {
		return;
	}
	DataSize = sizeof(PublicKey_t);
	if (RS->GetVariable(OaPublicKeyName, &OaPublicKeyGuid, 0, &DataSize, &SlicTable->PublicKey) != EFI_SUCCESS ||
		DataSize != sizeof(PublicKey_t)) {
		return;
	}
	//
	// copy OemId, OemTableId from Marker to SLIC ACPI header
	//
	BS->CopyMem(SlicTable->Header.OemId, SlicTable->Marker.OemId, 6);
	BS->CopyMem(SlicTable->Header.OemTableId, SlicTable->Marker.OemTableId, 8);
	//
	// add SLIC to ACPI tables
	//
	if(BS->LocateProtocol(&AcpiProtocolGuid, NULL, (VOID **) &AcpiSupportProtocol) == EFI_SUCCESS) {
		Status = AcpiSupportProtocol->SetAcpiTable(AcpiSupportProtocol, (VOID *) SLIC, TRUE,
			EFI_ACPI_TABLE_VERSION_1_0B|EFI_ACPI_TABLE_VERSION_2_0|EFI_ACPI_TABLE_VERSION_3_0, &Handel);
	}

	if (Status != EFI_SUCCESS) {
		return;
	}

#if SLP_INJECT == 1
	//
	// add SLP 1.0 string to legacy region
	//
	DataSize = sizeof(SlpString);
	if (RS->GetVariable(OaSlpName, &OaSlpGuid, 0, &DataSize, SlpString) == EFI_SUCCESS) {
		if (BS->LocateProtocol(&LegacyBiosGuid, NULL, (VOID **) &LegacyBiosProtocol) == EFI_SUCCESS) {
			if (LegacyBiosProtocol->GetLegacyRegion(LegacyBiosProtocol, sizeof(SlpString), 1, 2, &LegacyAddress) == EFI_SUCCESS) {
				Status = LegacyBiosProtocol->CopyLegacyRegion(LegacyBiosProtocol, DataSize, LegacyAddress, SlpString);
			}
		}
	}
#endif

#if PATCH_TABLES >= 1
	//
	// find ACPI tables
	//
	OldTpl = BS->RaiseTPL(TPL_HIGH_LEVEL);
	Status = GetSystemConfigurationTable(&EfiAcpi20TableGuid, (VOID **) &RsdpTable);
	if (EFI_ERROR (Status)) {
		Status = GetSystemConfigurationTable(&EfiAcpiTableGuid, (VOID **) &RsdpTable);
	}
	if (Status == EFI_SUCCESS) {
		if (RsdpTable->Revision == 0) {
			RsdtTable = (RsdtTbl_t *) RsdpTable->RSDTAddress;
		}
		else if (RsdpTable->Revision == 2) {
			RsdtTable = (RsdtTbl_t *) RsdpTable->RSDTAddress;
			XsdtTable = (XsdtTbl_t *) RsdpTable->XSDTAddress;
		}
		else {
			return;
		}
	}
	else {
		return;
	}
	//
	// copy SLIC OemId, OemTableId to RSDP, RSDT, XSDT
	//
#if PATCH_TABLES == 2
	DataSize = (RsdtTable->Header.Length - sizeof(AcpiHeader_t)) << 2;
	for(i = 0 ; i < DataSize; i++) {
		AcpiTable = (AcpiTbl_t *) RsdtTable->Entry[i];
		if (AcpiTable != NULL) {
			if (CompareMem(AcpiTable->Header.OemId, RsdtTable->Header.OemId, 6) == 0 &&
				CompareMem(AcpiTable->Header.OemTableId, RsdtTable->Header.OemTableId, 8) == 0) {
				BS->CopyMem(AcpiTable->Header.OemId, SlicTable->Header.OemId, 6);
				BS->CopyMem(AcpiTable->Header.OemTableId, SlicTable->Header.OemTableId, 8);
				AcpiTable->Header.Checksum = 0;
				AcpiTable->Header.Checksum = ComputeChecksum((UINT8 *) AcpiTable, AcpiTable->Header.Length);
			}
		}
	}
#endif
	BS->CopyMem(RsdtTable->Header.OemId, SlicTable->Header.OemId, 6);
	BS->CopyMem(RsdtTable->Header.OemTableId, SlicTable->Header.OemTableId, 8);
	RsdtTable->Header.Checksum = 0;
	RsdtTable->Header.Checksum = ComputeChecksum((UINT8 *) RsdtTable, RsdtTable->Header.Length);
	BS->CopyMem(RsdpTable->OemId, SlicTable->Header.OemId, 6);
	RsdpTable->Checksum = 0;
	RsdpTable->Checksum = ComputeChecksum((UINT8 *) RsdpTable, 0x14);
	if(RsdpTable->Revision == 2) {
#if PATCH_TABLES == 2
		DataSize = (XsdtTable->Header.Length - sizeof(AcpiHeader_t)) << 3;
		for(i = 0 ; i < DataSize; i++) {
			AcpiTable = (AcpiTbl_t *) (XsdtTable->Entry[i] & 0xFFFFFFFF);
			if (AcpiTable != NULL) {
				if (CompareMem(AcpiTable->Header.OemId, XsdtTable->Header.OemId, 6) == 0 &&
					CompareMem(AcpiTable->Header.OemTableId, XsdtTable->Header.OemTableId, 8) == 0) {
					BS->CopyMem(AcpiTable->Header.OemId, SlicTable->Header.OemId, 6);
					BS->CopyMem(AcpiTable->Header.OemTableId, SlicTable->Header.OemTableId, 8);
					AcpiTable->Header.Checksum = 0;
					AcpiTable->Header.Checksum = ComputeChecksum((UINT8 *) AcpiTable, AcpiTable->Header.Length);
				}
			}
		}
#endif
		RsdpTable->ExtendedChecksum = 0;
		RsdpTable->ExtendedChecksum = ComputeChecksum((UINT8 *) RsdpTable, RsdpTable->Length);
		BS->CopyMem(XsdtTable->Header.OemId, SlicTable->Header.OemId, 6);
		BS->CopyMem(XsdtTable->Header.OemTableId, SlicTable->Header.OemTableId, 8);
		XsdtTable->Header.Checksum = 0;
		XsdtTable->Header.Checksum = ComputeChecksum((UINT8 *) XsdtTable, XsdtTable->Header.Length);
	}
	//
	// copy OemId to RSDP in legacy region
	//
	LegacyRsdpTable = (Rsdp20Tbl_t *) FindAcpiRsdPtr();
	if (LegacyRsdpTable != NULL && LegacyUnlock() == EFI_SUCCESS)
	{
		BS->CopyMem(LegacyRsdpTable->OemId, SlicTable->Header.OemId, 6);
		LegacyRsdpTable->RSDTAddress = RsdpTable->RSDTAddress;
		LegacyRsdpTable->Checksum = 0;
		LegacyRsdpTable->Checksum = ComputeChecksum((UINT8 *) LegacyRsdpTable, 0x14);
		if(LegacyRsdpTable->Revision == 2) {
			LegacyRsdpTable->XSDTAddress = RsdpTable->XSDTAddress;
			LegacyRsdpTable->ExtendedChecksum = 0;
			LegacyRsdpTable->ExtendedChecksum = ComputeChecksum((UINT8 *) LegacyRsdpTable, LegacyRsdpTable->Length);
		}
		LegacyLock();
	}
	BS->RestoreTPL(OldTpl);
#endif
	Data = 0;
	Size = sizeof(Data);
	//
	// set failsafe byte to 0
	//
	RS->SetVariable(FailSafeName, &FailSafeGuid, Attributes, Size, &Data);
	return;
}
//
EFI_STATUS
	ModuleEntryPoint (
	EFI_HANDLE ImageHandle,
	EFI_SYSTEM_TABLE* SystemTable
	)
{
	EFI_STATUS Status = EFI_SUCCESS;
	ST = SystemTable;
	BS = ST->BootServices;
	RS = ST->RuntimeServices;
	//
	// disable the module if failsafe byte is set to 1
	//
	RS->GetVariable(FailSafeName, &FailSafeGuid, &Attributes, &Size, &Data);
	if(Data == 0) {
		Status =  BS->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Main, NULL, &EventReadyToBootGuid, &EventReadyToBoot);
	}
	return Status;
}