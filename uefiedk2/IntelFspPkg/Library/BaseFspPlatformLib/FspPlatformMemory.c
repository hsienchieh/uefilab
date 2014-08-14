/** @file

  Copyright (c) 2014, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/HobLib.h>
#include <Library/PeiServicesLib.h>
#include <Library/FspCommonLib.h>
#include <Guid/GuidHobFsp.h>
#include <FspGlobalData.h>
#include <FspApi.h>

/**
  Get system memory from HOB.

  @param[in,out] LowMemoryLength   less than 4G memory length
  @param[in,out] HighMemoryLength  greater than 4G memory length
**/
VOID
EFIAPI
FspGetSystemMemorySize (
  IN OUT UINT64              *LowMemoryLength,
  IN OUT UINT64              *HighMemoryLength
  )
{
  EFI_PEI_HOB_POINTERS    Hob;

  *HighMemoryLength = 0;
  *LowMemoryLength  = SIZE_1MB;
  //
  // Get the HOB list for processing
  //
  Hob.Raw = GetHobList ();

  //
  // Collect memory ranges
  //
  while (!END_OF_HOB_LIST (Hob)) {
    if (Hob.Header->HobType == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
      if (Hob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) {
        //
        // Need memory above 1MB to be collected here
        //
        if (Hob.ResourceDescriptor->PhysicalStart >= BASE_1MB &&
            Hob.ResourceDescriptor->PhysicalStart < (EFI_PHYSICAL_ADDRESS) BASE_4GB) {
          *LowMemoryLength += (UINT64) (Hob.ResourceDescriptor->ResourceLength);
        } else if (Hob.ResourceDescriptor->PhysicalStart >= (EFI_PHYSICAL_ADDRESS) BASE_4GB) {
          *HighMemoryLength += (UINT64) (Hob.ResourceDescriptor->ResourceLength);
        }
      }
    }
    Hob.Raw = GET_NEXT_HOB (Hob);
  }
}

/**
  Migrate bootloader data before destroying CAR.

**/
VOID
EFIAPI
FspMigrateTemporaryMemory (
  VOID
 )
{
  FSP_INIT_RT_COMMON_BUFFER  *FspInitRtBuffer;
  UINT32             BootLoaderTempRamStart;
  UINT32             BootLoaderTempRamEnd;
  UINT32             BootLoaderTempRamSize;
  UINT32             OffsetGap;
  UINT32             FspParamPtr;
  FSP_INIT_PARAMS   *FspInitParams;
  UINT32            *NewStackTop;
  VOID              *BootLoaderTempRamHob;
  VOID              *UpdDataRgnPtr;
  VOID              *PlatformDataPtr;

  //
  // Get the temporary memory range used by the bootloader
  //
  BootLoaderTempRamStart = PcdGet32(PcdTemporaryRamBase);
  BootLoaderTempRamSize  = PcdGet32(PcdTemporaryRamSize) - PcdGet32(PcdFspTemporaryRamSize);
  BootLoaderTempRamEnd   = BootLoaderTempRamStart +  BootLoaderTempRamSize;

  //
  // Build a Boot Loader Temporary Memory GUID HOB
  //
  BootLoaderTempRamHob = BuildGuidHob (&gFspBootLoaderTemporaryMemoryGuid, BootLoaderTempRamSize);
  CopyMem (BootLoaderTempRamHob, (VOID *)BootLoaderTempRamStart, BootLoaderTempRamSize);
  OffsetGap = (UINT32)BootLoaderTempRamHob - BootLoaderTempRamStart;

  //
  // Set a new stack frame for the continuation function
  //
  FspInitParams   = (FSP_INIT_PARAMS *)GetFspApiParameter ();
  FspInitRtBuffer = (FSP_INIT_RT_COMMON_BUFFER *)FspInitParams->RtBufferPtr;
  NewStackTop     = (UINT32 *)FspInitRtBuffer->StackTop - 1;
  SetFspCoreStackPointer (NewStackTop);

  //
  // Fix the FspInit Parameter Pointers to the new location.
  //
  FspParamPtr = GetFspApiParameter ();
  if (FspParamPtr >= BootLoaderTempRamStart && FspParamPtr < BootLoaderTempRamEnd) {
    SetFspApiParameter(FspParamPtr + OffsetGap);
  }

  FspInitParams = (FSP_INIT_PARAMS *)GetFspApiParameter ();
  if ((UINT32)(FspInitParams->RtBufferPtr) >= BootLoaderTempRamStart &&
      (UINT32)(FspInitParams->RtBufferPtr) <  BootLoaderTempRamEnd) {
    FspInitParams->RtBufferPtr = (VOID *)((UINT32)(FspInitParams->RtBufferPtr) + OffsetGap);
  }

  if ((UINT32)(FspInitParams->NvsBufferPtr) >= BootLoaderTempRamStart &&
      (UINT32)(FspInitParams->NvsBufferPtr) <  BootLoaderTempRamEnd) {
    FspInitParams->NvsBufferPtr = (VOID *)((UINT32)(FspInitParams->NvsBufferPtr) + OffsetGap);
  }

  if ((UINT32)(((FSP_INIT_RT_COMMON_BUFFER *)(FspInitParams->RtBufferPtr))->UpdDataRgnPtr) >= BootLoaderTempRamStart &&
      (UINT32)(((FSP_INIT_RT_COMMON_BUFFER *)(FspInitParams->RtBufferPtr))->UpdDataRgnPtr) <  BootLoaderTempRamEnd) {
    ((FSP_INIT_RT_COMMON_BUFFER *)(FspInitParams->RtBufferPtr))->UpdDataRgnPtr = \
           (VOID *)((UINT32)(((FSP_INIT_RT_COMMON_BUFFER *)(FspInitParams->RtBufferPtr))->UpdDataRgnPtr) + OffsetGap);
  }

  //
  // Update UPD pointer in FSP Global Data
  //
  UpdDataRgnPtr = ((FSP_INIT_RT_COMMON_BUFFER *)FspInitParams->RtBufferPtr)->UpdDataRgnPtr;
  if (UpdDataRgnPtr != NULL) {
    SetFspUpdDataPointer (UpdDataRgnPtr);
  }

  //
  // Update Platform data pointer in FSP Global Data
  //
  PlatformDataPtr = GetFspPlatformDataPointer ();
  if (((UINT32)PlatformDataPtr >= BootLoaderTempRamStart) &&
      ((UINT32)PlatformDataPtr <  BootLoaderTempRamEnd)) {
    SetFspPlatformDataPointer ((UINT8 *)PlatformDataPtr + OffsetGap);
  }

}
