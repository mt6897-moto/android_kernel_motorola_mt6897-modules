/***************************************
***        lenovo_dp.h           ***
****************************************/

void dp_modes_probing_init(void);
void dp_modes_probing_clear(void);
void dp_modes_probing_add(void *pnewmode);
unsigned int dp_modes_probing_get_modes_count(void);
unsigned int dp_modes_probing_get_selected_index(void);
unsigned int dp_modes_probing_select(int hd, int vd, int fps);
void *dp_modes_probing_get(int index);
