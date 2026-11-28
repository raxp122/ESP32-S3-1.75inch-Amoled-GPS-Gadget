// packs.h - the files of an app that live on the microSD (images, texts): asset packs.
//
// The CI builds them (tools/packs/) and attaches <name>.amp to every release. An app asks for
// its pack with the version it was written for; when /amulet/apps/<name>/.version differs, the
// pack is downloaded from the latest release and unpacked there (format: core.h), then the
// version file is written last, so an interrupted download is simply done again.
#pragma once
#include <stdbool.h>
#include "sd.h"

#define PACKS_DIR SD_MOUNT "/amulet/apps"

typedef enum { PACKJ_IDLE, PACKJ_RUNNING, PACKJ_DONE, PACKJ_FAILED } pack_job_t;

bool        packs_ready(const char *name, const char *version);
void        packs_install(const char *name, const char *version);   // in the background
pack_job_t  packs_job(void);
int         packs_progress(void);      // 0-100
const char *packs_error(void);
// "/sdcard/amulet/apps/<name>/<file>"
void        packs_path(const char *name, const char *file, char *out, int n);
