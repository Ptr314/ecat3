#ifndef MFM_TOOLS_H
#define MFM_TOOLS_H

#include <QString>
#include "mfm_formats.h"

uint8_t * generate_mfm_agat_140(QString file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[]);
uint8_t * generate_mfm_agat_840(QString file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[]);

void save_mfm_file(QString file_name, int sides, int tracks, int track_size, HXC_MFM_TRACK_INFO track_indexes[], uint8_t * data);

#endif // MFM_TOOLS_H
