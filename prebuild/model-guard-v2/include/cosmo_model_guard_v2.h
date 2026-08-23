#ifndef COSMO_MODEL_GUARD_V2_H_
#define COSMO_MODEL_GUARD_V2_H_

#include <stddef.h>
#include <stdint.h>

#include <bmlib_runtime.h>

#ifndef CMG_V2_API
#if defined(__GNUC__) || defined(__clang__)
#define CMG_V2_API __attribute__((visibility("default")))
#else
#define CMG_V2_API
#endif
#endif

#define CMG_V2_ABI_MAJOR UINT32_C(2)

typedef struct CmgV2Artifact CmgV2Artifact;

typedef int32_t CmgV2Status;

#define CMG_V2_OK ((CmgV2Status)0)

#define CMG_V2_FORMAT_INVALID ((CmgV2Status) - 1001)
#define CMG_V2_FORMAT_UNSUPPORTED ((CmgV2Status) - 1002)
#define CMG_V2_FORMAT_SOURCE_MISMATCH ((CmgV2Status) - 1003)
#define CMG_V2_FORMAT_LIMIT ((CmgV2Status) - 1004)

#define CMG_V2_LICENSE_UNAVAILABLE ((CmgV2Status) - 2001)
#define CMG_V2_LICENSE_REJECTED ((CmgV2Status) - 2002)
/* v2.3 names for the same frozen ABI status values. */
#define CMG_V2_CERTIFICATE_UNAVAILABLE CMG_V2_LICENSE_UNAVAILABLE
#define CMG_V2_CERTIFICATE_REJECTED CMG_V2_LICENSE_REJECTED

#define CMG_V2_CRYPTO_FAILED ((CmgV2Status) - 3001)

#define CMG_V2_RESOURCE_INVALID_ARGUMENT ((CmgV2Status) - 4001)
#define CMG_V2_RESOURCE_INVALID_STATE ((CmgV2Status) - 4002)
#define CMG_V2_RESOURCE_IO ((CmgV2Status) - 4003)
#define CMG_V2_RESOURCE_NO_MEMORY ((CmgV2Status) - 4004)
#define CMG_V2_RESOURCE_BUSY ((CmgV2Status) - 4005)
#define CMG_V2_RESOURCE_INTERNAL ((CmgV2Status) - 4006)
#define CMG_V2_RESOURCE_ABI_MISMATCH ((CmgV2Status) - 4007)

#define CMG_V2_BACKEND_FAILED ((CmgV2Status) - 5001)

typedef uint32_t CmgV2SourceFormat;

#define CMG_V2_SOURCE_COSMO_NN_V1 ((CmgV2SourceFormat)UINT32_C(1))
#define CMG_V2_SOURCE_RAW_BMODEL ((CmgV2SourceFormat)UINT32_C(2))

typedef uint32_t CmgV2SophonLoadFlags;

#define CMG_V2_SOPHON_SHARE_MEM ((CmgV2SophonLoadFlags)UINT32_C(0x00000001))

#define CMG_V2_ARTIFACT_INFO_SIZE UINT32_C(72)
#define CMG_V2_SOPHON_LOAD_OPTIONS_SIZE UINT32_C(16)

typedef struct CmgV2ArtifactInfo {
  /* In: caller capacity. Out: bytes defined and written by the guard. */
  uint32_t struct_size;
  uint32_t source_format;
  uint32_t segment_count;
  uint32_t reserved;
  uint8_t artifact_id[16];
  uint64_t generation;
  uint8_t model_identity_sha256[32];
} CmgV2ArtifactInfo;

typedef struct CmgV2SophonLoadOptions {
  /* In: at least CMG_V2_SOPHON_LOAD_OPTIONS_SIZE; v2 reads only this prefix. */
  uint32_t struct_size;
  uint32_t flags;
  uint32_t reserved[2];
} CmgV2SophonLoadOptions;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * On every failure, *out_artifact is NULL. A successful open has already
 * authenticated the core and the device preset certificate, checked the live
 * device binding, and derived the preset-model content key.
 */
CMG_V2_API CmgV2Status CmgV2OpenArtifact(
    const char *installed_model_path, CmgV2SourceFormat expected_source_format,
    CmgV2Artifact **out_artifact);

/*
 * The caller zero-initializes out_info and sets struct_size to its capacity.
 * This ABI version requires at least CMG_V2_ARTIFACT_INFO_SIZE bytes, writes
 * only the known 72-byte prefix, and returns struct_size == 72 on success.
 */
CMG_V2_API CmgV2Status CmgV2GetArtifactInfo(const CmgV2Artifact *artifact,
                                            CmgV2ArtifactInfo *out_info);

/*
 * options may be NULL for defaults. Otherwise struct_size must be at least
 * CMG_V2_SOPHON_LOAD_OPTIONS_SIZE; larger structures are accepted but v2 reads
 * only the known 16-byte prefix. Known reserved fields must be zero and unknown
 * flags are rejected. On every failure, *out_bmrt is NULL. On success,
 * ownership of the bmrt handle transfers to the caller. bm_handle is borrowed
 * and must outlive the transferred bmrt handle.
 */
CMG_V2_API CmgV2Status CmgV2LoadSophonSegment(
    CmgV2Artifact *artifact, bm_handle_t bm_handle, uint32_t segment_index,
    const CmgV2SophonLoadOptions *options, void **out_bmrt);

/* NULL is accepted. A non-NULL artifact must be closed exactly once. */
CMG_V2_API void CmgV2CloseArtifact(CmgV2Artifact *artifact);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define CMG_V2_STATIC_ASSERT(condition, message)                               \
  static_assert((condition), message)
#define CMG_V2_ALIGNOF(type) alignof(type)
#else
#define CMG_V2_STATIC_ASSERT(condition, message)                               \
  _Static_assert((condition), message)
#define CMG_V2_ALIGNOF(type) _Alignof(type)
#endif

CMG_V2_STATIC_ASSERT(sizeof(CmgV2Status) == 4, "CmgV2Status must be 32 bits");
CMG_V2_STATIC_ASSERT(sizeof(CmgV2SourceFormat) == 4,
                     "CmgV2SourceFormat must be 32 bits");
CMG_V2_STATIC_ASSERT(sizeof(CmgV2SophonLoadFlags) == 4,
                     "CmgV2SophonLoadFlags must be 32 bits");

CMG_V2_STATIC_ASSERT(sizeof(CmgV2ArtifactInfo) == CMG_V2_ARTIFACT_INFO_SIZE,
                     "CmgV2ArtifactInfo ABI size mismatch");
CMG_V2_STATIC_ASSERT(CMG_V2_ALIGNOF(CmgV2ArtifactInfo) == 8,
                     "CmgV2ArtifactInfo ABI alignment mismatch");
CMG_V2_STATIC_ASSERT(offsetof(CmgV2ArtifactInfo, struct_size) == 0,
                     "CmgV2ArtifactInfo.struct_size ABI offset mismatch");
CMG_V2_STATIC_ASSERT(offsetof(CmgV2ArtifactInfo, source_format) == 4,
                     "CmgV2ArtifactInfo.source_format ABI offset mismatch");
CMG_V2_STATIC_ASSERT(offsetof(CmgV2ArtifactInfo, segment_count) == 8,
                     "CmgV2ArtifactInfo.segment_count ABI offset mismatch");
CMG_V2_STATIC_ASSERT(offsetof(CmgV2ArtifactInfo, reserved) == 12,
                     "CmgV2ArtifactInfo.reserved ABI offset mismatch");
CMG_V2_STATIC_ASSERT(offsetof(CmgV2ArtifactInfo, artifact_id) == 16,
                     "CmgV2ArtifactInfo.artifact_id ABI offset mismatch");
CMG_V2_STATIC_ASSERT(offsetof(CmgV2ArtifactInfo, generation) == 32,
                     "CmgV2ArtifactInfo.generation ABI offset mismatch");
CMG_V2_STATIC_ASSERT(
    offsetof(CmgV2ArtifactInfo, model_identity_sha256) == 40,
    "CmgV2ArtifactInfo.model_identity_sha256 ABI offset mismatch");

CMG_V2_STATIC_ASSERT(sizeof(CmgV2SophonLoadOptions) ==
                         CMG_V2_SOPHON_LOAD_OPTIONS_SIZE,
                     "CmgV2SophonLoadOptions ABI size mismatch");
CMG_V2_STATIC_ASSERT(CMG_V2_ALIGNOF(CmgV2SophonLoadOptions) == 4,
                     "CmgV2SophonLoadOptions ABI alignment mismatch");
CMG_V2_STATIC_ASSERT(offsetof(CmgV2SophonLoadOptions, struct_size) == 0,
                     "CmgV2SophonLoadOptions.struct_size ABI offset mismatch");
CMG_V2_STATIC_ASSERT(offsetof(CmgV2SophonLoadOptions, flags) == 4,
                     "CmgV2SophonLoadOptions.flags ABI offset mismatch");
CMG_V2_STATIC_ASSERT(offsetof(CmgV2SophonLoadOptions, reserved) == 8,
                     "CmgV2SophonLoadOptions.reserved ABI offset mismatch");

#undef CMG_V2_ALIGNOF
#undef CMG_V2_STATIC_ASSERT

#endif /* COSMO_MODEL_GUARD_V2_H_ */
