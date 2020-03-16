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

#ifndef UM_H_INCLUDED
#define UM_H_INCLUDED

/* Cloud error codes definition                                     */
#define UM_ERR_OK          (  0)   /* success                      */

#define UM_STS_FW_DL_START     (10)    /* FW download started                      */
#define UM_STS_FW_DL_END       (11)    /* FW download successfully completed       */
#define UM_STS_FW_WR_START     (20)    /* FW write on alt partition started        */
#define UM_STS_FW_WR_END       (21)    /* FW image write successfully completed    */
#define UM_STS_FW_BC_START     (30)    /* Bootconfig partition update started      */
#define UM_STS_FW_BC_END       (31)    /* Bootconfig partition update completed    */

#define UM_ERR_OK          (  0)   /* success                      */
#define UM_ERR_ARGS        ( -1)   /* Wrong arguments (app error)  */
#define UM_ERR_URL         ( -3)   /* error setting url            */
#define UM_ERR_DL_FW       ( -4)   /* DL of FW image failed        */
#define UM_ERR_DL_MD5      ( -5)   /* DL of *.md5 sum failed       */
#define UM_ERR_MD5_FAIL    ( -6)   /* md5 CS failed or platform    */
#define UM_ERR_IMG_FAIL    ( -7)   /* image check failed           */
#define UM_ERR_FL_ERASE    ( -8)   /* flash erase failed           */
#define UM_ERR_FL_WRITE    ( -9)   /* flash write failed           */
#define UM_ERR_FL_CHECK    (-10)   /* flash verification failed    */
#define UM_ERR_BC_SET      (-11)   /* set new bootconfig failed    */
#define UM_ERR_APPLY       (-12)   /* restarting device failed     */
#define UM_ERR_BC_ERASE    (-14)   /* flash BC erase failed        */
#define UM_ERR_SU_RUN      (-15)   /* safeupdate is running        */
#define UM_ERR_DL_NOFREE   (-16)   /* Not enough free space on dev */
#define UM_ERR_WRONG_PARAM (-17)   /* Wrong flashing parameters    */
#define UM_ERR_INTERNAL    (-18)   /* Internal rerror              */

/* default timeout                      */
#define UM_DEFAULT_DL_TMO  (60 * 20)   /* 20 minutes timeout   */


bool um_ovsdb(void);

#endif /* UM_H_INCLUDED */
