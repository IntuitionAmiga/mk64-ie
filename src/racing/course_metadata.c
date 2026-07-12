#include <string.h>

#include "course_metadata.h"

#include "asset_endian.h"
#include "platform/platform.h"

#define COURSE_METADATA_MAGIC "MK64META"
#define COURSE_METADATA_MAGIC_LEN 8
#define COURSE_METADATA_VERSION 1
#define COURSE_METADATA_RECORD_SIZE 16
#define COURSE_METADATA_HEADER_SIZE 16

static struct CourseMetadata sCourseMetadata[COURSE_METADATA_COURSES];
static int sCourseMetadataCount = 0;

int course_metadata_parse(const void* blob, size_t size,
                          struct CourseMetadata* out, size_t max_courses) {
    const uint8_t* bytes = (const uint8_t*) blob;
    uint32_t version;
    uint32_t count;
    uint32_t i;

    if (size < COURSE_METADATA_HEADER_SIZE) {
        return -1;
    }
    if (memcmp(bytes, COURSE_METADATA_MAGIC, COURSE_METADATA_MAGIC_LEN) != 0) {
        return -1;
    }
    version = asset_be_u32(bytes + 8);
    count = asset_be_u32(bytes + 12);
    if (version != COURSE_METADATA_VERSION) {
        return -1;
    }
    if (count > max_courses) {
        return -1;
    }
    if (size != COURSE_METADATA_HEADER_SIZE + (size_t) count * COURSE_METADATA_RECORD_SIZE) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        const uint8_t* rec = bytes + COURSE_METADATA_HEADER_SIZE
                           + i * COURSE_METADATA_RECORD_SIZE;
        out[i].vertexCount = asset_be_u32(rec + 0);
        out[i].packedOffset = asset_be_u32(rec + 4);
        out[i].finalDisplaylistOffset = asset_be_u32(rec + 8);
        out[i].unknown1 = asset_be_u32(rec + 12);
    }
    return (int) count;
}

void course_metadata_load(void) {
    uint8_t blob[COURSE_METADATA_HEADER_SIZE
                 + COURSE_METADATA_COURSES * COURSE_METADATA_RECORD_SIZE];
    size_t size = 0;
    int count;

    if (!platform_asset_read("course_metadata.bin", blob, sizeof(blob), &size)) {
        platform_fatal("failed to read asset course_metadata.bin");
    }
    count = course_metadata_parse(blob, size, sCourseMetadata, COURSE_METADATA_COURSES);
    if (count != COURSE_METADATA_COURSES) {
        platform_fatal("course_metadata.bin is malformed (%zu bytes)", size);
    }
    sCourseMetadataCount = count;
}

const struct CourseMetadata* course_metadata_get(int courseId) {
    if (sCourseMetadataCount == 0) {
        platform_fatal("course_metadata_get(%d) before course_metadata_load", courseId);
    }
    if (courseId < 0 || courseId >= sCourseMetadataCount) {
        platform_fatal("course_metadata_get(%d): no such course", courseId);
    }
    return &sCourseMetadata[courseId];
}
