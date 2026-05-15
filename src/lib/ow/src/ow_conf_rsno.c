/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* opensync */
#include <ff_lib.h>
#include <ff_provider.h>
#include <log.h>

/* osw */
#include <osw_ut.h>

/* local */
#include "ow_conf_rsno.h"
#include "osw_types.h"

const char *ow_conf_rsno_mode_to_cstr(enum ow_conf_rsno_mode mode)
{
    switch (mode)
    {
        case OW_CONF_RSNO_MODE_DISABLED:
            return "disabled";
        case OW_CONF_RSNO_MODE_WPA3_COMPAT:
            return "wpa3_compat";
        case OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION:
            return "wpa3_compat_transition";
    }
    return NULL;
}

ow_conf_rsno_mode_t ow_conf_rsno_mode_get(struct osw_conf_vif *vif)
{
    if (ff_is_flag_enabled("rsno_disabled"))
    {
        return OW_CONF_RSNO_MODE_DISABLED;
    }

    if (ff_is_flag_enabled("rsno_wpa3_compat"))
    {
        return OW_CONF_RSNO_MODE_WPA3_COMPAT;
    }

    if (ff_is_flag_enabled("rsno_wpa3_compat_wifi7_only"))
    {
        if (vif->u.ap.wpa.akm_sae_ext)
        {
            return OW_CONF_RSNO_MODE_WPA3_COMPAT;
        }
        else
        {
            return OW_CONF_RSNO_MODE_DISABLED;
        }
    }

    if (ff_is_flag_enabled("rsno_wpa3_compat_transition"))
    {
        return OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION;
    }

    return OW_CONF_RSNO_MODE_DISABLED;
}

static uint32_t ow_conf_rsno_rsne_akm_allowed(ow_conf_rsno_mode_t mode, enum osw_band band)
{
    const bool is_6ghz = (band == OSW_BAND_6GHZ);
    uint32_t akm = 0;
    switch (mode)
    {
        case OW_CONF_RSNO_MODE_DISABLED:
            akm |= UINT32_MAX;
            break;
        case OW_CONF_RSNO_MODE_WPA3_COMPAT:
            akm |= (1 << OSW_AKM_WPA_PSK);
            akm |= (1 << OSW_AKM_RSN_PSK);
            akm |= (1 << OSW_AKM_RSN_PSK_SHA256);
            akm |= (1 << OSW_AKM_RSN_PSK_SHA384);
            akm |= (1 << OSW_AKM_RSN_FT_PSK);
            akm |= (1 << OSW_AKM_RSN_FT_PSK_SHA384);
            akm |= (is_6ghz ? (1 << OSW_AKM_RSN_SAE) : 0);
            akm |= (is_6ghz ? (1 << OSW_AKM_RSN_FT_SAE) : 0);
            break;
        case OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION:
            akm |= (1 << OSW_AKM_WPA_PSK);
            akm |= (1 << OSW_AKM_RSN_PSK);
            akm |= (1 << OSW_AKM_RSN_PSK_SHA256);
            akm |= (1 << OSW_AKM_RSN_PSK_SHA384);
            akm |= (1 << OSW_AKM_RSN_SAE);
            akm |= (1 << OSW_AKM_RSN_FT_PSK);
            akm |= (1 << OSW_AKM_RSN_FT_PSK_SHA384);
            akm |= (1 << OSW_AKM_RSN_FT_SAE);
            break;
    }
    return akm;
}

static uint32_t ow_conf_rsno_rsne_pairwise_allowed(ow_conf_rsno_mode_t mode)
{
    uint32_t pairwise = 0;
    switch (mode)
    {
        case OW_CONF_RSNO_MODE_DISABLED:
            pairwise |= UINT32_MAX;
            break;
        case OW_CONF_RSNO_MODE_WPA3_COMPAT:
            pairwise |= (1 << OSW_CIPHER_WPA_TKIP);
            pairwise |= (1 << OSW_CIPHER_WPA_CCMP);
            pairwise |= (1 << OSW_CIPHER_RSN_CCMP_128);
            break;
        case OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION:
            pairwise |= (1 << OSW_CIPHER_WPA_TKIP);
            pairwise |= (1 << OSW_CIPHER_WPA_CCMP);
            pairwise |= (1 << OSW_CIPHER_RSN_CCMP_128);
            break;
    }
    return pairwise;
}

static uint32_t ow_conf_rsno_1_akm_allowed(ow_conf_rsno_mode_t mode, enum osw_band band)
{
    const bool is_6ghz = (band == OSW_BAND_6GHZ);
    uint32_t akm = 0;
    switch (mode)
    {
        case OW_CONF_RSNO_MODE_DISABLED:
            break;
        case OW_CONF_RSNO_MODE_WPA3_COMPAT:
            akm |= (is_6ghz ? 0 : (1 << OSW_AKM_RSN_SAE));
            akm |= (is_6ghz ? 0 : (1 << OSW_AKM_RSN_FT_SAE));
            break;
        case OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION:
            break;
    }
    return akm;
}

static uint32_t ow_conf_rsno_1_pairwise_allowed(ow_conf_rsno_mode_t mode)
{
    uint32_t pairwise = 0;
    switch (mode)
    {
        case OW_CONF_RSNO_MODE_DISABLED:
            break;
        case OW_CONF_RSNO_MODE_WPA3_COMPAT:
            pairwise |= (1 << OSW_CIPHER_RSN_CCMP_128);
            pairwise |= (1 << OSW_CIPHER_RSN_CCMP_256);
            pairwise |= (1 << OSW_CIPHER_RSN_GCMP_128);
            pairwise |= (1 << OSW_CIPHER_RSN_GCMP_256);
            break;
        case OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION:
            break;
    }
    return pairwise;
}

static uint32_t ow_conf_rsno_2_akm_allowed(ow_conf_rsno_mode_t mode)
{
    uint32_t akm = 0;
    switch (mode)
    {
        case OW_CONF_RSNO_MODE_DISABLED:
            break;
        case OW_CONF_RSNO_MODE_WPA3_COMPAT:
            akm |= (1 << OSW_AKM_RSN_SAE_EXT);
            akm |= (1 << OSW_AKM_RSN_FT_SAE_EXT);
        case OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION:
            akm |= (1 << OSW_AKM_RSN_SAE_EXT);
            akm |= (1 << OSW_AKM_RSN_FT_SAE_EXT);
            break;
    }
    return akm;
}

static uint32_t ow_conf_rsno_2_pairwise_allowed(ow_conf_rsno_mode_t mode)
{
    uint32_t pairwise = 0;
    switch (mode)
    {
        case OW_CONF_RSNO_MODE_DISABLED:
            break;
        case OW_CONF_RSNO_MODE_WPA3_COMPAT:
            pairwise |= (1 << OSW_CIPHER_RSN_GCMP_256);
            break;
        case OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION:
            pairwise |= (1 << OSW_CIPHER_RSN_GCMP_256);
            break;
    }
    return pairwise;
}

void ow_conf_rsno_mutate_vif_ap(struct osw_conf_vif *osw_vif, ow_conf_rsno_mode_t mode)
{
    struct osw_conf_vif_ap *ap = &osw_vif->u.ap;
    struct osw_wpa *wpa = &ap->wpa;
    const struct osw_channel *c = &ap->channel;
    const enum osw_band band = osw_freq_to_band(c->control_freq_mhz);

    memset(&ap->rsn_override_1, 0, sizeof(ap->rsn_override_1));
    memset(&ap->rsn_override_2, 0, sizeof(ap->rsn_override_2));

    const uint32_t akm = osw_wpa_get_akm_bitmask(wpa);
    const uint32_t pairwise = osw_wpa_get_pairwise_bitmask(wpa);
    const bool pmf_not_disabled = (wpa->pmf != OSW_PMF_DISABLED);

    const uint32_t rsno_1_akm_allowed = ow_conf_rsno_1_akm_allowed(mode, band);
    const uint32_t rsno_1_akm = (akm & rsno_1_akm_allowed);
    const uint32_t rsno_1_pairwise_allowed = ow_conf_rsno_1_pairwise_allowed(mode);
    const uint32_t rsno_1_pairwise = (pairwise & rsno_1_pairwise_allowed);

    const uint32_t rsno_2_akm_allowed = ow_conf_rsno_2_akm_allowed(mode);
    const uint32_t rsno_2_akm = (akm & rsno_2_akm_allowed);
    const uint32_t rsno_2_pairwise_allowed = ow_conf_rsno_2_pairwise_allowed(mode);
    const uint32_t rsno_2_pairwise = (pairwise & rsno_2_pairwise_allowed);

    if (rsno_1_akm != 0 && rsno_1_pairwise != 0 && pmf_not_disabled)
    {
        ap->rsn_override_1.enabled = true;
        ap->rsn_override_1.akm = rsno_1_akm;
        ap->rsn_override_1.pairwise = rsno_1_pairwise;
        ap->rsn_override_1.pmf = OSW_PMF_REQUIRED;
    }

    if (rsno_2_akm != 0 && rsno_2_pairwise != 0 && pmf_not_disabled)
    {
        ap->rsn_override_2.enabled = true;
        ap->rsn_override_2.akm = rsno_2_akm;
        ap->rsn_override_2.pairwise = rsno_2_pairwise;
        ap->rsn_override_2.pmf = OSW_PMF_REQUIRED;
    }

    const uint32_t rsno1_akm_moved = (ap->rsn_override_1.enabled ? rsno_1_akm : 0);
    const uint32_t rsno2_akm_moved = (ap->rsn_override_2.enabled ? rsno_2_akm : 0);
    const uint32_t rsne_akm_moved = rsno1_akm_moved | rsno2_akm_moved;
    const uint32_t rsno1_pairwise_moved = (ap->rsn_override_1.enabled ? rsno_1_pairwise : 0);
    const uint32_t rsno2_pairwise_moved = (ap->rsn_override_2.enabled ? rsno_2_pairwise : 0);
    const uint32_t rsne_pairwise_moved = rsno1_pairwise_moved | rsno2_pairwise_moved;
    const uint32_t rsne_akm_allowed = ow_conf_rsno_rsne_akm_allowed(mode, band);
    const uint32_t rsne_akm = (akm & rsne_akm_allowed) | (akm & ~rsne_akm_moved);
    const uint32_t rsne_pairwise_allowed = ow_conf_rsno_rsne_pairwise_allowed(mode);
    const uint32_t rsne_pairwise = (pairwise & rsne_pairwise_allowed) | (pairwise & ~rsne_pairwise_moved);

    if (rsne_akm == 0)
    {
        /* RSNE must have at least one AKM. This could empty here be because
         * this is an EAP setup, or Open network.
         *
         * EAP + PSK/SAE setup, while technically compliant to various specs,
         * is not expected to be supported for the forseeable future. Most
         * client side implementations wouldn't even present such a network
         * properly to the end-user.
         */
        return;
    }

    if (rsne_pairwise == 0)
    {
        /* RSNE must have at least one pairwise cipher. See above. */
        return;
    }

    const bool rsne_has_sae = (rsne_akm & (1 << OSW_AKM_RSN_SAE)) != 0;
    const bool rsno1_has_pmf = (ap->rsn_override_1.pmf != OSW_PMF_DISABLED);
    const bool rsno2_has_pmf = (ap->rsn_override_2.pmf != OSW_PMF_DISABLED);
    const bool rsno_has_pmf = rsno1_has_pmf || rsno2_has_pmf;
    if (rsne_has_sae == false && rsno_has_pmf && wpa->pmf == OSW_PMF_OPTIONAL)
    {
        wpa->pmf = OSW_PMF_DISABLED;
    }

    osw_wpa_set_akm_bitmask(wpa, rsne_akm);
    osw_wpa_set_pairwise_bitmask(wpa, rsne_pairwise);

    if (ap->rsn_override_1.enabled || ap->rsn_override_2.enabled)
    {
        switch (band)
        {
            case OSW_BAND_2GHZ:
                ap->rsn_override_omit_rsnxe = false;
                break;
            case OSW_BAND_5GHZ:
                ap->rsn_override_omit_rsnxe = false;
                break;
            case OSW_BAND_6GHZ:
                break;
            case OSW_BAND_UNDEFINED:
                break;
        }
    }
}

static uint32_t ow_conf_rsno_akm_sae_bitmask(void)
{
    uint32_t akm = 0;
    akm |= (1 << OSW_AKM_RSN_SAE);
    akm |= (1 << OSW_AKM_RSN_FT_SAE);
    akm |= (1 << OSW_AKM_RSN_SAE_EXT);
    akm |= (1 << OSW_AKM_RSN_FT_SAE_EXT);
    return akm;
}

static void ow_conf_rsno_fix_wpa_with_rsno(struct osw_wpa *wpa, const struct osw_rsn_override *rsno)
{
    if (rsno->enabled == false) return;

    const bool rsne_sae = osw_wpa_is_sae(wpa);
    const bool rsne_psk = osw_wpa_is_psk(wpa);
    const bool rsno_sae = (rsno->akm & ow_conf_rsno_akm_sae_bitmask()) != 0;
    const bool sae = rsne_sae || rsno_sae;
    const bool pmf_required = sae && !rsne_psk;
    const bool pmf_optional = sae && rsne_psk;

    wpa->akm_sae |= !!(rsno->akm & (1 << OSW_AKM_RSN_SAE));
    wpa->akm_ft_sae |= !!(rsno->akm & (1 << OSW_AKM_RSN_FT_SAE));
    wpa->akm_sae_ext |= !!(rsno->akm & (1 << OSW_AKM_RSN_SAE_EXT));
    wpa->akm_ft_sae_ext |= !!(rsno->akm & (1 << OSW_AKM_RSN_FT_SAE_EXT));

    wpa->pairwise_ccmp |= !!(rsno->pairwise & (1 << OSW_CIPHER_RSN_CCMP_128));
    wpa->pairwise_ccmp256 |= !!(rsno->pairwise & (1 << OSW_CIPHER_RSN_CCMP_256));
    wpa->pairwise_gcmp |= !!(rsno->pairwise & (1 << OSW_CIPHER_RSN_GCMP_128));
    wpa->pairwise_gcmp256 |= !!(rsno->pairwise & (1 << OSW_CIPHER_RSN_GCMP_256));

    switch (wpa->pmf)
    {
        case OSW_PMF_DISABLED:
            if (pmf_optional) wpa->pmf = OSW_PMF_OPTIONAL;
            if (pmf_required) wpa->pmf = OSW_PMF_REQUIRED;
            break;
        case OSW_PMF_OPTIONAL:
            if (pmf_required) wpa->pmf = OSW_PMF_REQUIRED;
            break;
        case OSW_PMF_REQUIRED:
            break;
    }
}

void ow_conf_rsno_fix_wpa_with_state(struct osw_wpa *wpa, const struct osw_drv_vif_state_ap *ap)
{
    ow_conf_rsno_fix_wpa_with_rsno(wpa, &ap->rsn_override_1);
    ow_conf_rsno_fix_wpa_with_rsno(wpa, &ap->rsn_override_2);
}

static void ow_conf_rsno_dump_wpa(const char *prefix, const struct osw_wpa *wpa)
{
    char buf[64];
    osw_wpa_to_str(buf, sizeof(buf), wpa);
    LOGI("%s%s", prefix, buf);
}

static void ow_conf_rsno_dump_wpa_compare(const struct osw_wpa *before, const struct osw_wpa *after)
{
    ow_conf_rsno_dump_wpa("before: ", before);
    ow_conf_rsno_dump_wpa(" after: ", after);
}

OSW_UT(ow_conf_rsno_wpa2_psk_only)
{
    const struct osw_wpa orig = {
        .wpa = 0,
        .rsn = true,
        .akm_psk = true,
        .pairwise_ccmp = true,
    };
    struct osw_conf_vif vif = {
        .u.ap =
                {
                    .channel =
                            {
                                .control_freq_mhz = 2412,
                            },
                },
    };

    /* WPA2 PSK only should yield no changes to WPA configuration */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_DISABLED);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);
}

OSW_UT(ow_conf_rsno_wpa2_psk_only_with_gcmp_negative)
{
    const struct osw_wpa orig = {
        .wpa = 0,
        .rsn = true,
        .akm_psk = true,
        .pairwise_ccmp = true,
        .pairwise_gcmp = true,
    };
    struct osw_conf_vif vif = {
        .u.ap =
                {
                    .channel =
                            {
                                .control_freq_mhz = 2412,
                            },
                },
    };

    /* WFA spec forbids certain AKMs to be used in RSNO1 and RSNO2. The PSK
     * cannot be included in RSNO1 nor RSNO2. This means that pairwise ciphers
     * _alone_, especially for PSK-only cannot be split into RSNE + RSNOx.
     *
     * The spec makes it unclear if RSNOx AKM list can be empty _and_ pairwise
     * non-empty (to cover this particular case).
     *
     * The implementations may opt to infer RSNOx enablement from AKM being
     * non-empty. hostapd does that for example. This means it is impossible to
     * even signal RSNO1 with empty AKM and GCMP pairwise.
     *
     * This is therefore a negative test.
     */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_DISABLED);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);
}

OSW_UT(ow_conf_rsno_rsn_psk_sae_and_sae_ext_but_no_pmf_negative)
{
    const struct osw_wpa orig = {
        .wpa = 0,
        .rsn = true,
        .akm_psk = true,
        .akm_sae = true,
        .akm_sae_ext = true,
        .pairwise_ccmp = true,
        .pairwise_gcmp256 = true,
        .pmf = OSW_PMF_DISABLED,
    };
    struct osw_conf_vif vif = {
        .u.ap =
                {
                    .channel =
                            {
                                .control_freq_mhz = 2412,
                            },
                },
    };

    /* RSNE with PSK+SAE+SAE-EXT but PMF disabled is invalid configuration.
     * This is a negative test to ensure no changes are made to WPA config */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_DISABLED);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);
}

OSW_UT(ow_conf_rsno_rsn_eap_negative)
{
    const struct osw_wpa orig = {
        .wpa = 0,
        .rsn = true,
        .akm_eap = true,
        .pairwise_ccmp = true,
        .pmf = OSW_PMF_REQUIRED,
    };
    struct osw_conf_vif vif = {
        .u.ap =
                {
                    .channel =
                            {
                                .control_freq_mhz = 2412,
                            },
                },
    };

    /* RSNE with EAP only is invalid configuration for RSNO. This is a negative
     * test to ensure no changes are made to WPA config */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_DISABLED);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);
}

OSW_UT(ow_conf_rsno_no_wpa_no_rsn_negative)
{
    const struct osw_wpa orig = {
        .wpa = true,
        .rsn = false,
        .akm_psk = true,
        .pairwise_tkip = true,
    };
    struct osw_conf_vif vif = {
        .u.ap =
                {
                    .channel =
                            {
                                .control_freq_mhz = 2412,
                            },
                },
    };

    /* No RSNE (open network) is invalid configuration for RSNO. This is a
     * negative test to ensure no changes are made to WPA config */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_DISABLED);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);
}

OSW_UT(ow_conf_rsno_wpa_mixed_psk_only)
{
    const struct osw_wpa orig = {
        .wpa = true,
        .rsn = true,
        .akm_psk = true,
        .pairwise_tkip = true,
        .pairwise_ccmp = true,
    };
    struct osw_conf_vif vif = {
        .u.ap =
                {
                    .channel =
                            {
                                .control_freq_mhz = 2412,
                            },
                },
    };

    /* WPA+WPA2 PSK only should yield no changes to WPA configuration */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_DISABLED);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);
}

OSW_UT(ow_conf_rsno_wpa3_transition)
{
    const struct osw_wpa orig = {
        .wpa = false,
        .rsn = true,
        .akm_psk = true,
        .akm_sae = true,
        .pairwise_ccmp = true,
        .pmf = OSW_PMF_OPTIONAL,
    };
    struct osw_conf_vif vif = {
        .u.ap =
                {
                    .channel =
                            {
                                .control_freq_mhz = 2412,
                            },
                },
    };
    struct osw_drv_vif_state_ap ap = {};

    /* WPA3 Transition (PSK+SAE) should yield no changes to WPA configuration
     * unless mode is WPA3 Compatibility */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_DISABLED);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    /* WPA3 Compatibility (PSK only) should strip SAE from WPA configuration and move it to RSNO1 */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(vif.u.ap.wpa.wpa == 0);
    OSW_UT_EVAL(vif.u.ap.wpa.rsn == 1);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_psk == true);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_sae == false);
    OSW_UT_EVAL(vif.u.ap.wpa.pairwise_ccmp == true);
    OSW_UT_EVAL(vif.u.ap.wpa.pmf == OSW_PMF_DISABLED);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == true);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.akm == (1 << OSW_AKM_RSN_SAE));
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.pairwise == (1 << OSW_CIPHER_RSN_CCMP_128));
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.pmf == OSW_PMF_REQUIRED);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_omit_rsnxe == false);

    ap.rsn_override_1 = vif.u.ap.rsn_override_1;
    ap.rsn_override_2 = vif.u.ap.rsn_override_2;
    ow_conf_rsno_fix_wpa_with_state(&vif.u.ap.wpa, &ap);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
}

OSW_UT(ow_conf_rsno_wpa3_transition_with_wpa1)
{
    const struct osw_wpa orig = {
        .wpa = true,
        .rsn = true,
        .akm_psk = true,
        .akm_sae = true,
        .pairwise_tkip = true,
        .pairwise_ccmp = true,
        .pmf = OSW_PMF_OPTIONAL,
    };
    struct osw_conf_vif vif = {
        .u.ap =
                {
                    .channel =
                            {
                                .control_freq_mhz = 2412,
                            },
                },
    };
    struct osw_drv_vif_state_ap ap = {};

    /* WPA3 Transition (PSK+SAE) with WPA1 (PSK-only) should yield no changes
     * to WPA configuration unless mode is WPA3 Compatibility */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_DISABLED);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    /* WPA3 Compatibility (PSK only) should strip SAE from WPA configuration
     * and move it to RSNO1 */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(vif.u.ap.wpa.wpa == 1);
    OSW_UT_EVAL(vif.u.ap.wpa.rsn == 1);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_psk == true);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_sae == false);
    OSW_UT_EVAL(vif.u.ap.wpa.pairwise_tkip == true);
    OSW_UT_EVAL(vif.u.ap.wpa.pairwise_ccmp == true);
    OSW_UT_EVAL(vif.u.ap.wpa.pmf == OSW_PMF_DISABLED);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == true);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.akm == (1 << OSW_AKM_RSN_SAE));
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.pairwise == (1 << OSW_CIPHER_RSN_CCMP_128));
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.pmf == OSW_PMF_REQUIRED);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_omit_rsnxe == false);

    ap.rsn_override_1 = vif.u.ap.rsn_override_1;
    ap.rsn_override_2 = vif.u.ap.rsn_override_2;
    ow_conf_rsno_fix_wpa_with_state(&vif.u.ap.wpa, &ap);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
}

OSW_UT(ow_conf_rsno_wpa3_transition_with_sae_ext)
{
    const struct osw_wpa orig = {
        .wpa = false,
        .rsn = true,
        .akm_psk = true,
        .akm_sae = true,
        .akm_sae_ext = true,
        .pairwise_ccmp = true,
        .pairwise_gcmp256 = true,
        .pmf = OSW_PMF_OPTIONAL,
    };
    struct osw_conf_vif vif = {
        .u.ap =
                {
                    .channel =
                            {
                                .control_freq_mhz = 2412,
                            },
                },
    };
    struct osw_drv_vif_state_ap ap = {};

    /* WPA3 Transition for Wi-Fi 7 (PSK+SAE+SAE-EXT) should yield no changes to
     * WPA configuration if mode is disabled */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_DISABLED);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    /* WPA3 Compatibility Transition should strip SAE-EXT from RSNE and move it
     * to RSNO2. RSNO1 should be left unused. RSNE should maintain SAE (+ PSK).
     *
     * It should leave PMF in RSNE untouched.
     */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(vif.u.ap.wpa.wpa == 0);
    OSW_UT_EVAL(vif.u.ap.wpa.rsn == 1);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_psk == true);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_sae == true);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_sae_ext == false);
    OSW_UT_EVAL(vif.u.ap.wpa.pairwise_ccmp == true);
    OSW_UT_EVAL(vif.u.ap.wpa.pairwise_gcmp256 == false);
    OSW_UT_EVAL(vif.u.ap.wpa.pmf == orig.pmf);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == true);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.akm == (1 << OSW_AKM_RSN_SAE_EXT));
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.pairwise == (1 << OSW_CIPHER_RSN_GCMP_256));
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.pmf == OSW_PMF_REQUIRED);
    OSW_UT_EVAL(vif.u.ap.rsn_override_omit_rsnxe == false);

    ap.rsn_override_1 = vif.u.ap.rsn_override_1;
    ap.rsn_override_2 = vif.u.ap.rsn_override_2;
    ow_conf_rsno_fix_wpa_with_state(&vif.u.ap.wpa, &ap);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);

    /* WPA3 Compatibility (PSK only) should strip SAE from WPA configuration
     * and move it to RSNO1 and SAE-EXT to RSNO2 */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(vif.u.ap.wpa.wpa == 0);
    OSW_UT_EVAL(vif.u.ap.wpa.rsn == 1);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_psk == true);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_sae == false);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_sae_ext == false);
    OSW_UT_EVAL(vif.u.ap.wpa.pairwise_ccmp == true);
    OSW_UT_EVAL(vif.u.ap.wpa.pairwise_gcmp256 == false);
    OSW_UT_EVAL(vif.u.ap.wpa.pmf == OSW_PMF_DISABLED);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == true);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.akm == (1 << OSW_AKM_RSN_SAE));
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.pairwise == ((1 << OSW_CIPHER_RSN_CCMP_128) | (1 << OSW_CIPHER_RSN_GCMP_256)));
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.pmf == OSW_PMF_REQUIRED);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == true);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.akm == (1 << OSW_AKM_RSN_SAE_EXT));
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.pairwise == (1 << OSW_CIPHER_RSN_GCMP_256));
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.pmf == OSW_PMF_REQUIRED);
    OSW_UT_EVAL(vif.u.ap.rsn_override_omit_rsnxe == false);

    ap.rsn_override_1 = vif.u.ap.rsn_override_1;
    ap.rsn_override_2 = vif.u.ap.rsn_override_2;
    ow_conf_rsno_fix_wpa_with_state(&vif.u.ap.wpa, &ap);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
}

OSW_UT(ow_conf_rsno_wpa3_transition_on_6ghz)
{
    const struct osw_wpa orig = {
        .wpa = false,
        .rsn = true,
        .akm_psk = false,
        .akm_sae = true,
        .pairwise_ccmp = true,
    };
    struct osw_conf_vif vif = {
        .u.ap =
                {
                    .channel =
                            {
                                .control_freq_mhz = 5935,
                            },
                },
    };

    /* WPA3 Transition (6 GHz) is technically signalled identically to WPA3
     * Personal. It should yield no changes to WPA configuration */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_DISABLED);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);
}

/* Should be identical to ow_conf_rsno_wpa3_transition_with_sae_ext but on 6GHz */
OSW_UT(ow_conf_rsno_wpa3_transition_on_6ghz_with_sae_ext)
{
    const struct osw_wpa orig = {
        .wpa = false,
        .rsn = true,
        .akm_psk = false,
        .akm_sae = true,
        .akm_sae_ext = true,
        .pairwise_ccmp = true,
        .pairwise_gcmp256 = true,
        .pmf = OSW_PMF_REQUIRED,
    };
    struct osw_conf_vif vif = {
        .u.ap =
                {
                    .channel =
                            {
                                .control_freq_mhz = 5935,
                            },
                },
    };
    struct osw_drv_vif_state_ap ap = {};

    /* WPA3 Transition (SAE+SAE-EXT) should yield no changes to WPA
     * configuration */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_DISABLED);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == false);

    /* WPA3 Compatibility and WPA3 Compatibility Transition (SAE+SAE-EXT)
     * should strip SAE-EXT from RSNE and move it to RSNO2. RSNO1 should be
     * left unused. SAE is intended to be in RSNE.
     */

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(vif.u.ap.wpa.wpa == 0);
    OSW_UT_EVAL(vif.u.ap.wpa.rsn == 1);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_psk == false);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_sae == true);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_sae_ext == false);
    OSW_UT_EVAL(vif.u.ap.wpa.pairwise_ccmp == true);
    OSW_UT_EVAL(vif.u.ap.wpa.pairwise_gcmp256 == false);
    OSW_UT_EVAL(vif.u.ap.wpa.pmf == orig.pmf);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == true);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.akm == (1 << OSW_AKM_RSN_SAE_EXT));
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.pairwise == (1 << OSW_CIPHER_RSN_GCMP_256));
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.pmf == OSW_PMF_REQUIRED);
    OSW_UT_EVAL(vif.u.ap.rsn_override_omit_rsnxe == false);

    ap.rsn_override_1 = vif.u.ap.rsn_override_1;
    ap.rsn_override_2 = vif.u.ap.rsn_override_2;
    ow_conf_rsno_fix_wpa_with_state(&vif.u.ap.wpa, &ap);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);

    vif.u.ap.wpa = orig;
    ow_conf_rsno_mutate_vif_ap(&vif, OW_CONF_RSNO_MODE_WPA3_COMPAT);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(vif.u.ap.wpa.wpa == 0);
    OSW_UT_EVAL(vif.u.ap.wpa.rsn == 1);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_psk == false);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_sae == true);
    OSW_UT_EVAL(vif.u.ap.wpa.akm_sae_ext == false);
    OSW_UT_EVAL(vif.u.ap.wpa.pairwise_ccmp == true);
    OSW_UT_EVAL(vif.u.ap.wpa.pairwise_gcmp256 == false);
    OSW_UT_EVAL(vif.u.ap.wpa.pmf == orig.pmf);
    OSW_UT_EVAL(vif.u.ap.rsn_override_1.enabled == false);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.enabled == true);
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.akm == (1 << OSW_AKM_RSN_SAE_EXT));
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.pairwise == (1 << OSW_CIPHER_RSN_GCMP_256));
    OSW_UT_EVAL(vif.u.ap.rsn_override_2.pmf == OSW_PMF_REQUIRED);
    OSW_UT_EVAL(vif.u.ap.rsn_override_omit_rsnxe == false);

    ap.rsn_override_1 = vif.u.ap.rsn_override_1;
    ap.rsn_override_2 = vif.u.ap.rsn_override_2;
    ow_conf_rsno_fix_wpa_with_state(&vif.u.ap.wpa, &ap);
    ow_conf_rsno_dump_wpa_compare(&orig, &vif.u.ap.wpa);
    OSW_UT_EVAL(memcmp(&orig, &vif.u.ap.wpa, sizeof(orig)) == 0);
}

static bool ow_conf_rsno_mode_get_flag(ff_provider_t *provider, const char *name)
{
    return getenv(name);
}

OSW_UT(ow_conf_rsno_mode_compat)
{
    struct osw_conf_vif vif = {};

    ff_provider_t provider = {
        .name = "test provider",
        .is_flag_enabled_fn = ow_conf_rsno_mode_get_flag,
    };
    ff_provider_register(&provider);

    unsetenv("rsno_disabled");
    unsetenv("rsno_wpa3_compat");
    unsetenv("rsno_wpa3_compat_wifi7_only");
    unsetenv("rsno_wpa3_compat_transition");
    setenv("rsno_wpa3_compat", "", 1);
    OSW_UT_EVAL(ff_is_flag_enabled("rsno_wpa3_compat") == true);

    vif.u.ap.wpa.akm_sae_ext = false;
    OSW_UT_EVAL(ow_conf_rsno_mode_get(&vif) == OW_CONF_RSNO_MODE_WPA3_COMPAT);

    vif.u.ap.wpa.akm_sae_ext = true;
    OSW_UT_EVAL(ow_conf_rsno_mode_get(&vif) == OW_CONF_RSNO_MODE_WPA3_COMPAT);

    unsetenv("rsno_disabled");
    unsetenv("rsno_wpa3_compat");
    unsetenv("rsno_wpa3_compat_wifi7_only");
    unsetenv("rsno_wpa3_compat_transition");
    setenv("rsno_wpa3_compat_wifi7_only", "", 1);
    OSW_UT_EVAL(ff_is_flag_enabled("rsno_wpa3_compat_wifi7_only") == true);

    vif.u.ap.wpa.akm_sae_ext = false;
    OSW_UT_EVAL(ow_conf_rsno_mode_get(&vif) == OW_CONF_RSNO_MODE_DISABLED);

    vif.u.ap.wpa.akm_sae_ext = true;
    OSW_UT_EVAL(ow_conf_rsno_mode_get(&vif) == OW_CONF_RSNO_MODE_WPA3_COMPAT);

    unsetenv("rsno_disabled");
    unsetenv("rsno_wpa3_compat");
    unsetenv("rsno_wpa3_compat_wifi7_only");
    unsetenv("rsno_wpa3_compat_transition");
    setenv("rsno_wpa3_compat_transition", "", 1);
    OSW_UT_EVAL(ff_is_flag_enabled("rsno_wpa3_compat_transition") == true);

    vif.u.ap.wpa.akm_sae_ext = false;
    OSW_UT_EVAL(ow_conf_rsno_mode_get(&vif) == OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);

    vif.u.ap.wpa.akm_sae_ext = true;
    OSW_UT_EVAL(ow_conf_rsno_mode_get(&vif) == OW_CONF_RSNO_MODE_WPA3_COMPAT_TRANSITION);
}
