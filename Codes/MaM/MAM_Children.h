#ifndef MAM_CHILDREN_H
#define MAM_CHILDREN_H

int MAM_Check_children_type(void);

void MAM_Children_init(void (*i_user_function)(void *), void *i_user_args, malleability_data_t *rep_s_data, malleability_data_t *dist_s_data, 
                    malleability_data_t *rep_a_data, malleability_data_t *dist_a_data);

#endif