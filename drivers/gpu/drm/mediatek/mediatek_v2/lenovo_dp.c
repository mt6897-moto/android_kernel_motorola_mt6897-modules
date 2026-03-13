/*************************************
 *
 * lenovo_dp.c
 * probe dp modes
 *
**************************************/
#include "mtk_dp.h"
#include "mtk_dp_debug.h"
#include <drm/drm_modes.h>
#include "lenovo_dp.h"

#define MAX_EXT_MODES_SIZE  (FAKE_DEFAULT_RES-1)

typedef struct drm_display_mode drm_display_mode;

unsigned int dp_modes_pb_selected_index;
unsigned int dp_modes_pb_size;
drm_display_mode dptx_est_modes_pb[MAX_EXT_MODES_SIZE];

void dp_modes_probing_init(void) {
	return dp_modes_probing_clear();
}

void dp_modes_probing_clear(void) {
	DPTXFUNC("dp_modes_probing_clear!\n");
	memset(dptx_est_modes_pb, 0, MAX_EXT_MODES_SIZE*sizeof(drm_display_mode));
	dp_modes_pb_size = 0;
	dp_modes_pb_selected_index = FAKE_DEFAULT_RES;
	return;
}

void dp_modes_probing_add(void *pnewmode) {
	if(dp_modes_pb_size >= MAX_EXT_MODES_SIZE) return;
	memcpy(&dptx_est_modes_pb[dp_modes_pb_size],pnewmode,sizeof(drm_display_mode));
	dp_modes_pb_size++;
	return;
}

unsigned int dp_modes_probing_get_selected_index(void) {
	DPTXFUNC("selected_index is %d\n", dp_modes_pb_selected_index);
	return dp_modes_pb_selected_index;
}

unsigned int dp_modes_probing_get_modes_count(void) {
	DPTXFUNC("modes_count is %d\n", dp_modes_pb_size);
	return dp_modes_pb_size;
}

unsigned int dp_modes_probing_select(int hd, int vd, int fps) {
	unsigned int  status = FAKE_DEFAULT_RES;
	bool need_4k60 = false;
	int i;
	DPTXFUNC("hd=%d, vd=%d, fps=%d\n", hd,vd,fps);

	if(hd>=3840 && vd>=2160 && fps>=60) need_4k60 = true;

	for(i=0; i<dp_modes_pb_size; i++) {
		if(hd == dptx_est_modes_pb[i].hdisplay
		    && vd == dptx_est_modes_pb[i].vdisplay
		    && fps == drm_mode_vrefresh(&dptx_est_modes_pb[i])) {
			//only use the status to check if need to enable 4k60...
			status = need_4k60?SINK_3840_2160:SINK_640_480;
			dp_modes_pb_selected_index = i;
			break;
		}
	}
	DPTXFUNC("selected index=%d\n", dp_modes_pb_selected_index);
	return status;
}

void *dp_modes_probing_get(int index) {
	if(index >= dp_modes_pb_size) return NULL;
	return (void*)&dptx_est_modes_pb[index];
}
