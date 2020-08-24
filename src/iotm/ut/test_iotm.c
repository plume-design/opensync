#include "test_iotm.h"
#include "iotm_session.h"

static struct ev_loop *loop;

// Test callback to override native call to dllopen dllclose
    bool
test_init_plugin(struct iotm_session *session)
{
    if (strcmp(session->dso, "") == 0) return false;

    return true;
}

    void
setUp(void)
{
    struct iotm_mgr *mgr;
    loop = EV_DEFAULT;
    iotm_init_mgr(loop);
    mgr = iotm_get_mgr();
    mgr->init_plugin = test_init_plugin;
}


    void
tearDown(void)
{
    iotm_teardown_mgr();
}




    int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Set the logs to stdout */

    target_log_open("IOTM_TEST", LOG_OPEN_STDOUT_QUIET);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);


	test_iot_rule_ovsdb_suite();
	test_iot_session_suite();
	test_event_suite();
	test_ev_suite();
	test_iotm_rule_suite();
	test_iot_router_suite();
	test_list_suite();
	test_tree_suite();
	test_iotm_plug_event_suite();
	test_iotm_plug_command_suite();
	test_iot_tag_suite();
	test_iot_tl_suite();
    test_data_type_suite();

    return UNITY_END();
}
