// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Refusal-only owner-root stub for the haydn UFS bring-up link.
//
// ShengUfs is compiled with NATIVE_VERIFIED_BLOCK=1, so its verified-block paths
// reference the owner-root symbols that only libVerifiedRootProbe.a defines. That
// archive can only be produced from a prior audit of the real OS image
// (build/verified-root-os/verification.json over 044-76034-038.dmg, exact
// image sha256 e5634d04..., 1483785 blocks). This board has neither the audited
// image nor that report, and the audit cannot be synthesised.
//
// This stub satisfies the link with ZERO policies and a crypto bridge that always
// fails, so nothing can ever be authenticated through it: configureVerification()
// stops at "policy count invalid" before it reads a single manifest, and every
// read stays refused. It exists only so the hardware probe boot-args
// (native-ufs-probe=2 / 10 / 11) can run and report what the SM8350 UFS
// controller actually does.
//
// Do not extend this file into a trust root, and do not let it return success:
// a real owner-root has to come from the audited image, never from a placeholder.

#include <stddef.h>
#include <stdint.h>

#include "VerifiedBlockPolicy.h"
#include "verified_root.h"

extern "C" {

// Zero policies: the capacity-matching selector refuses before any manifest read.
const NativeVerifiedBlockPolicy verified_root_policies[1] = {{nullptr, 0}};
const uint64_t verified_root_policy_count = 0;

// Full-size so the (unreachable) memcpy stays in bounds; never the real anchor.
extern const uint8_t verified_root_key[VR_KEY_BYTES] = {0};

static int
refuse_sha256(const void *, size_t, uint8_t[32])
{
  return -1;
}

static int
refuse_rsa3072_sha256(const uint8_t[VR_KEY_BYTES], const uint8_t[32],
                      const uint8_t[VR_SIGNATURE_BYTES])
{
  return -1;
}

extern const struct vr_crypto vr_xnu_crypto = {refuse_sha256, refuse_rsa3072_sha256};

int
vr_open(const uint8_t *, size_t, const uint8_t[VR_KEY_BYTES], uint64_t,
        const struct vr_crypto *, struct vr_view *)
{
  return VR_SIGNATURE;
}

int
vr_verify_block(const struct vr_view *, uint64_t, const void *, size_t,
                const struct vr_crypto *)
{
  return VR_BLOCK;
}

}  // extern "C"

// NativeUfs.cpp declares this weak, but the reference sits inside a template
// instantiation where the attribute is dropped, so the link still needs a
// definition. haydn has no boot-progress injection (CONFIG_XNU_BOOT_PROGRESS is
// off, which would require the whole owner-root chain), so this is a no-op. It is
// weak so a real libBootProgress.a can override it if that chain is ever enabled.
extern "C" __attribute__((weak)) void
boot_progress_poll(void)
{
}

