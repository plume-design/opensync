#ifndef SM_STATS_PUB_H_INCLUDED
#define SM_STATS_PUB_H_INCLUDED

#include <stdint.h>
#include <string.h>

#include "dppline.h"
#include "stats_pub.h"
#include "target.h"

int     sm_stats_pub_init(void);
void    sm_stats_pub_uninit(void);

int     sm_stats_pub_survey_init(void);
void    sm_stats_pub_survey_uninit(void);
int     sm_stats_pub_survey_update(
        char *phy_name,
        char *if_name,
        int channel,
        dpp_survey_record_t *rec);
stats_pub_survey_t* sm_stats_pub_survey_get(char *phy_name, int channel);

int     sm_stats_pub_device_init(void);
void    sm_stats_pub_device_uninit(void);
int     sm_stats_pub_device_update(dpp_device_record_t *rec);
stats_pub_device_t* sm_stats_pub_device_get(void);

int     sm_stats_pub_client_init(void);
void    sm_stats_pub_client_uninit(void);
int     sm_stats_pub_client_update(mac_address_t mac, dpp_client_stats_t *rec);
stats_pub_client_t* sm_stats_pub_client_get(mac_address_t mac);

#endif /* SM_STATS_PUB_H_INCLUDED */
