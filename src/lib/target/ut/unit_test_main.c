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

#include "header.h"
#include "unit_test_utils.h"

char *test_name = "test_target";

void run_unit_test()
{
    RUN_TEST( test_target_device_info );
    RUN_TEST( test_target_is_interface_ready );
    RUN_TEST( test_target_device_connectivity_check );
    RUN_TEST( test_target_map_ifname );
    RUN_TEST( test_target_map_ifname_exists );
    RUN_TEST( test_target_stats_device_get );
    RUN_TEST( test_target_map_init );
    RUN_TEST( test_target_map_insert );
    RUN_TEST( test_target_tools_dir );
    RUN_TEST( test_target_bin_dir );
    RUN_TEST( test_target_scripts_dir );
    RUN_TEST( test_target_persistent_storage_dir );
    RUN_TEST( test_target_ethclient_iflist_get );
    RUN_TEST( test_target_stats_device_temp_get );
    RUN_TEST( test_target_device_execute );
    RUN_TEST( test_target_device_wdt_ping );
    /*It can be commented because, it will restart all the managers every time*/
    //RUN_TEST( test_target_device_restart_managers );

}//END OF FUNCTION

int main(void)
{
    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, NULL, NULL);

    run_unit_test();

    return ut_fini();
}//END of main function
