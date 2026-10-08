#include "vanilla_window_vulkan_utils.h"

VANILLA_PostProcessingCreateInfo* postProcessingInfos = NULL;
bool enablePostProcessing = false;

VANILLA_EXPORT VANILLA_RESULT VANILLA_configurePostProcessing(const VANILLA_PostProcessingCreateInfo* infos, uint16_t count) {
    if (didstart == true) { return VANILLA_ERR_ALREADY_STARTED; }
    if (postProcessingInfos != NULL || count == 0) { return VANILLA_SUCCESS; }
    if (infos == NULL) { return VANILLA_ERR_NULL_POINTER; }

    postProcessingInfos = VANILLA_mmap(sizeof(VANILLA_PostProcessingCreateInfo) * count);
    if (postProcessingInfos == VANILLA_MAP_FAILED) {
        postProcessingInfos = NULL;
        return VANILLA_ERR_ALLOC_FAILED;
    }

    memcpy(postProcessingInfos, infos, sizeof(VANILLA_PostProcessingCreateInfo) * count);
    vulkan.postProcessingCount = count;

    return VANILLA_SUCCESS;
}

VANILLA_EXPORT void VANILLA_enablePostProcessing(void) {
    if (didstart == true) { return; }
    enablePostProcessing = true;
}

VANILLA_EXPORT void VANILLA_disablePostProcessing(void) {
    if (didstart == true) { return; }
    enablePostProcessing = false;
}