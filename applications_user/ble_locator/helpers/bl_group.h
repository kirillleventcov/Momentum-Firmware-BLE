/**
 * @file bl_group.h
 * Device groups: a fingerprint learned by intersecting several captured
 * advertisers, plus the scoring used to decide "is this one of them?".
 */

#pragma once

#include "bl_adv.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BL_GROUP_NAME_MAX (23)
#define BL_GROUP_MAX      (24) /**< groups held in memory */
#define BL_CAPTURE_MAX    (8) /**< captures that can go into one group */

/** Criteria a group can assert. Only criteria present in every capture end up
 * enabled, which is what makes learning from several devices work. */
typedef enum {
    BlCritOui = 1U << 0,
    BlCritCompany = 1U << 1,
    BlCritMfgHead = 1U << 2,
    BlCritUuid128 = 1U << 3,
    BlCritUuid16 = 1U << 4,
    BlCritNamePrefix = 1U << 5,
    BlCritNoName = 1U << 6,
    BlCritAdTypes = 1U << 7,
    BlCritAdvLen = 1U << 8,
    BlCritMfgLen = 1U << 9,
    BlCritAddrType = 1U << 10,
    BlCritConnectable = 1U << 11,
} BlCriterion;

typedef struct {
    char name[BL_GROUP_NAME_MAX + 1];
    /* Stable across renames, reorders and deletions, because it is what names
     * the group's member file. Assigned on load for anything that lacks one. */
    uint16_t id;
    bool enabled;
    /* Set only by group files written before the app dropped its shipped
     * starter groups. Nothing in this version ever sets it; it exists so an
     * imported old file can have those entries dropped on the way in. */
    bool builtin;
    uint8_t captures; /**< how many advertisers were intersected */
    uint8_t threshold; /**< required score in percent, 0..100 */
    uint32_t crit; /**< bitmask of BlCriterion */

    uint8_t oui[3];
    uint16_t company_id;
    uint8_t mfg_head[BL_MFG_HEAD_MAX];
    uint8_t mfg_head_len;
    uint8_t uuid128[16];
    uint16_t uuid16[BL_UUID16_MAX];
    uint8_t uuid16_count;
    char name_prefix[BL_NAME_MAX + 1];
    uint8_t name_prefix_len;
    uint32_t adtype_mask;
    uint8_t adv_len_min;
    uint8_t adv_len_max;
    uint8_t mfg_len_min;
    uint8_t mfg_len_max;
    uint8_t addr_type;
    bool connectable;
} BlGroup;

/** Outcome of building a group, measured against everything else on air.
 *
 * A fingerprint is only useful if it separates the group from its surroundings,
 * so the build is scored against the devices that were *not* captured. These
 * numbers are what tells an operator whether the group is worth trusting.
 */
typedef struct {
    uint8_t captures; /**< advertisers that went into the group */
    uint16_t others_tested; /**< other devices on air at build time */
    uint16_t others_matched; /**< how many of those still match */
    uint8_t min_capture_score; /**< worst score among the captures */
    int8_t max_other_score; /**< best score among the others, -1 if none matched */
    bool separated; /**< true when nothing but the group matches */
} BlBuildReport;

/** A set of advertisers captured during a Learn session */
typedef struct {
    BlFeatures items[BL_CAPTURE_MAX];
    uint8_t count;
} BlCaptureSet;

/** In-memory group list */
typedef struct {
    BlGroup items[BL_GROUP_MAX];
    uint8_t count;
} BlGroupList;

/** Score `f` against `p`.
 *
 * @return  0..100 confidence, or -1 when a criterion is directly contradicted
 *          (a hard mismatch, e.g. a different manufacturer id).
 */
int bl_group_score(const BlGroup* p, const BlFeatures* f);

/** @return true if `f` should be reported as a hit for `p` */
static inline bool bl_group_matches(const BlGroup* p, const BlFeatures* f) {
    const int score = bl_group_score(p, f);
    return (score >= 0) && (score >= (int)p->threshold);
}

/** Build a group by intersecting every capture in `set`.
 *
 * Refuses to build a fingerprint that is too generic to be useful; in that case
 * `error` is set to a short human-readable reason.
 *
 * @return true on success
 */
bool bl_group_build(const BlCaptureSet* set, BlGroup* out, const char** error);

/** Raise a freshly built group's threshold until nothing else on air matches.
 *
 * `negative_scores` holds bl_group_score() results for every device that was
 * seen but not captured. The threshold is placed just above the best of them,
 * so far as that is possible without also excluding the captures themselves.
 * When no gap exists the group is left as sharp as it can be and the report
 * says so - a fingerprint that cannot be separated is worth knowing about.
 */
void bl_group_tighten(
    BlGroup* group,
    const BlCaptureSet* captures,
    const int8_t* negative_scores,
    size_t negative_count,
    BlBuildReport* report);

/** One-line human summary of what a group keys on, e.g. "name+mfg+uuid" */
void bl_group_describe(const BlGroup* p, char* out, size_t out_size);

/** Multi-line detail dump for the group info screen */
void bl_group_details(const BlGroup* p, FuriString* out);

/* --- persistence -------------------------------------------------------- */

/** Load groups from `path`. Missing file is not an error (list ends empty). */
bool bl_group_list_load(BlGroupList* list, const char* path);

/** Write every group in `list` to `path` (atomically enough for our needs) */
bool bl_group_list_save(const BlGroupList* list, const char* path);

/** Add `p` to `list`, replacing a same-named entry. @return false if full */
bool bl_group_list_add(BlGroupList* list, const BlGroup* p);

/** An id no group in `list` is using. Never 0, which means "not assigned". */
uint16_t bl_group_list_next_id(const BlGroupList* list);

/** Give every group without an id a unique one. Call right after loading. */
void bl_group_list_assign_ids(BlGroupList* list);

/** Remove entry `index` */
void bl_group_list_remove(BlGroupList* list, uint8_t index);

/** Strip leading and trailing spaces from a group name, in place */
void bl_group_name_trim(char* name);

/** Index of the group called `name` (exact match), or -1 */
int8_t bl_group_list_find(const BlGroupList* list, const char* name);

/** Why `name` cannot be given to group `self` (-1 for a new group), as a short
 * message for the keyboard, or NULL when it can. A name must not be blank after
 * trimming and no other group may already use it. */
const char* bl_group_name_problem(const BlGroupList* list, const char* name, int8_t self);

/** bl_group_name_problem() == NULL */
bool bl_group_name_available(const BlGroupList* list, const char* name, int8_t self);

#ifdef __cplusplus
}
#endif
