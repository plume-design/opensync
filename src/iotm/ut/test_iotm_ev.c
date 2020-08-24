#include "test_iotm.h"
#include "iotm_ev.h"

void test_ev_init()
{
    struct iotm_mgr *mgr = iotm_get_mgr();

    mgr->periodic_ts = 0;
    iotm_ev_init();
    TEST_ASSERT_TRUE_MESSAGE(mgr->periodic_ts != 0, "Should have set the periodic timestamp on init");
}



void test_ev_suite() {
    RUN_TEST(test_ev_init);
}
