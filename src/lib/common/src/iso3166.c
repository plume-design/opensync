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

/* libc */
#include <string.h>

/* unit */
#include <iso3166.h>

/* opensync */
#include <log.h>
#include <const.h>

/* https://en.wikipedia.org/wiki/ISO_3166-1, 2023.02.13 */
static struct iso3166_entry g_iso3166_table[] = {
    { .name = "Afghanistan", .alpha2 = "AF", .alpha3 = "AFG", .num = 4, },
    { .name = "Åland Islands", .alpha2 = "AX", .alpha3 = "ALA", .num = 248, },
    { .name = "Albania", .alpha2 = "AL", .alpha3 = "ALB", .num = 8, },
    { .name = "Algeria", .alpha2 = "DZ", .alpha3 = "DZA", .num = 12, },
    { .name = "American Samoa", .alpha2 = "AS", .alpha3 = "ASM", .num = 16, },
    { .name = "Andorra", .alpha2 = "AD", .alpha3 = "AND", .num = 20, },
    { .name = "Angola", .alpha2 = "AO", .alpha3 = "AGO", .num = 24, },
    { .name = "Anguilla", .alpha2 = "AI", .alpha3 = "AIA", .num = 660, },
    { .name = "Antarctica", .alpha2 = "AQ", .alpha3 = "ATA", .num = 10, },
    { .name = "Antigua and Barbuda", .alpha2 = "AG", .alpha3 = "ATG", .num = 28, },
    { .name = "Argentina", .alpha2 = "AR", .alpha3 = "ARG", .num = 32, },
    { .name = "Armenia", .alpha2 = "AM", .alpha3 = "ARM", .num = 51, },
    { .name = "Aruba", .alpha2 = "AW", .alpha3 = "ABW", .num = 533, },
    { .name = "Australia", .alpha2 = "AU", .alpha3 = "AUS", .num = 36, },
    { .name = "Austria", .alpha2 = "AT", .alpha3 = "AUT", .num = 40, },
    { .name = "Azerbaijan", .alpha2 = "AZ", .alpha3 = "AZE", .num = 31, },
    { .name = "Bahamas", .alpha2 = "BS", .alpha3 = "BHS", .num = 44, },
    { .name = "Bahrain", .alpha2 = "BH", .alpha3 = "BHR", .num = 48, },
    { .name = "Bangladesh", .alpha2 = "BD", .alpha3 = "BGD", .num = 50, },
    { .name = "Barbados", .alpha2 = "BB", .alpha3 = "BRB", .num = 52, },
    { .name = "Belarus", .alpha2 = "BY", .alpha3 = "BLR", .num = 112, },
    { .name = "Belgium", .alpha2 = "BE", .alpha3 = "BEL", .num = 56, },
    { .name = "Belize", .alpha2 = "BZ", .alpha3 = "BLZ", .num = 84, },
    { .name = "Benin", .alpha2 = "BJ", .alpha3 = "BEN", .num = 204, },
    { .name = "Bermuda", .alpha2 = "BM", .alpha3 = "BMU", .num = 60, },
    { .name = "Bhutan", .alpha2 = "BT", .alpha3 = "BTN", .num = 64, },
    { .name = "Bolivia (Plurinational State of)", .alpha2 = "BO", .alpha3 = "BOL", .num = 68, },
    { .name = "Bonaire, Sint Eustatius and Saba", .alpha2 = "BQ", .alpha3 = "BES", .num = 535, },
    { .name = "Bosnia and Herzegovina", .alpha2 = "BA", .alpha3 = "BIH", .num = 70, },
    { .name = "Botswana", .alpha2 = "BW", .alpha3 = "BWA", .num = 72, },
    { .name = "Bouvet Island", .alpha2 = "BV", .alpha3 = "BVT", .num = 74, },
    { .name = "Brazil", .alpha2 = "BR", .alpha3 = "BRA", .num = 76, },
    { .name = "British Indian Ocean Territory", .alpha2 = "IO", .alpha3 = "IOT", .num = 86, },
    { .name = "Brunei Darussalam", .alpha2 = "BN", .alpha3 = "BRN", .num = 96, },
    { .name = "Bulgaria", .alpha2 = "BG", .alpha3 = "BGR", .num = 100, },
    { .name = "Burkina Faso", .alpha2 = "BF", .alpha3 = "BFA", .num = 854, },
    { .name = "Burundi", .alpha2 = "BI", .alpha3 = "BDI", .num = 108, },
    { .name = "Cabo Verde", .alpha2 = "CV", .alpha3 = "CPV", .num = 132, },
    { .name = "Cambodia", .alpha2 = "KH", .alpha3 = "KHM", .num = 116, },
    { .name = "Cameroon", .alpha2 = "CM", .alpha3 = "CMR", .num = 120, },
    { .name = "Canada", .alpha2 = "CA", .alpha3 = "CAN", .num = 124, },
    { .name = "Cayman Islands", .alpha2 = "KY", .alpha3 = "CYM", .num = 136, },
    { .name = "Central African Republic", .alpha2 = "CF", .alpha3 = "CAF", .num = 140, },
    { .name = "Chad", .alpha2 = "TD", .alpha3 = "TCD", .num = 148, },
    { .name = "Chile", .alpha2 = "CL", .alpha3 = "CHL", .num = 152, },
    { .name = "China", .alpha2 = "CN", .alpha3 = "CHN", .num = 156, },
    { .name = "Christmas Island", .alpha2 = "CX", .alpha3 = "CXR", .num = 162, },
    { .name = "Cocos (Keeling) Islands", .alpha2 = "CC", .alpha3 = "CCK", .num = 166, },
    { .name = "Colombia", .alpha2 = "CO", .alpha3 = "COL", .num = 170, },
    { .name = "Comoros", .alpha2 = "KM", .alpha3 = "COM", .num = 174, },
    { .name = "Congo", .alpha2 = "CG", .alpha3 = "COG", .num = 178, },
    { .name = "Congo, Democratic Republic of the", .alpha2 = "CD", .alpha3 = "COD", .num = 180, },
    { .name = "Cook Islands", .alpha2 = "CK", .alpha3 = "COK", .num = 184, },
    { .name = "Costa Rica", .alpha2 = "CR", .alpha3 = "CRI", .num = 188, },
    { .name = "Côte d'Ivoire", .alpha2 = "CI", .alpha3 = "CIV", .num = 384, },
    { .name = "Croatia", .alpha2 = "HR", .alpha3 = "HRV", .num = 191, },
    { .name = "Cuba", .alpha2 = "CU", .alpha3 = "CUB", .num = 192, },
    { .name = "Curaçao", .alpha2 = "CW", .alpha3 = "CUW", .num = 531, },
    { .name = "Cyprus", .alpha2 = "CY", .alpha3 = "CYP", .num = 196, },
    { .name = "Czechia", .alpha2 = "CZ", .alpha3 = "CZE", .num = 203, },
    { .name = "Denmark", .alpha2 = "DK", .alpha3 = "DNK", .num = 208, },
    { .name = "Djibouti", .alpha2 = "DJ", .alpha3 = "DJI", .num = 262, },
    { .name = "Dominica", .alpha2 = "DM", .alpha3 = "DMA", .num = 212, },
    { .name = "Dominican Republic", .alpha2 = "DO", .alpha3 = "DOM", .num = 214, },
    { .name = "Ecuador", .alpha2 = "EC", .alpha3 = "ECU", .num = 218, },
    { .name = "Egypt", .alpha2 = "EG", .alpha3 = "EGY", .num = 818, },
    { .name = "El Salvador", .alpha2 = "SV", .alpha3 = "SLV", .num = 222, },
    { .name = "Equatorial Guinea", .alpha2 = "GQ", .alpha3 = "GNQ", .num = 226, },
    { .name = "Eritrea", .alpha2 = "ER", .alpha3 = "ERI", .num = 232, },
    { .name = "Estonia", .alpha2 = "EE", .alpha3 = "EST", .num = 233, },
    { .name = "Eswatini", .alpha2 = "SZ", .alpha3 = "SWZ", .num = 748, },
    { .name = "Ethiopia", .alpha2 = "ET", .alpha3 = "ETH", .num = 231, },
    { .name = "Falkland Islands (Malvinas)", .alpha2 = "FK", .alpha3 = "FLK", .num = 238, },
    { .name = "Faroe Islands", .alpha2 = "FO", .alpha3 = "FRO", .num = 234, },
    { .name = "Fiji", .alpha2 = "FJ", .alpha3 = "FJI", .num = 242, },
    { .name = "Finland", .alpha2 = "FI", .alpha3 = "FIN", .num = 246, },
    { .name = "France", .alpha2 = "FR", .alpha3 = "FRA", .num = 250, },
    { .name = "French Guiana", .alpha2 = "GF", .alpha3 = "GUF", .num = 254, },
    { .name = "French Polynesia", .alpha2 = "PF", .alpha3 = "PYF", .num = 258, },
    { .name = "French Southern Territories", .alpha2 = "TF", .alpha3 = "ATF", .num = 260, },
    { .name = "Gabon", .alpha2 = "GA", .alpha3 = "GAB", .num = 266, },
    { .name = "Gambia", .alpha2 = "GM", .alpha3 = "GMB", .num = 270, },
    { .name = "Georgia", .alpha2 = "GE", .alpha3 = "GEO", .num = 268, },
    { .name = "Germany", .alpha2 = "DE", .alpha3 = "DEU", .num = 276, },
    { .name = "Ghana", .alpha2 = "GH", .alpha3 = "GHA", .num = 288, },
    { .name = "Gibraltar", .alpha2 = "GI", .alpha3 = "GIB", .num = 292, },
    { .name = "Greece", .alpha2 = "GR", .alpha3 = "GRC", .num = 300, },
    { .name = "Greenland", .alpha2 = "GL", .alpha3 = "GRL", .num = 304, },
    { .name = "Grenada", .alpha2 = "GD", .alpha3 = "GRD", .num = 308, },
    { .name = "Guadeloupe", .alpha2 = "GP", .alpha3 = "GLP", .num = 312, },
    { .name = "Guam", .alpha2 = "GU", .alpha3 = "GUM", .num = 316, },
    { .name = "Guatemala", .alpha2 = "GT", .alpha3 = "GTM", .num = 320, },
    { .name = "Guernsey", .alpha2 = "GG", .alpha3 = "GGY", .num = 831, },
    { .name = "Guinea", .alpha2 = "GN", .alpha3 = "GIN", .num = 324, },
    { .name = "Guinea-Bissau", .alpha2 = "GW", .alpha3 = "GNB", .num = 624, },
    { .name = "Guyana", .alpha2 = "GY", .alpha3 = "GUY", .num = 328, },
    { .name = "Haiti", .alpha2 = "HT", .alpha3 = "HTI", .num = 332, },
    { .name = "Heard Island and McDonald Islands", .alpha2 = "HM", .alpha3 = "HMD", .num = 334, },
    { .name = "Holy See", .alpha2 = "VA", .alpha3 = "VAT", .num = 336, },
    { .name = "Honduras", .alpha2 = "HN", .alpha3 = "HND", .num = 340, },
    { .name = "Hong Kong", .alpha2 = "HK", .alpha3 = "HKG", .num = 344, },
    { .name = "Hungary", .alpha2 = "HU", .alpha3 = "HUN", .num = 348, },
    { .name = "Iceland", .alpha2 = "IS", .alpha3 = "ISL", .num = 352, },
    { .name = "India", .alpha2 = "IN", .alpha3 = "IND", .num = 356, },
    { .name = "Indonesia", .alpha2 = "ID", .alpha3 = "IDN", .num = 360, },
    { .name = "Iran (Islamic Republic of)", .alpha2 = "IR", .alpha3 = "IRN", .num = 364, },
    { .name = "Iraq", .alpha2 = "IQ", .alpha3 = "IRQ", .num = 368, },
    { .name = "Ireland", .alpha2 = "IE", .alpha3 = "IRL", .num = 372, },
    { .name = "Isle of Man", .alpha2 = "IM", .alpha3 = "IMN", .num = 833, },
    { .name = "Israel", .alpha2 = "IL", .alpha3 = "ISR", .num = 376, },
    { .name = "Italy", .alpha2 = "IT", .alpha3 = "ITA", .num = 380, },
    { .name = "Jamaica", .alpha2 = "JM", .alpha3 = "JAM", .num = 388, },
    { .name = "Japan", .alpha2 = "JP", .alpha3 = "JPN", .num = 392, },
    { .name = "Jersey", .alpha2 = "JE", .alpha3 = "JEY", .num = 832, },
    { .name = "Jordan", .alpha2 = "JO", .alpha3 = "JOR", .num = 400, },
    { .name = "Kazakhstan", .alpha2 = "KZ", .alpha3 = "KAZ", .num = 398, },
    { .name = "Kenya", .alpha2 = "KE", .alpha3 = "KEN", .num = 404, },
    { .name = "Kiribati", .alpha2 = "KI", .alpha3 = "KIR", .num = 296, },
    { .name = "Korea (Democratic People's Republic of)", .alpha2 = "KP", .alpha3 = "PRK", .num = 408, },
    { .name = "Korea, Republic of", .alpha2 = "KR", .alpha3 = "KOR", .num = 410, },
    { .name = "Kuwait", .alpha2 = "KW", .alpha3 = "KWT", .num = 414, },
    { .name = "Kyrgyzstan", .alpha2 = "KG", .alpha3 = "KGZ", .num = 417, },
    { .name = "Lao People's Democratic Republic", .alpha2 = "LA", .alpha3 = "LAO", .num = 418, },
    { .name = "Latvia", .alpha2 = "LV", .alpha3 = "LVA", .num = 428, },
    { .name = "Lebanon", .alpha2 = "LB", .alpha3 = "LBN", .num = 422, },
    { .name = "Lesotho", .alpha2 = "LS", .alpha3 = "LSO", .num = 426, },
    { .name = "Liberia", .alpha2 = "LR", .alpha3 = "LBR", .num = 430, },
    { .name = "Libya", .alpha2 = "LY", .alpha3 = "LBY", .num = 434, },
    { .name = "Liechtenstein", .alpha2 = "LI", .alpha3 = "LIE", .num = 438, },
    { .name = "Lithuania", .alpha2 = "LT", .alpha3 = "LTU", .num = 440, },
    { .name = "Luxembourg", .alpha2 = "LU", .alpha3 = "LUX", .num = 442, },
    { .name = "Macao", .alpha2 = "MO", .alpha3 = "MAC", .num = 446, },
    { .name = "Madagascar", .alpha2 = "MG", .alpha3 = "MDG", .num = 450, },
    { .name = "Malawi", .alpha2 = "MW", .alpha3 = "MWI", .num = 454, },
    { .name = "Malaysia", .alpha2 = "MY", .alpha3 = "MYS", .num = 458, },
    { .name = "Maldives", .alpha2 = "MV", .alpha3 = "MDV", .num = 462, },
    { .name = "Mali", .alpha2 = "ML", .alpha3 = "MLI", .num = 466, },
    { .name = "Malta", .alpha2 = "MT", .alpha3 = "MLT", .num = 470, },
    { .name = "Marshall Islands", .alpha2 = "MH", .alpha3 = "MHL", .num = 584, },
    { .name = "Martinique", .alpha2 = "MQ", .alpha3 = "MTQ", .num = 474, },
    { .name = "Mauritania", .alpha2 = "MR", .alpha3 = "MRT", .num = 478, },
    { .name = "Mauritius", .alpha2 = "MU", .alpha3 = "MUS", .num = 480, },
    { .name = "Mayotte", .alpha2 = "YT", .alpha3 = "MYT", .num = 175, },
    { .name = "Mexico", .alpha2 = "MX", .alpha3 = "MEX", .num = 484, },
    { .name = "Micronesia (Federated States of)", .alpha2 = "FM", .alpha3 = "FSM", .num = 583, },
    { .name = "Moldova, Republic of", .alpha2 = "MD", .alpha3 = "MDA", .num = 498, },
    { .name = "Monaco", .alpha2 = "MC", .alpha3 = "MCO", .num = 492, },
    { .name = "Mongolia", .alpha2 = "MN", .alpha3 = "MNG", .num = 496, },
    { .name = "Montenegro", .alpha2 = "ME", .alpha3 = "MNE", .num = 499, },
    { .name = "Montserrat", .alpha2 = "MS", .alpha3 = "MSR", .num = 500, },
    { .name = "Morocco", .alpha2 = "MA", .alpha3 = "MAR", .num = 504, },
    { .name = "Mozambique", .alpha2 = "MZ", .alpha3 = "MOZ", .num = 508, },
    { .name = "Myanmar", .alpha2 = "MM", .alpha3 = "MMR", .num = 104, },
    { .name = "Namibia", .alpha2 = "NA", .alpha3 = "NAM", .num = 516, },
    { .name = "Nauru", .alpha2 = "NR", .alpha3 = "NRU", .num = 520, },
    { .name = "Nepal", .alpha2 = "NP", .alpha3 = "NPL", .num = 524, },
    { .name = "Netherlands", .alpha2 = "NL", .alpha3 = "NLD", .num = 528, },
    { .name = "New Caledonia", .alpha2 = "NC", .alpha3 = "NCL", .num = 540, },
    { .name = "New Zealand", .alpha2 = "NZ", .alpha3 = "NZL", .num = 554, },
    { .name = "Nicaragua", .alpha2 = "NI", .alpha3 = "NIC", .num = 558, },
    { .name = "Niger", .alpha2 = "NE", .alpha3 = "NER", .num = 562, },
    { .name = "Nigeria", .alpha2 = "NG", .alpha3 = "NGA", .num = 566, },
    { .name = "Niue", .alpha2 = "NU", .alpha3 = "NIU", .num = 570, },
    { .name = "Norfolk Island", .alpha2 = "NF", .alpha3 = "NFK", .num = 574, },
    { .name = "North Macedonia", .alpha2 = "MK", .alpha3 = "MKD", .num = 807, },
    { .name = "Northern Mariana Islands", .alpha2 = "MP", .alpha3 = "MNP", .num = 580, },
    { .name = "Norway", .alpha2 = "NO", .alpha3 = "NOR", .num = 578, },
    { .name = "Oman", .alpha2 = "OM", .alpha3 = "OMN", .num = 512, },
    { .name = "Pakistan", .alpha2 = "PK", .alpha3 = "PAK", .num = 586, },
    { .name = "Palau", .alpha2 = "PW", .alpha3 = "PLW", .num = 585, },
    { .name = "Palestine, State of", .alpha2 = "PS", .alpha3 = "PSE", .num = 275, },
    { .name = "Panama", .alpha2 = "PA", .alpha3 = "PAN", .num = 591, },
    { .name = "Papua New Guinea", .alpha2 = "PG", .alpha3 = "PNG", .num = 598, },
    { .name = "Paraguay", .alpha2 = "PY", .alpha3 = "PRY", .num = 600, },
    { .name = "Peru", .alpha2 = "PE", .alpha3 = "PER", .num = 604, },
    { .name = "Philippines", .alpha2 = "PH", .alpha3 = "PHL", .num = 608, },
    { .name = "Pitcairn", .alpha2 = "PN", .alpha3 = "PCN", .num = 612, },
    { .name = "Poland", .alpha2 = "PL", .alpha3 = "POL", .num = 616, },
    { .name = "Portugal", .alpha2 = "PT", .alpha3 = "PRT", .num = 620, },
    { .name = "Puerto Rico", .alpha2 = "PR", .alpha3 = "PRI", .num = 630, },
    { .name = "Qatar", .alpha2 = "QA", .alpha3 = "QAT", .num = 634, },
    { .name = "Réunion", .alpha2 = "RE", .alpha3 = "REU", .num = 638, },
    { .name = "Romania", .alpha2 = "RO", .alpha3 = "ROU", .num = 642, },
    { .name = "Russian Federation", .alpha2 = "RU", .alpha3 = "RUS", .num = 643, },
    { .name = "Rwanda", .alpha2 = "RW", .alpha3 = "RWA", .num = 646, },
    { .name = "Saint Barthélemy", .alpha2 = "BL", .alpha3 = "BLM", .num = 652, },
    { .name = "Saint Helena, Ascension and Tristan da Cunha", .alpha2 = "SH", .alpha3 = "SHN", .num = 654, },
    { .name = "Saint Kitts and Nevis", .alpha2 = "KN", .alpha3 = "KNA", .num = 659, },
    { .name = "Saint Lucia", .alpha2 = "LC", .alpha3 = "LCA", .num = 662, },
    { .name = "Saint Martin (French part)", .alpha2 = "MF", .alpha3 = "MAF", .num = 663, },
    { .name = "Saint Pierre and Miquelon", .alpha2 = "PM", .alpha3 = "SPM", .num = 666, },
    { .name = "Saint Vincent and the Grenadines", .alpha2 = "VC", .alpha3 = "VCT", .num = 670, },
    { .name = "Samoa", .alpha2 = "WS", .alpha3 = "WSM", .num = 882, },
    { .name = "San Marino", .alpha2 = "SM", .alpha3 = "SMR", .num = 674, },
    { .name = "Sao Tome and Principe", .alpha2 = "ST", .alpha3 = "STP", .num = 678, },
    { .name = "Saudi Arabia", .alpha2 = "SA", .alpha3 = "SAU", .num = 682, },
    { .name = "Senegal", .alpha2 = "SN", .alpha3 = "SEN", .num = 686, },
    { .name = "Serbia", .alpha2 = "RS", .alpha3 = "SRB", .num = 688, },
    { .name = "Seychelles", .alpha2 = "SC", .alpha3 = "SYC", .num = 690, },
    { .name = "Sierra Leone", .alpha2 = "SL", .alpha3 = "SLE", .num = 694, },
    { .name = "Singapore", .alpha2 = "SG", .alpha3 = "SGP", .num = 702, },
    { .name = "Sint Maarten (Dutch part)", .alpha2 = "SX", .alpha3 = "SXM", .num = 534, },
    { .name = "Slovakia", .alpha2 = "SK", .alpha3 = "SVK", .num = 703, },
    { .name = "Slovenia", .alpha2 = "SI", .alpha3 = "SVN", .num = 705, },
    { .name = "Solomon Islands", .alpha2 = "SB", .alpha3 = "SLB", .num = 90, },
    { .name = "Somalia", .alpha2 = "SO", .alpha3 = "SOM", .num = 706, },
    { .name = "South Africa", .alpha2 = "ZA", .alpha3 = "ZAF", .num = 710, },
    { .name = "South Georgia and the South Sandwich Islands", .alpha2 = "GS", .alpha3 = "SGS", .num = 239, },
    { .name = "South Sudan", .alpha2 = "SS", .alpha3 = "SSD", .num = 728, },
    { .name = "Spain", .alpha2 = "ES", .alpha3 = "ESP", .num = 724, },
    { .name = "Sri Lanka", .alpha2 = "LK", .alpha3 = "LKA", .num = 144, },
    { .name = "Sudan", .alpha2 = "SD", .alpha3 = "SDN", .num = 729, },
    { .name = "Suriname", .alpha2 = "SR", .alpha3 = "SUR", .num = 740, },
    { .name = "Svalbard and Jan Mayen", .alpha2 = "SJ", .alpha3 = "SJM", .num = 744, },
    { .name = "Sweden", .alpha2 = "SE", .alpha3 = "SWE", .num = 752, },
    { .name = "Switzerland", .alpha2 = "CH", .alpha3 = "CHE", .num = 756, },
    { .name = "Syrian Arab Republic", .alpha2 = "SY", .alpha3 = "SYR", .num = 760, },
    { .name = "Taiwan, Province of China", .alpha2 = "TW", .alpha3 = "TWN", .num = 158, },
    { .name = "Tajikistan", .alpha2 = "TJ", .alpha3 = "TJK", .num = 762, },
    { .name = "Tanzania, United Republic of", .alpha2 = "TZ", .alpha3 = "TZA", .num = 834, },
    { .name = "Thailand", .alpha2 = "TH", .alpha3 = "THA", .num = 764, },
    { .name = "Timor-Leste", .alpha2 = "TL", .alpha3 = "TLS", .num = 626, },
    { .name = "Togo", .alpha2 = "TG", .alpha3 = "TGO", .num = 768, },
    { .name = "Tokelau", .alpha2 = "TK", .alpha3 = "TKL", .num = 772, },
    { .name = "Tonga", .alpha2 = "TO", .alpha3 = "TON", .num = 776, },
    { .name = "Trinidad and Tobago", .alpha2 = "TT", .alpha3 = "TTO", .num = 780, },
    { .name = "Tunisia", .alpha2 = "TN", .alpha3 = "TUN", .num = 788, },
    { .name = "Türkiye", .alpha2 = "TR", .alpha3 = "TUR", .num = 792, },
    { .name = "Turkmenistan", .alpha2 = "TM", .alpha3 = "TKM", .num = 795, },
    { .name = "Turks and Caicos Islands", .alpha2 = "TC", .alpha3 = "TCA", .num = 796, },
    { .name = "Tuvalu", .alpha2 = "TV", .alpha3 = "TUV", .num = 798, },
    { .name = "Uganda", .alpha2 = "UG", .alpha3 = "UGA", .num = 800, },
    { .name = "Ukraine", .alpha2 = "UA", .alpha3 = "UKR", .num = 804, },
    { .name = "United Arab Emirates", .alpha2 = "AE", .alpha3 = "ARE", .num = 784, },
    { .name = "United Kingdom of Great Britain and Northern Ireland", .alpha2 = "GB", .alpha3 = "GBR", .num = 826, },
    { .name = "United States Minor Outlying Islands", .alpha2 = "UM", .alpha3 = "UMI", .num = 581, },
    { .name = "United States of America", .alpha2 = "US", .alpha3 = "USA", .num = 840, },
    { .name = "Uruguay", .alpha2 = "UY", .alpha3 = "URY", .num = 858, },
    { .name = "Uzbekistan", .alpha2 = "UZ", .alpha3 = "UZB", .num = 860, },
    { .name = "Vanuatu", .alpha2 = "VU", .alpha3 = "VUT", .num = 548, },
    { .name = "Venezuela (Bolivarian Republic of)", .alpha2 = "VE", .alpha3 = "VEN", .num = 862, },
    { .name = "Viet Nam", .alpha2 = "VN", .alpha3 = "VNM", .num = 704, },
    { .name = "Virgin Islands (British)", .alpha2 = "VG", .alpha3 = "VGB", .num = 92, },
    { .name = "Virgin Islands (U.S.)", .alpha2 = "VI", .alpha3 = "VIR", .num = 850, },
    { .name = "Wallis and Futuna", .alpha2 = "WF", .alpha3 = "WLF", .num = 876, },
    { .name = "Western Sahara", .alpha2 = "EH", .alpha3 = "ESH", .num = 732, },
    { .name = "Yemen", .alpha2 = "YE", .alpha3 = "YEM", .num = 887, },
    { .name = "Zambia", .alpha2 = "ZM", .alpha3 = "ZMB", .num = 894, },
    { .name = "Zimbabwe", .alpha2 = "ZW", .alpha3 = "ZWE", .num = 716, },
};

const struct iso3166_entry *
iso3166_lookup_by_alpha2(const char *alpha2)
{
    if (WARN_ON(strnlen(alpha2, 3) == 3)) return NULL;

    struct iso3166_entry *e;
    size_t i;
    for (i = 0; i < ARRAY_SIZE(g_iso3166_table); i++) {
        e = &g_iso3166_table[i];
        const bool matches = (strncmp(alpha2, e->alpha2, 2) == 0);
        if (matches)
            return e;
    }
    return NULL;
}

const struct iso3166_entry *
iso3166_lookup_by_num(const int num)
{
    struct iso3166_entry *e;
    size_t i;
    for (i = 0; i < ARRAY_SIZE(g_iso3166_table); i++) {
        e = &g_iso3166_table[i];
        if (e->num == num)
            return e;
    }
    return NULL;
}
