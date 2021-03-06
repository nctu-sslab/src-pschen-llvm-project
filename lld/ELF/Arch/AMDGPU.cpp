//===- AMDGPU.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class AMDGPU final : public TargetInfo {
public:
  AMDGPU();
  uint32_t calcEFlags() const override;
  void relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  RelExpr getRelExpr(RelType Type, const Symbol &S,
                     const uint8_t *Loc) const override;
  RelType getDynRel(RelType Type) const override;
};
} // namespace

AMDGPU::AMDGPU() {
  RelativeRel = R_AMDGPU_RELATIVE64;
  GotRel = R_AMDGPU_ABS64;
  NoneRel = R_AMDGPU_NONE;
  SymbolicRel = R_AMDGPU_ABS64;
}

static uint32_t getEFlags(InputFile *File) {
  return cast<ObjFile<ELF64LE>>(File)->getObj().getHeader()->e_flags;
}

uint32_t AMDGPU::calcEFlags() const {
  assert(!ObjectFiles.empty());
  uint32_t Ret = getEFlags(ObjectFiles[0]);

  // Verify that all input files have the same e_flags.
  for (InputFile *F : makeArrayRef(ObjectFiles).slice(1)) {
    if (Ret == getEFlags(F))
      continue;
    error("incompatible e_flags: " + toString(F));
    return 0;
  }
  return Ret;
}

void AMDGPU::relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const {
  switch (Type) {
  case R_AMDGPU_ABS32:
  case R_AMDGPU_GOTPCREL:
  case R_AMDGPU_GOTPCREL32_LO:
  case R_AMDGPU_REL32:
  case R_AMDGPU_REL32_LO:
    write32le(Loc, Val);
    break;
  case R_AMDGPU_ABS64:
  case R_AMDGPU_REL64:
    write64le(Loc, Val);
    break;
  case R_AMDGPU_GOTPCREL32_HI:
  case R_AMDGPU_REL32_HI:
    write32le(Loc, Val >> 32);
    break;
  default:
    llvm_unreachable("unknown relocation");
  }
}

RelExpr AMDGPU::getRelExpr(RelType Type, const Symbol &S,
                           const uint8_t *Loc) const {
  switch (Type) {
  case R_AMDGPU_ABS32:
  case R_AMDGPU_ABS64:
    return R_ABS;
  case R_AMDGPU_REL32:
  case R_AMDGPU_REL32_LO:
  case R_AMDGPU_REL32_HI:
  case R_AMDGPU_REL64:
    return R_PC;
  case R_AMDGPU_GOTPCREL:
  case R_AMDGPU_GOTPCREL32_LO:
  case R_AMDGPU_GOTPCREL32_HI:
    return R_GOT_PC;
  default:
    error(getErrorLocation(Loc) + "unknown relocation (" + Twine(Type) +
          ") against symbol " + toString(S));
    return R_NONE;
  }
}

RelType AMDGPU::getDynRel(RelType Type) const {
  if (Type == R_AMDGPU_ABS64)
    return Type;
  return R_AMDGPU_NONE;
}

TargetInfo *elf::getAMDGPUTargetInfo() {
  static AMDGPU Target;
  return &Target;
}
