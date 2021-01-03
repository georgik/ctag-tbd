/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2020 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

#include "ctagSampleRom.hpp"
#include "esp_spi_flash.h"
#include "esp_log.h"

#ifdef TBD_SIM
#define CONFIG_SAMPLE_ROM_START_ADDRESS 0
#else

#include "sdkconfig.h"

#endif

namespace CTAG::SP::HELPERS {

    uint32_t ctagSampleRom::totalSize = 0;
    uint32_t ctagSampleRom::numberSlices = 0;
    uint32_t ctagSampleRom::headerSize = 0;
    vector<uint32_t> ctagSampleRom::v_sliceSizes;
    vector<uint32_t> ctagSampleRom::v_sliceOffsets;
    bool ctagSampleRom::isInitialized = false;

    ctagSampleRom::ctagSampleRom() {
        if(isInitialized) return;
        RefreshDataStructure();
    }

    uint32_t ctagSampleRom::GetNumberSlices() {
        return numberSlices;
    }

    uint32_t ctagSampleRom::GetSliceSize(const uint32_t slice) {
        if(!isInitialized) return 0;
        if(slice >= v_sliceSizes.size()) return 0;
        return v_sliceSizes[slice];
    }

    uint32_t ctagSampleRom::GetSliceGroupSize(const uint32_t startSlice, const uint32_t endSlice) {
        if(!isInitialized) return 0;
        if(endSlice <= startSlice) return 0;
        uint32_t totalSize = 0;
        for (uint32_t i = startSlice; i <= endSlice; i++) {
            totalSize += v_sliceSizes[i];
        }
        return totalSize;
    }

    uint32_t ctagSampleRom::GetSliceOffset(const uint32_t slice) {
        if(!isInitialized) return 0;
        if(slice >= v_sliceSizes.size()) return 0;
        return v_sliceOffsets[slice];
    }

    // reads words, offset in words not bytes
    void ctagSampleRom::Read(int16_t *dst, uint32_t offset, const uint32_t n_samples) {
        if(!isInitialized) return;
        assert(dst != NULL);
        offset *= 2; // from int16 to bytes
        offset += headerSize; // add header size
        offset += CONFIG_SAMPLE_ROM_START_ADDRESS; // add start offset
        spi_flash_read(offset, dst, n_samples * 2);
    }

    bool ctagSampleRom::HasSlice(const uint32_t slice) {
        if(!isInitialized) return 0;
        if(slice >= numberSlices) return false;
        return true;
    }

    bool ctagSampleRom::HasSliceGroup(const uint32_t startSlice, const uint32_t endSlice) {
        if(!isInitialized) return 0;
        if (startSlice > numberSlices || endSlice > numberSlices) return false;
        return true;
    }

    uint32_t ctagSampleRom::GetFirstNonWaveTableSlice() {
        if(!isInitialized) return 0;
        for(int i=0;i<v_sliceSizes.size();i++){
            if(v_sliceSizes[i] > 256)
                return i;
        }
        return 0;
    }

    void ctagSampleRom::ReadSlice(int16_t *dst, const uint32_t slice, const uint32_t offset, const uint32_t n_samples) {
        if(!isInitialized) return;
        uint32_t start = v_sliceOffsets[slice] + offset;
        int32_t len = n_samples;
        if(offset + len >= v_sliceSizes[slice]){ // read beyond slice end ?
            len = v_sliceSizes[slice] - offset;
        }
        if(len <= 0) return; // nothing to read!
        Read(dst, start, len);
    }

    void ctagSampleRom::ReadSliceAsFloat(float *dst, const uint32_t slice, const uint32_t offset,
                                         const uint32_t n_samples) {
        if(!isInitialized) return;
        uint32_t start = v_sliceOffsets[slice] + offset;
        int32_t len = n_samples;
        if(offset + len >= v_sliceSizes[slice]){ // read beyond slice end ?
            len = v_sliceSizes[slice] - offset;
        }
        if(len <= 0) return; // nothing to read!
        int16_t idst[len];
        int16_t *dptr = idst;
        Read(idst, start, len);
        while(len--){
            *dst++ = float(*dptr++) * 0.000030518509476f;
        }
    }

    void ctagSampleRom::RefreshDataStructure() {
        isInitialized = false;
        v_sliceSizes.clear();
        v_sliceSizes.shrink_to_fit();
        v_sliceOffsets.clear();
        v_sliceOffsets.shrink_to_fit();
        uint32_t deadface = 0;
        headerSize = 0;
        spi_flash_read(CONFIG_SAMPLE_ROM_START_ADDRESS, &deadface, 4);
        headerSize += 4;
        if (deadface != 0xdeadface){
            ESP_LOGE("SROM", "Magic number wrong!");
            return;
        }
        spi_flash_read(CONFIG_SAMPLE_ROM_START_ADDRESS + 4, &totalSize, 4);
        headerSize += 4;
        ESP_LOGD("SROM", "Total sample data size %d bytes", totalSize);
        spi_flash_read(CONFIG_SAMPLE_ROM_START_ADDRESS + 8, &numberSlices, 4);
        headerSize += 4;
        ESP_LOGD("SROM", "Number slices %d", numberSlices);
        v_sliceOffsets.resize(numberSlices);
        spi_flash_read(CONFIG_SAMPLE_ROM_START_ADDRESS + 12, &v_sliceOffsets[0], 4 * numberSlices);
        headerSize += 4 * numberSlices;
        int lastOffset = 0;
        for (uint32_t i = 0; i < v_sliceOffsets.size(); i++) {
            v_sliceSizes.push_back(v_sliceOffsets[i] - lastOffset);
            lastOffset = v_sliceOffsets[i];
            v_sliceOffsets[i] -= v_sliceSizes[i];
            ESP_LOGD("SROM", "Slice size %d, offset %d", v_sliceSizes[i], v_sliceOffsets[i]);
        }
        v_sliceSizes.shrink_to_fit();
        v_sliceOffsets.shrink_to_fit();
        isInitialized = true;
    }
}