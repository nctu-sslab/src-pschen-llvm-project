//===------ omptarget.cpp - Target independent OpenMP target RTL -- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the interface to be used by Clang during the codegen of a
// target region.
//
//===----------------------------------------------------------------------===//

#include <omptarget.h>

#include "device.h"
#include "private.h"
#include "rtl.h"

#include <cassert>
#include <vector>
#include <inttypes.h>

#include "perf.h"
#include "at.h"
#include "rttype.h"

#ifdef OMPTARGET_DEBUG
int DebugLevel = 0;
int DebugLevel2 = 0;
#endif // OMPTARGET_DEBUG

int OptHostShadow = 0;

/* All begin addresses for partially mapped structs must be 8-aligned in order
 * to ensure proper alignment of members. E.g.
 *
 * struct S {
 *   int a;   // 4-aligned
 *   int b;   // 4-aligned
 *   int *p;  // 8-aligned
 * } s1;
 * ...
 * #pragma omp target map(tofrom: s1.b, s1.p[0:N])
 * {
 *   s1.b = 5;
 *   for (int i...) s1.p[i] = ...;
 * }
 *
 * Here we are mapping s1 starting from member b, so BaseAddress=&s1=&s1.a and
 * BeginAddress=&s1.b. Let's assume that the struct begins at address 0x100,
 * then &s1.a=0x100, &s1.b=0x104, &s1.p=0x108. Each member obeys the alignment
 * requirements for its type. Now, when we allocate memory on the device, in
 * CUDA's case cuMemAlloc() returns an address which is at least 256-aligned.
 * This means that the chunk of the struct on the device will start at a
 * 256-aligned address, let's say 0x200. Then the address of b will be 0x200 and
 * address of p will be a misaligned 0x204 (on the host there was no need to add
 * padding between b and p, so p comes exactly 4 bytes after b). If the device
 * kernel tries to access s1.p, a misaligned address error occurs (as reported
 * by the CUDA plugin). By padding the begin address down to a multiple of 8 and
 * extending the size of the allocated chuck accordingly, the chuck on the
 * device will start at 0x200 with the padding (4 bytes), then &s1.b=0x204 and
 * &s1.p=0x208, as they should be to satisfy the alignment requirements.
 */
static const int64_t alignment = 8;

/// Map global data and execute pending ctors
static int InitLibrary(DeviceTy& Device) {
  /*
   * Map global data
   */
  int32_t device_id = Device.DeviceID;
  int rc = OFFLOAD_SUCCESS;

  Device.PendingGlobalsMtx.lock();
  TrlTblMtx.lock();
  for (HostEntriesBeginToTransTableTy::iterator
      ii = HostEntriesBeginToTransTable.begin();
      ii != HostEntriesBeginToTransTable.end(); ++ii) {
    TranslationTable *TransTable = &ii->second;
    if (TransTable->TargetsTable[device_id] != 0) {
      // Library entries have already been processed
      continue;
    }

    // 1) get image.
    assert(TransTable->TargetsImages.size() > (size_t)device_id &&
           "Not expecting a device ID outside the table's bounds!");
    __tgt_device_image *img = TransTable->TargetsImages[device_id];
    if (!img) {
      DP("No image loaded for device id %d.\n", device_id);
      rc = OFFLOAD_FAIL;
      break;
    }
    // 2) load image into the target table.
    __tgt_target_table *TargetTable =
        TransTable->TargetsTable[device_id] = Device.load_binary(img);
    // Unable to get table for this image: invalidate image and fail.
    if (!TargetTable) {
      DP("Unable to generate entries table for device id %d.\n", device_id);
      TransTable->TargetsImages[device_id] = 0;
      rc = OFFLOAD_FAIL;
      break;
    }

    // Verify whether the two table sizes match.
    size_t hsize =
        TransTable->HostTable.EntriesEnd - TransTable->HostTable.EntriesBegin;
    size_t tsize = TargetTable->EntriesEnd - TargetTable->EntriesBegin;

    // Invalid image for these host entries!
    if (hsize != tsize) {
      DP("Host and Target tables mismatch for device id %d [%zx != %zx].\n",
         device_id, hsize, tsize);
      TransTable->TargetsImages[device_id] = 0;
      TransTable->TargetsTable[device_id] = 0;
      rc = OFFLOAD_FAIL;
      break;
    }

    // process global data that needs to be mapped.
    Device.DataMapMtx.lock();
    __tgt_target_table *HostTable = &TransTable->HostTable;
    for (__tgt_offload_entry *CurrDeviceEntry = TargetTable->EntriesBegin,
                             *CurrHostEntry = HostTable->EntriesBegin,
                             *EntryDeviceEnd = TargetTable->EntriesEnd;
         CurrDeviceEntry != EntryDeviceEnd;
         CurrDeviceEntry++, CurrHostEntry++) {
      if (CurrDeviceEntry->size != 0) {
        // has data.
        assert(CurrDeviceEntry->size == CurrHostEntry->size &&
               "data size mismatch");

        // Fortran may use multiple weak declarations for the same symbol,
        // therefore we must allow for multiple weak symbols to be loaded from
        // the fat binary. Treat these mappings as any other "regular" mapping.
        // Add entry to map.
        if (Device.getTgtPtrBegin(CurrHostEntry->addr, CurrHostEntry->size))
          continue;
        DP("Add mapping from host " DPxMOD " to device " DPxMOD " with size %zu"
            "\n", DPxPTR(CurrHostEntry->addr), DPxPTR(CurrDeviceEntry->addr),
            CurrDeviceEntry->size);
        Device.HostDataToTargetMap.emplace(
            (uintptr_t)CurrHostEntry->addr /*HstPtrBase*/,
            (uintptr_t)CurrHostEntry->addr /*HstPtrBegin*/,
            (uintptr_t)CurrHostEntry->addr + CurrHostEntry->size /*HstPtrEnd*/,
            (uintptr_t)CurrDeviceEntry->addr /*TgtPtrBegin*/,
            INF_REF_CNT /*RefCount*/);
      }
    }
    Device.DataMapMtx.unlock();
  }
  TrlTblMtx.unlock();

  if (rc != OFFLOAD_SUCCESS) {
    Device.PendingGlobalsMtx.unlock();
    return rc;
  }

  /*
   * Run ctors for static objects
   */
  if (!Device.PendingCtorsDtors.empty()) {
    // Call all ctors for all libraries registered so far
    for (auto &lib : Device.PendingCtorsDtors) {
      if (!lib.second.PendingCtors.empty()) {
        DP("Has pending ctors... call now\n");
        for (auto &entry : lib.second.PendingCtors) {
          void *ctor = entry;
          int rc = target(device_id, ctor, 0, NULL, NULL, NULL,
                          NULL, 1, 1, true /*team*/);
          if (rc != OFFLOAD_SUCCESS) {
            DP("Running ctor " DPxMOD " failed.\n", DPxPTR(ctor));
            Device.PendingGlobalsMtx.unlock();
            return OFFLOAD_FAIL;
          }
        }
        // Clear the list to indicate that this device has been used
        lib.second.PendingCtors.clear();
        DP("Done with pending ctors for lib " DPxMOD "\n", DPxPTR(lib.first));
      }
    }
  }
  Device.HasPendingGlobals = false;
  Device.PendingGlobalsMtx.unlock();

  return OFFLOAD_SUCCESS;
}

// Check whether a device has been initialized, global ctors have been
// executed and global data has been mapped; do so if not already done.
int CheckDeviceAndCtors(int64_t device_id) {
  // Is device ready?
  if (!device_is_ready(device_id)) {
    DP("Device %" PRId64 " is not ready.\n", device_id);
    return OFFLOAD_FAIL;
  }

  // Get device info.
  DeviceTy &Device = Devices[device_id];

  // Check whether global data has been mapped for this device
  Device.PendingGlobalsMtx.lock();
  bool hasPendingGlobals = Device.HasPendingGlobals;
  Device.PendingGlobalsMtx.unlock();
  if (hasPendingGlobals && InitLibrary(Device) != OFFLOAD_SUCCESS) {
    DP("Failed to init globals on device %" PRId64 "\n", device_id);
    return OFFLOAD_FAIL;
  }

  return OFFLOAD_SUCCESS;
}

static int32_t member_of(int64_t type) {
  return ((type & OMP_TGT_MAPTYPE_MEMBER_OF) >> 48) - 1;
}


/// Internal function to do the mapping and transfer the data to the device
int target_data_begin(DeviceTy &Device, int32_t arg_num,
    void **args_base, void **args, int64_t *arg_sizes, int64_t *arg_types) {
  if (Device.IsBulkEnabled) {
    return bulk_target_data_begin(Device, arg_num,
        args_base, args, arg_sizes, arg_types);
  }
  void *HstPtrBegin, *HstPtrBase;
  int64_t data_size, data_type;

  int ret;
  RttTy Rtt;
  if (arg_num && arg_types[0] & OMP_TGT_MAPTYPE_HAS_NESTED) {
    Rtt.init(args + arg_num);
  }

  // process each input.
  for (int32_t i = 0; i < arg_num; ++i) {
    // Ignore private variables and arrays - there is no mapping for them.
    if ((arg_types[i] & OMP_TGT_MAPTYPE_LITERAL) ||
        (arg_types[i] & OMP_TGT_MAPTYPE_PRIVATE))
      continue;

    HstPtrBegin = args[i];
    HstPtrBase = args_base[i];
    data_size = arg_sizes[i];
    data_type = arg_types[i];

    if (data_type & OMP_TGT_MAPTYPE_NESTED) {
      // init Rtt with stack addr
      ret = Rtt.newRttObject(&HstPtrBegin, &HstPtrBase,
          &data_size, &data_type);
      if (ret != RTT_SUCCESS) {
        DP2("RTT init failed");
        continue;
      }
DCGEN_REPEAT:
      // Regen Begin, Base, size maptype
      ret = Rtt.computeRegion();
      if (ret == RTT_END) {
        continue;
      }
      if (ret != RTT_SUCCESS) {
        return OFFLOAD_FAIL;
      }
    }

    DP2("Base: %p Ptr: %p size: %" PRId64 " type: 0x%lx\n", HstPtrBase, HstPtrBegin, data_size, data_type);
    // Adjust for proper alignment if this is a combined entry (for structs).
    // Look at the next argument - if that is MEMBER_OF this one, then this one
    // is a combined entry.
    int64_t padding = 0;
    const int next_i = i+1;
    // FIXME rtt
    if (member_of(arg_types[i]) < 0 && next_i < arg_num &&
        member_of(arg_types[next_i]) == i) {
      padding = (int64_t)HstPtrBegin % alignment;
      if (padding) {
        DP("Using a padding of %" PRId64 " bytes for begin address " DPxMOD
            "\n", padding, DPxPTR(HstPtrBegin));
        HstPtrBegin = (char *) HstPtrBegin - padding;
        data_size += padding;
      }
    }

    // Address of pointer on the host and device, respectively.
    void *Pointer_HstPtrBegin, *Pointer_TgtPtrBegin;
    bool IsNew, Pointer_IsNew;
    bool IsImplicit = data_type & OMP_TGT_MAPTYPE_IMPLICIT;
    // UpdateRef is based on MEMBER_OF instead of TARGET_PARAM because if we
    // have reached this point via __tgt_target_data_begin and not __tgt_target
    // then no argument is marked as TARGET_PARAM ("omp target data map" is not
    // associated with a target region, so there are no target parameters). This
    // may be considered a hack, we could revise the scheme in the future.
    bool UpdateRef = !(data_type & OMP_TGT_MAPTYPE_MEMBER_OF);
    if (data_type & OMP_TGT_MAPTYPE_PTR_AND_OBJ) {
      DP("Has a pointer entry: \n");
      // base is address of pointer.
      Pointer_TgtPtrBegin = Device.getOrAllocTgtPtr(HstPtrBase, HstPtrBase,
          sizeof(void *), Pointer_IsNew, IsImplicit, UpdateRef);
      if (!Pointer_TgtPtrBegin) {
        DP("Call to getOrAllocTgtPtr returned null pointer (device failure or "
            "illegal mapping).\n");
        return OFFLOAD_FAIL;
      }
      DP("There are %zu bytes allocated at target address " DPxMOD " - is%s new"
          "\n", sizeof(void *), DPxPTR(Pointer_TgtPtrBegin),
          (Pointer_IsNew ? "" : " not"));
      Pointer_HstPtrBegin = HstPtrBase;
      // modify current entry.
      HstPtrBase = *(void **)HstPtrBase;
      UpdateRef = true; // subsequently update ref count of pointee
    }

    void *TgtPtrBegin = Device.getOrAllocTgtPtr(HstPtrBegin, HstPtrBase,
        data_size, IsNew, IsImplicit, UpdateRef);
    if (!TgtPtrBegin && data_size) {
      // If data_size==0, then the argument could be a zero-length pointer to
      // NULL, so getOrAlloc() returning NULL is not an error.
      DP("Call to getOrAllocTgtPtr returned null pointer (device failure or "
          "illegal mapping).\n");
    }
    DP("There are %" PRId64 " bytes allocated at target address " DPxMOD
        " - is%s new\n", data_size, DPxPTR(TgtPtrBegin),
        (IsNew ? "" : " not"));

    if (data_type & OMP_TGT_MAPTYPE_RETURN_PARAM) {
      uintptr_t Delta = (uintptr_t)HstPtrBegin - (uintptr_t)HstPtrBase;
      void *TgtPtrBase = (void *)((uintptr_t)TgtPtrBegin - Delta);
      DP("Returning device pointer " DPxMOD "\n", DPxPTR(TgtPtrBase));
      // FIXME rtt ?
      args_base[i] = TgtPtrBase;
    }

    if (data_type & OMP_TGT_MAPTYPE_TO) {
      bool copy = false;
      if (IsNew || (data_type & OMP_TGT_MAPTYPE_ALWAYS)) {
        copy = true;
      } else if (data_type & OMP_TGT_MAPTYPE_MEMBER_OF) {
        // Copy data only if the "parent" struct has RefCount==1.
        // FIXME rtt
        int32_t parent_idx = member_of(data_type);
        long parent_rc = Device.getMapEntryRefCnt(args[parent_idx]);
        assert(parent_rc > 0 && "parent struct not found");
        if (parent_rc == 1) {
          copy = true;
        }
      }

      if (copy) {
        DP("Moving %" PRId64 " bytes (hst:" DPxMOD ") -> (tgt:" DPxMOD ")\n",
            data_size, DPxPTR(HstPtrBegin), DPxPTR(TgtPtrBegin));
        int rt = Device.data_submit(TgtPtrBegin, HstPtrBegin, data_size);
        if (rt != OFFLOAD_SUCCESS) {
          DP("Copying data to device failed.\n");
          return OFFLOAD_FAIL;
        }
        if (Device.IsDCEnabled && data_size == sizeof(void*)) {
          // FIXME what if this is not a address???
          // TODO Add flag in clang
          void *ptr = *(void**)HstPtrBegin;
          mm_context_t *context = NULL;
          if (_MYMALLOC_ISMYSPACE(ptr)) {
            context = get_mm_context(ptr);
          } else if (Device.ATMode & OMP_OFFMODE_AT_TABLE) {
            context = get_mm_context(ptr);
          }
          if (context) {
            DP2("Transfer " DPxMOD " with  dc object #%d\n",
                DPxPTR(ptr), context->id);
            rt = context->data_submit();
            if (rt != OFFLOAD_SUCCESS) {
              return OFFLOAD_FAIL;
            }
          }
        }
      }
    }
    if (data_type & OMP_TGT_MAPTYPE_PTR_AND_OBJ) {
      PERF_WRAP(Perf.UpdatePtr.start();)
      DP("Update pointer (" DPxMOD ") -> [" DPxMOD "]\n",
          DPxPTR(Pointer_TgtPtrBegin), DPxPTR(TgtPtrBegin));
      uint64_t Delta = (uint64_t)HstPtrBegin - (uint64_t)HstPtrBase;
      void *TgtPtrBase = (void *)((uint64_t)TgtPtrBegin - Delta);
      int rt = Device.data_submit(Pointer_TgtPtrBegin, &TgtPtrBase,
          sizeof(void *));
      if (rt != OFFLOAD_SUCCESS) {
        DP("Copying data to device failed.\n");
        return OFFLOAD_FAIL;
      }
      // create shadow pointers for this entry
      Device.ShadowMtx.lock();
      Device.ShadowPtrMap[Pointer_HstPtrBegin] = {HstPtrBase,
          Pointer_TgtPtrBegin, TgtPtrBase};
      Device.ShadowMtx.unlock();
      PERF_WRAP(Perf.UpdatePtr.end();)
    }
skip_update:
    if (data_type & OMP_TGT_MAPTYPE_NESTED) {
      goto DCGEN_REPEAT;
    }
  }

  return OFFLOAD_SUCCESS;
}


// New version of target data begin bulk
// With data env
int bulk_target_data_begin(DeviceTy &Device, int32_t arg_num,
    void **args_base, void **args, int64_t *arg_sizes, int64_t *arg_types) {
  DP2("target_data_begin\n");
  void *HstPtrBegin, *HstPtrBase;
  int64_t data_size, data_type;

  int ret;
  RttTy Rtt;
  if (arg_num && arg_types[0] & OMP_TGT_MAPTYPE_HAS_NESTED) {
    Rtt.init(args + arg_num, arg_sizes + arg_num);
  }

  // process each input.
  for (int32_t i = 0; i < arg_num; ++i) {
    // Ignore private variables and arrays - there is no mapping for them.
    if ((arg_types[i] & OMP_TGT_MAPTYPE_LITERAL) ||
        (arg_types[i] & OMP_TGT_MAPTYPE_PRIVATE)) {
      continue;
    }
    HstPtrBegin = args[i];
    HstPtrBase = args_base[i];
    data_size = arg_sizes[i];
    data_type = arg_types[i];

    if (data_type & OMP_TGT_MAPTYPE_NESTED) {
      // init Rtt with stack addr
      ret = Rtt.newRttObject(&HstPtrBegin, &HstPtrBase,
          &data_size, &data_type);
      if (ret != RTT_SUCCESS) {
        DP2("RTT init failed");
        continue;
      }
DCGEN_REPEAT:
      // Regen Begin, Base, size maptype
      ret = Rtt.computeRegion();
      if (ret == RTT_END) {
        continue;
      }
      if (ret != RTT_SUCCESS) {
        return OFFLOAD_FAIL;
      }
    }

    DP2("Addr %p Base: %p size: %" PRId64 " type: 0x%lx\n",
        HstPtrBegin, HstPtrBase, data_size, data_type);

    // Adjust for proper alignment if this is a combined entry (for structs).
    // Look at the next argument - if that is MEMBER_OF this one, then this one
    // is a combined entry.
    int64_t padding = 0;
    const int next_i = i+1;
    if (member_of(data_type) < 0 && next_i < arg_num &&
        member_of(arg_types[next_i]) == i) {
      padding = (int64_t)HstPtrBegin % alignment;
      if (padding) {
        DP2("Using a padding of %" PRId64 " bytes for begin address " DPxMOD
            "\n", padding, DPxPTR(HstPtrBegin));
        HstPtrBegin = (char *) HstPtrBegin - padding;
        data_size += padding;
      }
    }

    // Address of pointer on the host and device, respectively.
    void *Pointer_HstPtrBegin, *Pointer_TgtPtrBegin;
    //void *Pointer_HstPtrBegin; //*Pointer_TgtPtrBegin;
    bool IsNew, Pointer_IsNew;
    bool IsImplicit = data_type & OMP_TGT_MAPTYPE_IMPLICIT;
    // UpdateRef is based on MEMBER_OF instead of TARGET_PARAM because if we
    // have reached this point via __tgt_target_data_begin and not __tgt_target
    // then no argument is marked as TARGET_PARAM ("omp target data map" is not
    // associated with a target region, so there are no target parameters). This
    // may be considered a hack, we could revise the scheme in the future.
    bool UpdateRef = !(data_type & OMP_TGT_MAPTYPE_MEMBER_OF);
    if (data_type & OMP_TGT_MAPTYPE_PTR_AND_OBJ) {
      DP2("Has a pointer entry: \n");
      // base is address of pointer.
      intptr_t ret;
      //Pointer_TgtPtrBegin = AT(HstPtrBase) translated;
      //Pointer_TgtPtrBegin = Device.getOrAllocTgtPtr(HstPtrBase, HstPtrBase,
      ret = (intptr_t) Device.getOrAllocTgtPtr(HstPtrBase, HstPtrBase,
          sizeof(void *), Pointer_IsNew, IsImplicit, UpdateRef);
      if (!ret) {
        DP2("Call to getOrAllocTgtPtr failed (device failure or "
            "illegal mapping).\n");
        return OFFLOAD_FAIL;
      }
      DP2("There are %zu bytes on target mapped with host address " DPxMOD " - is%s new"
          "\n", sizeof(void *), DPxPTR(HstPtrBase),
          (Pointer_IsNew ? "" : " not"));
      Pointer_HstPtrBegin = HstPtrBase;
      // modify current entry.
      HstPtrBase = *(void **) HstPtrBase;
      UpdateRef = true; // subsequently update ref count of pointee
    }

    //void *TgtPtrBegin = AT(HstPtrBegin);
    intptr_t ret = (intptr_t) Device.getOrAllocTgtPtr(HstPtrBegin, HstPtrBase,
    //void *TgtPtrBegin = Device.getOrAllocTgtPtr(HstPtrBegin, HstPtrBase,
        data_size, IsNew, IsImplicit, UpdateRef);
    //if (!TgtPtrBegin && data_size) {
    if (!ret && data_size) {
      // If data_size==0, then the argument could be a zero-length pointer to
      // NULL, so getOrAlloc() returning NULL is not an error.
      DP2("Call to getOrAllocTgtPtr failed (device failure or "
          "illegal mapping).\n");
      return OFFLOAD_FAIL;
    }
    DP2("There are %" PRId64 " bytes on target mapped with host address " DPxMOD
        " - is%s new\n", data_size, DPxPTR(HstPtrBegin),
        (IsNew ? "" : " not"));

    if (data_type & OMP_TGT_MAPTYPE_RETURN_PARAM) {
      assert( 0 && "OMP_TGT_MAPTYPE_RETURN_PARAM is not supported yet");
      /*
      uintptr_t Delta = (uintptr_t)HstPtrBegin - (uintptr_t)HstPtrBase;
      void *TgtPtrBase = (void *)((uintptr_t)TgtPtrBegin - Delta);
      DP("Returning device pointer " DPxMOD "\n", DPxPTR(TgtPtrBase));
      args_base[i] = TgtPtrBase;
      */
    }

    if (data_type & OMP_TGT_MAPTYPE_TO) {
      bool copy = false;
      if (IsNew || (data_type & OMP_TGT_MAPTYPE_ALWAYS)) {
        copy = true;
      } else if (data_type & OMP_TGT_MAPTYPE_MEMBER_OF) {
        // Copy data only if the "parent" struct has RefCount==1.
        int32_t parent_idx = member_of(data_type);
        long parent_rc = Device.getMapEntryRefCnt(args[parent_idx]);
        assert(parent_rc > 0 && "parent struct not found");
        if (parent_rc == 1) {
          copy = true;
        }
      }

      if (copy) {
        DP("Suspend moving %" PRId64 " bytes (hst:" DPxMOD ")\n",
            data_size, DPxPTR(HstPtrBegin));
        //int rt = Device.data_submit(TgtPtrBegin, HstPtrBegin, data_size);
        int rt = Device.bulk_data_submit(HstPtrBegin, data_size);
        if (rt != OFFLOAD_SUCCESS) {
          DP("Copying data to device failed.\n");
          return OFFLOAD_FAIL;
        }
      }
    }
    if (Device.IsATEnabled) {
      if (data_type & OMP_TGT_MAPTYPE_NESTED) {
        goto DCGEN_REPEAT;
      }
      continue;
    }

    if (data_type & OMP_TGT_MAPTYPE_PTR_AND_OBJ) {
      PERF_WRAP(Perf.UpdatePtr.start())
      uint64_t Delta = (uint64_t)HstPtrBegin - (uint64_t)HstPtrBase;
      int rt = Device.suspend_update(Pointer_HstPtrBegin, HstPtrBegin, Delta, HstPtrBase);
      if (rt != OFFLOAD_SUCCESS) {
        DP2("Copying data to device failed.\n");
        return OFFLOAD_FAIL;
      }
      PERF_WRAP(Perf.UpdatePtr.end())
    }

    if (data_type & OMP_TGT_MAPTYPE_NESTED) {
      goto DCGEN_REPEAT;
    }
  }

  return OFFLOAD_SUCCESS;
}

/// Internal function to undo the mapping and retrieve the data from the device.
int target_data_end(DeviceTy &Device, int32_t arg_num, void **args_base,
    void **args, int64_t *arg_sizes, int64_t *arg_types) {
  PERF_WRAP(Perf.RTDataEnd.start();)
  // process each input.
  RttTy Rtt;
  if (arg_num && arg_types[0] & OMP_TGT_MAPTYPE_HAS_NESTED) {
    Rtt.initIsFrom(args, arg_types, arg_num);
  }

  for (int32_t i = arg_num - 1; i >= 0; --i) {
    // Ignore private variables and arrays - there is no mapping for them.
    // Also, ignore the use_device_ptr directive, it has no effect here.
    if ((arg_types[i] & OMP_TGT_MAPTYPE_LITERAL) ||
        (arg_types[i] & OMP_TGT_MAPTYPE_PRIVATE))
      continue;

    int ret;
    void *HstPtrBegin = args[i];
    void *HstPtrBase = args_base[i];
    int64_t data_size = arg_sizes[i];
    int64_t data_type = arg_types[i];

    if (data_type & OMP_TGT_MAPTYPE_NESTED) {
      // init Rtt with stack addr
      ret = Rtt.newRttObject(&HstPtrBegin, &HstPtrBase,
          &data_size, &data_type);
      if (ret != RTT_SUCCESS) {
        DP2("RTT init failed");
        continue;
      }
DCGEN_REPEAT:
      // Regen Begin, Base, size maptype
      ret = Rtt.computeRegion();
      if (ret == RTT_END) {
        continue;
      }
      if (ret != RTT_SUCCESS) {
        return OFFLOAD_FAIL;
      }
    }
    DP2("Base: %p Ptr: %p size: %" PRId64 " type: 0x%lx\n", HstPtrBase, HstPtrBegin, data_size, data_type);
    DP("Base: %p Ptr: %p size: %" PRId64 " type: 0x%lx\n", HstPtrBase, HstPtrBegin, data_size, data_type);
    // Adjust for proper alignment if this is a combined entry (for structs).
    // Look at the next argument - if that is MEMBER_OF this one, then this one
    // is a combined entry.
    int64_t padding = 0;
    const int next_i = i+1;
    if (member_of(arg_types[i]) < 0 && next_i < arg_num &&
        member_of(arg_types[next_i]) == i) {
      padding = (int64_t)HstPtrBegin % alignment;
      if (padding) {
        DP("Using a padding of %" PRId64 " bytes for begin address " DPxMOD
            "\n", padding, DPxPTR(HstPtrBegin));
        HstPtrBegin = (char *) HstPtrBegin - padding;
        data_size += padding;
      }
    }

    bool IsLast;
    bool UpdateRef = !(data_type & OMP_TGT_MAPTYPE_MEMBER_OF) ||
        (data_type & OMP_TGT_MAPTYPE_PTR_AND_OBJ);
    bool ForceDelete = data_type & OMP_TGT_MAPTYPE_DELETE;

    // If PTR_AND_OBJ, HstPtrBegin is address of pointee
    void *TgtPtrBegin = Device.getTgtPtrBegin(HstPtrBegin, data_size, IsLast,
        UpdateRef);
    if (Device.IsBulkEnabled) {
      TgtPtrBegin = Device.bulkGetTgtPtrBegin(HstPtrBegin, data_size);
    }
    DP("There are %" PRId64 " bytes allocated at target address " DPxMOD
        " - is%s last\n", data_size, DPxPTR(TgtPtrBegin),
        (IsLast ? "" : " not"));


    bool DelEntry = IsLast || ForceDelete;

    if ((data_type & OMP_TGT_MAPTYPE_MEMBER_OF) &&
        !(data_type & OMP_TGT_MAPTYPE_PTR_AND_OBJ)) {
      DelEntry = false; // protect parent struct from being deallocated
    }

    if ((data_type & OMP_TGT_MAPTYPE_FROM) || DelEntry) {
      // Move data back to the host
      if (data_type & OMP_TGT_MAPTYPE_FROM) {
        bool Always = data_type & OMP_TGT_MAPTYPE_ALWAYS;
        //Always = true;
        bool CopyMember = false;
        if ((data_type & OMP_TGT_MAPTYPE_MEMBER_OF) &&
            !(data_type & OMP_TGT_MAPTYPE_PTR_AND_OBJ)) {
          // Copy data only if the "parent" struct has RefCount==1.
          int32_t parent_idx = member_of(data_type);
          long parent_rc = Device.getMapEntryRefCnt(args[parent_idx]);
          assert(parent_rc > 0 && "parent struct not found");
          if (parent_rc == 1) {
            CopyMember = true;
          }
        }

        if (DelEntry || Always || CopyMember) {
          DP("Moving %" PRId64 " bytes (tgt:" DPxMOD ") -> (hst:" DPxMOD ")\n",
              data_size, DPxPTR(TgtPtrBegin), DPxPTR(HstPtrBegin));
          int rt = Device.data_retrieve(HstPtrBegin, TgtPtrBegin, data_size);
          if (rt != OFFLOAD_SUCCESS) {
            DP("Copying data from device failed.\n");
            return OFFLOAD_FAIL;
          }
          if (Device.IsDCEnabled && data_size == sizeof(void*)) {
            // FIXME what if this is not a address???
            // TODO Add flag
            mm_context_t *context = NULL;
            void *ptr = *(void**)HstPtrBegin;
            if (Device.ATMode & OMP_OFFMODE_AT_TABLE) {
              context = get_mm_context(ptr);
            } else if (_MYMALLOC_ISMYSPACE(ptr)) {
                context = get_mm_context(ptr);
            }
            if (context) {
              DP2("Transfer back" DPxMOD " with  dc object #%d\n",
                  DPxPTR(HstPtrBegin), context->id);
              rt = context->data_retrieve();
              if (rt != OFFLOAD_SUCCESS) {
                return OFFLOAD_FAIL;
              }
            }
          }
        }
      }

      // If we copied back to the host a struct/array containing pointers, we
      // need to restore the original host pointer values from their shadow
      // copies. If the struct is going to be deallocated, remove any remaining
      // shadow pointer entries for this struct.
      uintptr_t lb = (uintptr_t) HstPtrBegin;
      uintptr_t ub = (uintptr_t) HstPtrBegin + data_size;

      if (Device.IsATEnabled) {
        goto DEL;
      }

      Device.ShadowMtx.lock();
      for (auto it = Device.ShadowPtrMap.upper_bound((void*)ub);
      //for (ShadowPtrListTy::iterator it = Device.ShadowPtrMap.begin();
           it != Device.ShadowPtrMap.end();) {
        void **ShadowHstPtrAddr = (void**) it->first;

        // An STL map is sorted on its keys; use this property
        // to quickly determine when to break out of the loop.
        if ((uintptr_t) ShadowHstPtrAddr < lb) {
          break;
        }
        /*if ((uintptr_t) ShadowHstPtrAddr >= ub)
          break;*/

        // If we copied the struct to the host, we need to restore the pointer.
        if (data_type & OMP_TGT_MAPTYPE_FROM) {
          DP("Restoring original host pointer value " DPxMOD " for host "
              "pointer " DPxMOD "\n", DPxPTR(it->second.HstPtrVal),
              DPxPTR(ShadowHstPtrAddr));
          *ShadowHstPtrAddr = it->second.HstPtrVal;
        }
        // If the struct is to be deallocated, remove the shadow entry.
        if (DelEntry) {
          DP("Removing shadow pointer " DPxMOD "\n", DPxPTR(ShadowHstPtrAddr));
          it = Device.ShadowPtrMap.erase(it);
        } else {
          ++it;
        }
      }
      Device.ShadowMtx.unlock();

DEL:
      // Deallocate map
      if (DelEntry) {
        int rt = Device.deallocTgtPtr(HstPtrBegin, data_size, ForceDelete);
        if (rt != OFFLOAD_SUCCESS) {
          DP("Deallocating data from device failed.\n");
          return OFFLOAD_FAIL;
        }
      }
    }
    if (data_type & OMP_TGT_MAPTYPE_NESTED) {
      goto DCGEN_REPEAT;
    }
  }
  PERF_WRAP(Perf.RTDataEnd.end();)
  return OFFLOAD_SUCCESS;
}

/// Internal function to pass data to/from the target.
int target_data_update(DeviceTy &Device, int32_t arg_num,
    void **args_base, void **args, int64_t *arg_sizes, int64_t *arg_types) {
  if (Device.IsBulkEnabled) {
    assert(0 && "target_data_update should not be used");
  }
  // process each input.
  for (int32_t i = 0; i < arg_num; ++i) {
    if ((arg_types[i] & OMP_TGT_MAPTYPE_LITERAL) ||
        (arg_types[i] & OMP_TGT_MAPTYPE_PRIVATE))
      continue;

    DP("Base: %p Ptr: %p size: %" PRId64 " type: 0x%lx\n", args_base[i],
        args[i], arg_sizes[i], arg_types[i]);
    void *HstPtrBegin = args[i];
    int64_t MapSize = arg_sizes[i];
    bool IsLast;
    void *TgtPtrBegin = Device.getTgtPtrBegin(HstPtrBegin, MapSize, IsLast,
        false);
    if (!TgtPtrBegin) {
      DP("hst data:" DPxMOD " not found, becomes a noop\n", DPxPTR(HstPtrBegin));
      continue;
    }

    if (arg_types[i] & OMP_TGT_MAPTYPE_FROM) {
      DP("Moving %" PRId64 " bytes (tgt:" DPxMOD ") -> (hst:" DPxMOD ")\n",
          arg_sizes[i], DPxPTR(TgtPtrBegin), DPxPTR(HstPtrBegin));
      int rt = Device.data_retrieve(HstPtrBegin, TgtPtrBegin, MapSize);
      if (rt != OFFLOAD_SUCCESS) {
        DP("Copying data from device failed.\n");
        return OFFLOAD_FAIL;
      }

      uintptr_t lb = (uintptr_t) HstPtrBegin;
      uintptr_t ub = (uintptr_t) HstPtrBegin + MapSize;
      Device.ShadowMtx.lock();
      for (auto it = Device.ShadowPtrMap.upper_bound((void*)ub);
      //for (ShadowPtrListTy::iterator it = Device.ShadowPtrMap.begin();
          it != Device.ShadowPtrMap.end(); ++it) {
        void **ShadowHstPtrAddr = (void**) it->first;
        if ((uintptr_t) ShadowHstPtrAddr < lb) {
          break;
        }
        /*  continue;
        if ((uintptr_t) ShadowHstPtrAddr >= ub)
          break;
          */
        DP("Restoring original host pointer value " DPxMOD " for host pointer "
            DPxMOD "\n", DPxPTR(it->second.HstPtrVal),
            DPxPTR(ShadowHstPtrAddr));
        *ShadowHstPtrAddr = it->second.HstPtrVal;
      }
      Device.ShadowMtx.unlock();
    }

    if (arg_types[i] & OMP_TGT_MAPTYPE_TO) {
      DP("Moving %" PRId64 " bytes (hst:" DPxMOD ") -> (tgt:" DPxMOD ")\n",
          arg_sizes[i], DPxPTR(HstPtrBegin), DPxPTR(TgtPtrBegin));
      int rt = Device.data_submit(TgtPtrBegin, HstPtrBegin, MapSize);
      if (rt != OFFLOAD_SUCCESS) {
        DP("Copying data to device failed.\n");
        return OFFLOAD_FAIL;
      }
      uintptr_t lb = (uintptr_t) HstPtrBegin;
      uintptr_t ub = (uintptr_t) HstPtrBegin + MapSize;
      Device.ShadowMtx.lock();
      for (auto it = Device.ShadowPtrMap.upper_bound((void*)ub);
      //for (ShadowPtrListTy::iterator it = Device.ShadowPtrMap.begin();
          it != Device.ShadowPtrMap.end(); ++it) {
        void **ShadowHstPtrAddr = (void**) it->first;

        if ((uintptr_t) ShadowHstPtrAddr < lb) {
          break;
        }
        /*if ((uintptr_t) ShadowHstPtrAddr >= ub)
          break;*/
        DP("Restoring original target pointer value " DPxMOD " for target "
            "pointer " DPxMOD "\n", DPxPTR(it->second.TgtPtrVal),
            DPxPTR(it->second.TgtPtrAddr));
        rt = Device.data_submit(it->second.TgtPtrAddr,
            &it->second.TgtPtrVal, sizeof(void *));
        if (rt != OFFLOAD_SUCCESS) {
          DP("Copying data to device failed.\n");
          Device.ShadowMtx.unlock();
          return OFFLOAD_FAIL;
        }
      }
      Device.ShadowMtx.unlock();
    }
  }
  return OFFLOAD_SUCCESS;
}

static const unsigned LambdaMapping = OMP_TGT_MAPTYPE_PTR_AND_OBJ |
                                      OMP_TGT_MAPTYPE_LITERAL |
                                      OMP_TGT_MAPTYPE_IMPLICIT;
static bool isLambdaMapping(int64_t Mapping) {
  return (Mapping & LambdaMapping) == LambdaMapping;
}

/// performs the same actions as data_begin in case arg_num is
/// non-zero and initiates run of the offloaded region on the target platform;
/// if arg_num is non-zero after the region execution is done it also
/// performs the same action as data_update and data_end above. This function
/// returns 0 if it was able to transfer the execution to a target and an
/// integer different from zero otherwise.
int target(int64_t device_id, void *host_ptr, int32_t arg_num,
    void **args_base, void **args, int64_t *arg_sizes, int64_t *arg_types,
    int32_t team_num, int32_t thread_limit, int IsTeamConstruct) {
  DeviceTy &Device = Devices[device_id];

  // Find the table information in the map or look it up in the translation
  // tables.
  TableMap *TM = 0;
  TblMapMtx.lock();
  HostPtrToTableMapTy::iterator TableMapIt = HostPtrToTableMap.find(host_ptr);
  if (TableMapIt == HostPtrToTableMap.end()) {
    // We don't have a map. So search all the registered libraries.
    TrlTblMtx.lock();
    for (HostEntriesBeginToTransTableTy::iterator
             ii = HostEntriesBeginToTransTable.begin(),
             ie = HostEntriesBeginToTransTable.end();
         !TM && ii != ie; ++ii) {
      // get the translation table (which contains all the good info).
      TranslationTable *TransTable = &ii->second;
      // iterate over all the host table entries to see if we can locate the
      // host_ptr.
      __tgt_offload_entry *begin = TransTable->HostTable.EntriesBegin;
      __tgt_offload_entry *end = TransTable->HostTable.EntriesEnd;
      __tgt_offload_entry *cur = begin;
      for (uint32_t i = 0; cur < end; ++cur, ++i) {
        if (cur->addr != host_ptr)
          continue;
        // we got a match, now fill the HostPtrToTableMap so that we
        // may avoid this search next time.
        TM = &HostPtrToTableMap[host_ptr];
        TM->Table = TransTable;
        TM->Index = i;
        break;
      }
    }
    TrlTblMtx.unlock();
  } else {
    TM = &TableMapIt->second;
  }
  TblMapMtx.unlock();

  // No map for this host pointer found!
  if (!TM) {
    DP("Host ptr " DPxMOD " does not have a matching target pointer.\n",
       DPxPTR(host_ptr));
    return OFFLOAD_FAIL;
  }

  // get target table.
  TrlTblMtx.lock();
  assert(TM->Table->TargetsTable.size() > (size_t)device_id &&
         "Not expecting a device ID outside the table's bounds!");
  __tgt_target_table *TargetTable = TM->Table->TargetsTable[device_id];
  TrlTblMtx.unlock();
  assert(TargetTable && "Global data has not been mapped\n");

  // Move data to device.
  int rc = target_data_begin(Device, arg_num, args_base, args, arg_sizes,
      arg_types);
  if (rc != OFFLOAD_SUCCESS) {
    DP("Call to target_data_begin failed, abort target.\n");
    return OFFLOAD_FAIL;
  }

  if (Device.IsBulkEnabled) {
    if (!Device.IsNoBulkEnabled) {
      Device.bulk_transfer();
    }
    Device.dump_segmentlist();
    //Device.dump_map();

    if (!Device.IsATEnabled) {
      Device.update_suspend_list(); // shadowlist
    }
  }

  if (Device.IsATEnabled) {
    // convert region list to table
    Device.table_transfer();
    // Add tgt table
    AT.addTable(Device.SegmentList.TgtMemPtr);
    AT.addTableSize(Device.SegmentList.TgtList.size());

    int fake_literal = 878787;
    int fake_table_size = 13;
    int fake_table_byte = fake_table_size * sizeof(struct SegmentTy);
    //DP2("Expected size: %d\n", fake_table_size);
    AT.addFakeByte(fake_table_byte);
    AT.addFakeSize(fake_literal);
  }

  std::vector<void *> tgt_args;
  std::vector<ptrdiff_t> tgt_offsets;

  // List of (first-)private arrays allocated for this target region
  std::vector<void *> fpArrays;
  std::vector<int> tgtArgsPositions(arg_num, -1);
  std::vector<mm_context_t *> contexts_for_ATTable;

  for (int32_t i = 0; i < arg_num; ++i) {
    if (!(arg_types[i] & OMP_TGT_MAPTYPE_TARGET_PARAM)) {
      // This is not a target parameter, do not push it into tgt_args.
      // Check for lambda mapping.
      if (isLambdaMapping(arg_types[i])) {
        assert((arg_types[i] & OMP_TGT_MAPTYPE_MEMBER_OF) &&
               "PTR_AND_OBJ must be also MEMBER_OF.");
        unsigned idx = member_of(arg_types[i]);
        int tgtIdx = tgtArgsPositions[idx];
        assert(tgtIdx != -1 && "Base address must be translated already.");
        // The parent lambda must be processed already and it must be the last
        // in tgt_args and tgt_offsets arrays.
        void *HstPtrVal = args[i];
        void *HstPtrBegin = args_base[i];
        void *HstPtrBase = args[idx];
        bool IsLast; // unused.
        void *TgtPtrBase =
            (void *)((intptr_t)tgt_args[tgtIdx] + tgt_offsets[tgtIdx]);
        DP("Parent lambda base " DPxMOD "\n", DPxPTR(TgtPtrBase));
        uint64_t Delta = (uint64_t)HstPtrBegin - (uint64_t)HstPtrBase;
        void *TgtPtrBegin = (void *)((uintptr_t)TgtPtrBase + Delta);
        void *Pointer_TgtPtrBegin = Device.getTgtPtrBegin(HstPtrVal, arg_sizes[i], IsLast, false);
        if (Device.IsBulkEnabled) {
          Pointer_TgtPtrBegin = Device.bulkGetTgtPtrBegin(HstPtrVal, arg_sizes[i]);
        }
        if (!Pointer_TgtPtrBegin) {
          DP("No lambda captured variable mapped (" DPxMOD ") - ignored\n",
             DPxPTR(HstPtrVal));
          continue;
        }
        DP("Update lambda reference (" DPxMOD ") -> [" DPxMOD "]\n",
           DPxPTR(Pointer_TgtPtrBegin), DPxPTR(TgtPtrBegin));
        int rt = Device.data_submit(TgtPtrBegin, &Pointer_TgtPtrBegin,
                                    sizeof(void *));
        if (rt != OFFLOAD_SUCCESS) {
          DP("Copying data to device failed.\n");
          return OFFLOAD_FAIL;
        }
      }
      continue;
    }
    void *HstPtrBegin = args[i];
    void *HstPtrBase = args_base[i];
    void *TgtPtrBegin;
    ptrdiff_t TgtBaseOffset;
    bool IsLast; // unused.
    if (arg_types[i] & OMP_TGT_MAPTYPE_LITERAL) {
      DP("Forwarding first-private value " DPxMOD " to the target construct\n",
          DPxPTR(HstPtrBase));
      TgtPtrBegin = HstPtrBase;
      //pschen
      if (Device.IsATEnabled) {
        TgtPtrBegin = AT.passLiteral(HstPtrBase, arg_sizes[i]);
      }
      TgtBaseOffset = 0;

    } else if (arg_types[i] & OMP_TGT_MAPTYPE_PRIVATE) {
      // Allocate memory for (first-)private array
      TgtPtrBegin = Device.RTL->data_alloc(Device.RTLDeviceID,
          arg_sizes[i], HstPtrBegin);
      if (!TgtPtrBegin) {
        DP ("Data allocation for %sprivate array " DPxMOD " failed, "
            "abort target.\n",
            (arg_types[i] & OMP_TGT_MAPTYPE_TO ? "first-" : ""),
            DPxPTR(HstPtrBegin));
        return OFFLOAD_FAIL;
      }
      fpArrays.push_back(TgtPtrBegin);
      TgtBaseOffset = (intptr_t)HstPtrBase - (intptr_t)HstPtrBegin;
#ifdef OMPTARGET_DEBUG
      void *TgtPtrBase = (void *)((intptr_t)TgtPtrBegin + TgtBaseOffset);
      DP("Allocated %" PRId64 " bytes of target memory at " DPxMOD " for "
          "%sprivate array " DPxMOD " - pushing target argument " DPxMOD "\n",
          arg_sizes[i], DPxPTR(TgtPtrBegin),
          (arg_types[i] & OMP_TGT_MAPTYPE_TO ? "first-" : ""),
          DPxPTR(HstPtrBegin), DPxPTR(TgtPtrBase));
#endif
      // If first-private, copy data from host
      if (arg_types[i] & OMP_TGT_MAPTYPE_TO) {
        int rt = Device.data_submit(TgtPtrBegin, HstPtrBegin, arg_sizes[i]);
        if (rt != OFFLOAD_SUCCESS) {
          DP ("Copying data to device failed, failed.\n");
          return OFFLOAD_FAIL;
        }
      }
    } else if (arg_types[i] & OMP_TGT_MAPTYPE_PTR_AND_OBJ) {
      TgtPtrBegin = Device.getTgtPtrBegin(HstPtrBase, sizeof(void *), IsLast,
          false);
      if (Device.IsBulkEnabled) {
        DP2("IsBulkEnabled\n");
        TgtPtrBegin = Device.bulkGetTgtPtrBegin(HstPtrBegin, sizeof(void*));
        DP2("IsBulkEnabled end\n");
      }
      TgtBaseOffset = 0; // no offset for ptrs.
      DP("Obtained target argument " DPxMOD " from host pointer " DPxMOD " to "
         "object " DPxMOD "\n", DPxPTR(TgtPtrBegin), DPxPTR(HstPtrBase),
         DPxPTR(HstPtrBase));
    } else {
      TgtPtrBegin = Device.getTgtPtrBegin(HstPtrBegin, arg_sizes[i], IsLast,
          false);
      if (_MYMALLOC_ISMYSPACE(HstPtrBegin)) {
        if (Device.ATMode & OMP_OFFMODE_AT_MASK) {
          TgtPtrBegin = (void*)_MYMALLOC_H2D(HstPtrBegin);
          DP2("omp target launching with myspace arg: %p->%p\n",
              HstPtrBegin, TgtPtrBegin);
        } else if (Device.ATMode & OMP_OFFMODE_AT_OFFSET) {
          TgtPtrBegin = (void*)((intptr_t)HstPtrBegin +
              get_offset(get_mm_context(HstPtrBegin)));
          DP2("Offset: arg of kernel: %p->%p\n", HstPtrBegin, TgtPtrBegin);
        }
      }
        if (TgtPtrBegin == NULL && (Device.ATMode & OMP_OFFMODE_AT_TABLE)) {
          mm_context_t *context;
          heap_t *heap = get_heap(HstPtrBegin, &context);
          if (heap && heap->tbegin) {
            TgtPtrBegin = (void*)((uintptr_t)heap->tbegin -
                (uintptr_t)heap->begin + (uintptr_t)HstPtrBegin);
            // Add context to table
            contexts_for_ATTable.push_back(context);
          }
          DP2("OMP_TABLE need to translate NULL args %p\n", TgtPtrBegin);
        }
      if (Device.IsBulkEnabled) {
        DP2("IsBulkEnabled 2\n");
        TgtPtrBegin = Device.bulkGetTgtPtrBegin(HstPtrBegin, arg_sizes[i]);
        DP2("IsBulkEnabled 2 end\n");
      }
      if (Device.IsATEnabled) {
        // FIXME skip one search
        // TgtPtrBegin does not matters
        TgtPtrBegin = AT.passArg(TgtPtrBegin, arg_sizes[i]);
        // For Fake AT

      }
      TgtBaseOffset = (intptr_t)HstPtrBase - (intptr_t)HstPtrBegin;

#ifdef OMPTARGET_DEBUG
      void *TgtPtrBase = (void *)((intptr_t)TgtPtrBegin + TgtBaseOffset);
      DP("Obtained target argument " DPxMOD " from host pointer " DPxMOD "\n",
          DPxPTR(TgtPtrBase), DPxPTR(HstPtrBegin));
#endif
    }
    tgtArgsPositions[i] = tgt_args.size();
    tgt_args.push_back(TgtPtrBegin);
    tgt_offsets.push_back(TgtBaseOffset);
  }
  // Insert  table
  /*
  if (Device.IsATEnabled) {
    tgt_args.push_back(Device.SegmentList.TgtMemPtr);
    tgt_offsets.push_back(0);
  }*/
  if (Device.ATMode & OMP_OFFMODE_AT_TABLE) {
    printf("Constructing  tableATTable\n");
    SegmentListTy &SegmentList = Device.SegmentList;
    SegmentList.clear();
    for (auto e : contexts_for_ATTable) {
      printf("construct with context %d\n", e->id);
      heap_t *first_heap = e->heap_list;
      heap_t *curr_heap = first_heap;
      do {
        if (!curr_heap->tbegin) {
          curr_heap = curr_heap->next;
          continue;
        }
        SegmentTy seg;
        seg.HstPtrBegin = (uintptr_t)curr_heap->begin;
        seg.HstPtrEnd =  (uintptr_t)curr_heap->end;
        seg.TgtPtrBegin = (uintptr_t)curr_heap->tbegin;
        SegmentList.emplace((uintptr_t)curr_heap->begin,
            seg);
        curr_heap = curr_heap->next;
        printf("push hst: 0x%p 0x%p\n", (void*)seg.HstPtrBegin, (void*)seg.TgtPtrBegin);
      } while(first_heap != curr_heap);
    }
    SegmentTy seg;
    seg.HstPtrBegin = (uintptr_t)SegmentList.size();
    SegmentList.TgtList.emplace_back(seg);
    for (auto &entry : SegmentList) {
      auto &e = entry.second;
      //printf("%lu %lu %lu\n", e.HstPtrBegin, e.HstPtrEnd, e.TgtPtrBegin);
      SegmentList.TgtList.push_back(e);
    }
    printf("AT Table size: %lu\n", SegmentList.size());
    SegmentList.TgtMemPtr = Device.RTL->data_alloc(Device.RTLDeviceID,
        SegmentList.size()*sizeof(SegmentTy), NULL);
    int rt = Device.data_submit(SegmentList.TgtMemPtr,
        &SegmentList.TgtList[0],
        SegmentList.size() * sizeof(SegmentTy));
    if (rt != OFFLOAD_SUCCESS) {
      DP("Transfer AT table failed\n");
      return OFFLOAD_FAIL;
    }
    // insert arg
    tgt_args.push_back(SegmentList.TgtMemPtr);
    tgt_offsets.push_back(0);
  }

  // Insert Mask
  if (Device.ATMode & OMP_OFFMODE_AT_MASK) {
    DP2("Append h2d mask %p to kernel\n", (void*&)_omp_h2dmask);
    tgt_args.push_back((void*)_omp_h2dmask);
    tgt_offsets.push_back(0);
  }
  if (Device.ATMode & OMP_OFFMODE_AT_OFFSET) {
    void *t_offset_list = NULL;
    if (!t_offset_list) {
      if (getenv("OMP_OFFSET_CM")) {
        int64_t size;
        t_offset_list = Device.RTL->get_readonly_mem(&size);
      } else {
        t_offset_list = Device.RTL->data_alloc(Device.RTLDeviceID,
            32*sizeof(intptr_t), NULL);
        Device.SegmentList.TgtMemPtr = t_offset_list;
      }
    }
    intptr_t offset_list[32];
    int size;
    // Here is hardcode
    // mask
    offset_list[0] = 0x000000f000000000L;
    // shift
    offset_list[1] = 9 * 4;
    get_offset_table(&size, offset_list + 2);
    // size
    if (size < 1) {
      exit(39);
    }
    // data_submit
    int rt = Device.data_submit(t_offset_list, offset_list, sizeof(intptr_t)*(size+4));
    if (rt != OFFLOAD_SUCCESS) {
      DP("Map offset list failed\n");
      return OFFLOAD_FAIL;
    }
    tgt_args.push_back(t_offset_list);
    tgt_offsets.push_back(0);
    DP2("Append offset list %p to kernel\n", offset_list);
  }

  assert(tgt_args.size() == tgt_offsets.size() &&
      "Size mismatch in arguments and offsets");

  // Pop loop trip count
  uint64_t ltc = Device.loopTripCnt;
  Device.loopTripCnt = 0;

  // Launch device execution
  DP("Launching target execution %s with pointer " DPxMOD " (index=%d).\n",
      TargetTable->EntriesBegin[TM->Index].name,
      DPxPTR(TargetTable->EntriesBegin[TM->Index].addr), TM->Index);
  DP2("Launch kernel\n");
  // print args
  for (auto arg : tgt_args) {
    DP2("     Arg: " DPxMOD "\n", DPxPTR(arg));
  }
  if (IsTeamConstruct) {
    rc = Device.run_team_region(TargetTable->EntriesBegin[TM->Index].addr,
        &tgt_args[0], &tgt_offsets[0], tgt_args.size(), team_num,
        thread_limit, ltc);
  } else {
    rc = Device.run_region(TargetTable->EntriesBegin[TM->Index].addr,
        &tgt_args[0], &tgt_offsets[0], tgt_args.size());
  }
  if (rc == OFFLOAD_FAIL) {
    DP ("Executing target region abort target.\n");
    return OFFLOAD_FAIL;
  }
  PERF_WRAP(Perf.Parallelism.add(rc);)

  // Deallocate (first-)private arrays
  for (auto it : fpArrays) {
    int rt = Device.RTL->data_delete(Device.RTLDeviceID, it);
    if (rt != OFFLOAD_SUCCESS) {
      DP("Deallocation of (first-)private arrays failed.\n");
      return OFFLOAD_FAIL;
    }
  }
  // Deallocate table for AT
  if (Device.ATMode & (OMP_OFFMODE_AT_TABLE | OMP_OFFMODE_AT_OFFSET)) {
    if (Device.SegmentList.TgtMemPtr) {
      int rt = Device.RTL->data_delete(Device.RTLDeviceID,
          Device.SegmentList.TgtMemPtr);
      if (rt != OFFLOAD_SUCCESS) {
        DP("Deallocation AT table failed.\n");
        return OFFLOAD_FAIL;
      }
      Device.SegmentList.TgtMemPtr = NULL;
    }
  }

  // Move data from device.
  int rt = target_data_end(Device, arg_num, args_base, args, arg_sizes,
      arg_types);
  // FIXME
  // Back No bulk
  if (0 && Device.IsBulkEnabled) {
    //rt = Device.bulk_transfer();
  }
  if (rt != OFFLOAD_SUCCESS) {
    DP("Call to target_data_end failed, abort targe.\n");
    return OFFLOAD_FAIL;
  }

  return OFFLOAD_SUCCESS;
}
