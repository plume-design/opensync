/*
* Copyright (c) 2019, Sagemcom.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <sys/wait.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <signal.h>
#include <inttypes.h>
#include <jansson.h>

#include "json_util.h"
#include "evsched.h"
#include "schema.h"
#include "log.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_table.h"
#include "ovsdb_cache.h"
#include "schema.h"
#include "ovsdb_sync.h"
#include "daemon.h"

#include "target.h"
#include "wano_ntp.h"

// Defines
#define MODULE_ID			LOG_MODULE_ID_MAIN
struct ovsdb_table          table_Node_State;
struct ovsdb_table 			table_Time;
static ovsdb_table_t		table_DHCP_Option;

#define LOCAL_LOG_HEADER	"[SGC_TIME]"

enum
{
	TIME_STATE_NOT_INIT			= 0,
	TIME_STATE_INIT_NO_DATA 	= 1,
	TIME_STATE_INIT_DATA		= 2,
	TIME_STATE_NTP_STARTED  	= 3,
	TIME_STATE_NTP_RUN_NEXT 	= 4,
	TIME_STATE_NTP_SET_TIMEZONE	= 5,
	TIME_STATE_NTP_OK       	= 6,
	TIME_STATE_NTP_ERROR    	= 7,
	TIME_STATE_MAX
};


#define NTP_CLIENT "/usr/sbin/ntpdate"
#define NTP_OPT "-b -s -u -p 1"
#define NTP_MODULE "ntp"

#define NTP_SYNCHRO_DIR		"/tmp/ntp"

#define TZ_FILE 			"/tmp/etc/TZ"
#define TZ_DATABASE_DIR 	"/usr/share/zoneinfo"
#define TMP_TZ_RESULT_FILE	"/tmp/ntp/tz_result.txt"

#define TIME_DNS_SERVER_NAME_MAX		128
#define TIME_DNS_IP_ADDR_SIZE			32
#define TIME_DNS_IP_ADDR_MAX_NUMBER		16

#define TIME_DNS_MAX_SERVERS			4
#define TIME_DNS_MAX_IP_ADDR_PER_SERVER 4

typedef struct
{
	char	server_name[TIME_DNS_SERVER_NAME_MAX+4];
	char	ip_address[TIME_DNS_IP_ADDR_SIZE+4];
} time_dns_info_t;

typedef struct
{
	uint32_t			n_entries;
	uint32_t			run_entry;
	time_dns_info_t		dns_info[TIME_DNS_IP_ADDR_MAX_NUMBER];
} dns_ipaddr_t;

typedef struct
{
	uint32_t			time_state;
	struct timespec 	time_start;
	dns_ipaddr_t		time_ipadd;
	struct schema_Node_State time_work;
	struct schema_Node_State server_work;
	struct schema_Node_State tz_work;
} time_data_t;

static ev_timer			time_timer;
static daemon_t			time_deamon;
static time_data_t		time_data;

static void wano_ntp_set_status( const char *status )
{
	struct schema_Node_State 	set;
	json_t 				*where = NULL;
	int 				rc     = 0;

	LOGD(LOCAL_LOG_HEADER" [%s] [%s]", __FUNCTION__, status);

	if( (status == NULL) 											 ||
		(strncmp(status, "disabled",       sizeof("disabled"))       &&
		 strncmp(status, "unsynchronized", sizeof("unsynchronized")) &&
		 strncmp(status, "synchronized",   sizeof("synchronized"))   &&
		 strncmp(status, "error",          sizeof("error"))             ) )
	{
		LOGE(LOCAL_LOG_HEADER" [%s] ERROR: invalid parameter [%s]", __FUNCTION__, status);
		return;
	}

	memset( &set, 0, sizeof(set) );
	set._partial_update = true;
	SCHEMA_SET_STR(set.value, status);

	where = ovsdb_where_multi(
		ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_State, module),NTP_MODULE, OCLM_STR),
		ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_State, key),"status", OCLM_STR),
		NULL);

	if( where == NULL )
	{
		LOGE(LOCAL_LOG_HEADER" [%s] ERROR: where is NULL", __FUNCTION__);
		return;
	}

	rc = ovsdb_table_update_where( &table_Node_State, where, &set );
	if( rc == 1 )
	{
		LOGD(LOCAL_LOG_HEADER" [%s] status is [%s]", __FUNCTION__, status);
	}
	else
	{
		LOGE(LOCAL_LOG_HEADER" [%s] ERROR status: unexpected result [%d]", __FUNCTION__, rc);
	}
}

static bool wano_ntp_get_timezone(struct schema_Node_State* tz_conf)
{

	json_t *where = NULL;
	bool errcode = true;

	if(!tz_conf)
	{
		LOGE(LOCAL_LOG_HEADER"Invalid Parameter");
	}

	where = ovsdb_where_multi(
		ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_State, module),NTP_MODULE, OCLM_STR),
		ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_State, key),"localtimezone", OCLM_STR),
		NULL);

	if( where == NULL )
	{
		LOGE(LOCAL_LOG_HEADER" [%s] ERROR: where is NULL", __FUNCTION__);
		return false;
	}

	errcode = ovsdb_table_select_one_where( &table_Node_State, where, tz_conf);
	if( errcode )
	{
		LOGD(LOCAL_LOG_HEADER" [%s] server value is [%s]", __FUNCTION__, tz_conf->value);
		return errcode;
	}
	else
	{
		LOGE(LOCAL_LOG_HEADER" [%s] ERROR status: unexpected result [%d]", __FUNCTION__, errcode);
		return errcode;
	}
}

/******************************************************************************
 *****************************************************************************/
static uint32_t wano_ntp_set_timezone( struct schema_Node_State* m_record )
{
	uint32_t 		retcode = 0;
	int				errcode;
	size_t 			newSize;
	FILE			*fp;
	char*			c_ptr;
	char*			tz_str;
	char			tzFileFound[128+4];
	char			cmd_buff[128+4];

	errcode = wano_ntp_get_timezone(&time_data.tz_work);

	tz_str = time_data.tz_work.value;

	if( time_data.tz_work.value_present && time_data.tz_work.value_exists )
	{
		if( unlink(TZ_FILE) != 0 )
		{
			LOGD(LOCAL_LOG_HEADER" [%s] ERROR: Failed to delete file [%s]. errno [%d]", __FUNCTION__, TZ_FILE, errno);
		}

		fp = fopen( TZ_FILE, "w" );
		if( fp != NULL )
		{
			fprintf( fp, "%s\n", tz_str );
			fclose( fp );

			snprintf(cmd_buff, 128, "grep -rl %s %s | head -n 1 > %s", tz_str, TZ_DATABASE_DIR, TMP_TZ_RESULT_FILE);
			LOGD(LOCAL_LOG_HEADER" [%s]  exec [%s]", __FUNCTION__, cmd_buff);
			errcode = cmd_log( cmd_buff );
			if( errcode )
			{
				LOGE(LOCAL_LOG_HEADER" [%s] ERROR: EXEC [%s] errcode [%d]", __FUNCTION__, cmd_buff, errcode);
			}

			fp = fopen( TMP_TZ_RESULT_FILE, "r" );
			if( fp != NULL )
			{
				memset( tzFileFound, 0, sizeof(tzFileFound) );
				c_ptr = tzFileFound;
				newSize = 128;
				if( getdelim(&c_ptr, &newSize, '\n', fp) > 0 )
				{
					strtok( tzFileFound, "\n" );
					LOGD(LOCAL_LOG_HEADER" [%s] timezone's file name found: [%s] - size[%zu]", __FUNCTION__, tzFileFound, newSize);

					snprintf(cmd_buff, 128, "rm -f /etc/localtime");
					LOGD(LOCAL_LOG_HEADER" [%s]  exec [%s]", __FUNCTION__, cmd_buff);
					errcode = cmd_log( cmd_buff );
					if( errcode )
					{
						LOGD(LOCAL_LOG_HEADER" [%s] ERROR: EXEC [%s] errcode [%d]", __FUNCTION__, cmd_buff, errcode);
					}

					snprintf(cmd_buff, 128, "ln -sf %s /etc/localtime", tzFileFound);
					LOGD(LOCAL_LOG_HEADER" [%s]  exec [%s]", __FUNCTION__, cmd_buff);
					errcode = cmd_log( cmd_buff );
					if( errcode )
					{
						LOGD(LOCAL_LOG_HEADER" [%s] ERROR: EXEC [%s] errcode [%d]", __FUNCTION__, cmd_buff, errcode);
					}
				}
				fclose(fp);
			}

			tzset();
		}
		else
		{
			retcode = 1;
			LOGE(LOCAL_LOG_HEADER" [%s] Failed opening TZ file [%s]", __FUNCTION__, TZ_FILE);
		}
	}
	else
	{
		LOGI(LOCAL_LOG_HEADER" [%s] ERROR: LTZ not set [%d] [%d]", __FUNCTION__, time_data.tz_work.value_present, time_data.tz_work.value_exists);
	}

	LOGD(LOCAL_LOG_HEADER" [%s] EXIT retocde=[%d]", __FUNCTION__, retcode );

	return( retcode );
}

/******************************************************************************
 *****************************************************************************/
static uint32_t wano_ntp_new( struct schema_Node_State* record )
{
	uint32_t 			retcode = 0;
	struct schema_Node_State*	m_record;

	LOGI(LOCAL_LOG_HEADER" [%s]", __FUNCTION__ );

	m_record = &time_data.time_work;
	if( time_data.time_state == TIME_STATE_INIT_NO_DATA )
	{
		memcpy( m_record, record, sizeof(struct schema_Node_State) );
		time_data.time_state = TIME_STATE_INIT_DATA;
	}
	else
	{
		LOGE(LOCAL_LOG_HEADER" [%s]  Wrong State = [%u]", __FUNCTION__, time_data.time_state);
		retcode = 1;
	}

	return( retcode );
}

/******************************************************************************
 *****************************************************************************/
static uint32_t wano_ntp_del( struct schema_Node_State* record )
{
	uint32_t 			retcode = 0;
	struct schema_Node_State*	m_record;

	LOGD(LOCAL_LOG_HEADER" [%s]", __FUNCTION__ );

	m_record = &time_data.time_work;
	if( memcmp( &m_record->_uuid, &record->_uuid, sizeof(ovs_uuid_t) ) == 0 )
	{
		memset( &time_data, 0, sizeof(time_data_t) );
		time_data.time_state = TIME_STATE_INIT_NO_DATA;
		LOGI(LOCAL_LOG_HEADER" [%s] DEL received. Deleting", __FUNCTION__);
	}
	else
	{
		LOGD(LOCAL_LOG_HEADER" [%s] DELETE Wrong UUID", __FUNCTION__);
		retcode = 1;
	}

	return( retcode );
}

static uint32_t wano_ntp_update( struct schema_Node_State* record )
{
	uint32_t 			retcode = 0;
	struct schema_Node_State*	m_record;

	LOGD(LOCAL_LOG_HEADER" [%s]", __FUNCTION__ );

	m_record   = &time_data.time_work;
	/* Reinit NTP by setting the state to TIME_STATE_INIT_DATA
	 * time_timer_cb function will pick it up from there and reread the servers list
	 */
	if( m_record->value_exists && !strncmp("true", m_record->value, sizeof("true")) )
	{
		time_data.time_state = TIME_STATE_INIT_DATA;
		LOGI(LOCAL_LOG_HEADER" [%s] Update received. Updating", __FUNCTION__);
	}
	else
	{
		LOGD(LOCAL_LOG_HEADER" [%s] No record found", __FUNCTION__);
		retcode = 1;
	}

	return( retcode );
}

void callback_Node_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Node_State *old_rec,
        struct schema_Node_State *conf)
{
	uint32_t errcode = 0;
    if (mon->mon_type == OVSDB_UPDATE_NEW && !strncmp(NTP_MODULE,conf->module,sizeof(NTP_MODULE)) && !strncmp("enable",conf->key,sizeof("enable")))
    {
        LOGD("%s: new node config entry: module %s, key: %s, value: %s",
             __func__, conf->module, conf->key, conf->value);
		errcode = wano_ntp_new(conf);
		if( errcode )
		{
			LOGE(LOCAL_LOG_HEADER" [%s] ERROR: NTP OVSDB event: start NTP failed=[%u]", __FUNCTION__, errcode);
		}
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL && !strncmp(NTP_MODULE,conf->module,sizeof(NTP_MODULE)) && !strncmp("enable",conf->key,sizeof("enable")))
    {
        LOGD("%s: delete node config entry: module %s, key: %s, value: %s",
             __func__, conf->module, conf->key, conf->value);
		errcode = wano_ntp_del(conf);
		if( errcode )
		{
			LOGE(LOCAL_LOG_HEADER" [%s] ERROR: NTP OVSDB event: stop NTP failed=[%u]", __FUNCTION__, errcode);
		}
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY && !strncmp(NTP_MODULE,conf->module,sizeof(NTP_MODULE)))
    {
        LOGD("%s: update node config entry: module %s, key: %s, value: %s",
             __func__, conf->module, conf->key, conf->value);
		if (!strncmp("server", conf->key, sizeof("server")))
		{
			errcode = wano_ntp_update(conf);
			if( errcode )
			{
				LOGE(LOCAL_LOG_HEADER" [%s] ERROR: NTP OVSDB event: modify NTP failed=[%u]", __FUNCTION__, errcode);
			}
		}
    }

}

/******************************************************************************
 *****************************************************************************/
static void wano_ntp_getaddrinfo( char* server_name, dns_ipaddr_t* ptr_dns_addr )
{
	int					ret;
	uint32_t			k;
	struct addrinfo		*res_ptr;
	struct addrinfo		*res_ptr_tmp;
	void 				*ptr = NULL;
	char 				addrstr[INET6_ADDRSTRLEN];
	struct addrinfo 	hints;

	LOGD(LOCAL_LOG_HEADER" [%s] [%s]", __FUNCTION__, server_name);

	res_init();
	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family  = AF_INET;
	hints.ai_flags  |= AI_ADDRCONFIG;

	ret = getaddrinfo( server_name, "ntp", &hints, &res_ptr );
	if( ret == 0 )
	{
		LOGD(LOCAL_LOG_HEADER" [%s] DNS lookup OK [%s]", __FUNCTION__, server_name);
		k = 0;
		res_ptr_tmp = res_ptr;
		while( res_ptr_tmp )
		{
			switch( res_ptr_tmp->ai_family )
			{
				case AF_INET:
					ptr = &((struct sockaddr_in*)res_ptr_tmp->ai_addr)->sin_addr;
					break;
				case AF_INET6:
					ptr = &((struct sockaddr_in6*)res_ptr_tmp->ai_addr)->sin6_addr;
					break;
				default:
					break;
			}

			inet_ntop( res_ptr_tmp->ai_family, ptr, addrstr, INET6_ADDRSTRLEN );

			LOGD(LOCAL_LOG_HEADER" [%s] DNS lookup OK [%s] -> [%u] -> [%s]", __FUNCTION__, server_name, k, addrstr );

			snprintf( ptr_dns_addr->dns_info[ptr_dns_addr->n_entries].server_name, TIME_DNS_SERVER_NAME_MAX, "%s", server_name );
			snprintf( ptr_dns_addr->dns_info[ptr_dns_addr->n_entries].ip_address,  TIME_DNS_IP_ADDR_SIZE,    "%s", addrstr );

			ptr_dns_addr->n_entries++;
			k++;
			if( k >= TIME_DNS_MAX_IP_ADDR_PER_SERVER )
			{
				break;
			}

			res_ptr_tmp = res_ptr_tmp->ai_next;
		}
		freeaddrinfo(res_ptr);
	}
	else
	{
		LOGI(LOCAL_LOG_HEADER" [%s] ERROR: DNS [%s] lookup failed (%s)", __FUNCTION__, server_name, gai_strerror(ret));
	}
}

/******************************************************************************
 *****************************************************************************/
static void wano_ntp_dns_resolve( char* srv_list, dns_ipaddr_t* p_dns_addr )
{
	char *srv = NULL;
	const char delim[2] = ",";

	memset( p_dns_addr, 0, sizeof(dns_ipaddr_t) );

	srv = strtok( srv_list, delim );
	while (srv != NULL)
	{
		wano_ntp_getaddrinfo( srv, p_dns_addr);
		srv=strtok(NULL, ",");
	}

}

/******************************************************************************
 *****************************************************************************/
static bool nm2_time_ntp_atexit( daemon_t *m_deamon )
{
	bool				ret_bool = true;
	struct timespec 	time_end;

	clock_gettime(CLOCK_MONOTONIC, &time_end);

	LOGI(LOCAL_LOG_HEADER" [%s] NTP EXIT [%s] code=[%d] state=[%u]", __FUNCTION__, m_deamon->dn_exec, m_deamon->dn_exist_status, time_data.time_state );
	LOGI(LOCAL_LOG_HEADER" [%s] NTP EXIT stime s=[%lu] ns=[%lu]", __FUNCTION__, time_data.time_start.tv_sec, time_data.time_start.tv_nsec );
	LOGI(LOCAL_LOG_HEADER" [%s] NTP EXIT etime s=[%lu] ns=[%lu]", __FUNCTION__,             time_end.tv_sec,             time_end.tv_nsec );

	switch( time_data.time_state )
	{
		case TIME_STATE_NOT_INIT:
		case TIME_STATE_INIT_NO_DATA:
		case TIME_STATE_INIT_DATA:
			break;
		case TIME_STATE_NTP_STARTED:
		case TIME_STATE_NTP_RUN_NEXT:
			if( m_deamon->dn_exist_status == 0 )
			{
				time_data.time_state = TIME_STATE_NTP_SET_TIMEZONE;
			}
			else
			{
				time_data.time_state = TIME_STATE_NTP_RUN_NEXT;
			}
			break;
		case TIME_STATE_NTP_SET_TIMEZONE:
		case TIME_STATE_NTP_OK:
		case TIME_STATE_NTP_ERROR:
		default:
			break;
	}

	return( ret_bool );
}

/*****************************************************************************/
/* #define NTP_OPT "-b -s -u -p 1 1.1.1.1"                                   */
/*****************************************************************************/
static uint32_t wano_ntp_run_time( daemon_t* m_deamon, dns_ipaddr_t* p_dns_addr )
{
	bool		ret_bool;
	uint32_t	ret_code = 0;
	char* 		m_argv[7];

	if( p_dns_addr->run_entry < p_dns_addr->n_entries )
	{
		ret_bool = daemon_init( m_deamon, NTP_CLIENT, DAEMON_LOG_ALL );
		if( ret_bool == true )
		{
			ret_bool = daemon_arg_reset( m_deamon );

			if( ret_bool == true )
			{
				m_argv[0] = "-b";
				m_argv[1] = "-s";
				m_argv[2] = "-u";
				m_argv[3] = "-p";
				m_argv[4] = "1";
				m_argv[5] = p_dns_addr->dns_info[p_dns_addr->run_entry].ip_address;
				m_argv[6] = NULL;

				ret_bool = daemon_arg_add_a( m_deamon, m_argv );
				if( ret_bool == true )
				{
					ret_bool = daemon_atexit( m_deamon, nm2_time_ntp_atexit );
					if( ret_bool == true )
					{
						clock_gettime(CLOCK_MONOTONIC, &time_data.time_start);
						ret_bool = daemon_start( m_deamon );
						if( ret_bool == true )
						{
							LOGI(LOCAL_LOG_HEADER" [%s] NTP Started OK entry=[%u]of[%u]: %s %s %s %s %s %s %s",
																			__FUNCTION__,
																			p_dns_addr->run_entry,
																			p_dns_addr->n_entries,
																			m_deamon->dn_exec,
																			m_argv[0],
																			m_argv[1],
																			m_argv[2],
																			m_argv[3],
																			m_argv[4],
																			m_argv[5] );
							p_dns_addr->run_entry++;
						}
						else
						{
							LOGE(LOCAL_LOG_HEADER" [%s] NTP ERROR: NOT Started entry=[%u]of[%u]: %s %s %s %s %s %s %s",
																			__FUNCTION__,
																			p_dns_addr->run_entry,
																			p_dns_addr->n_entries,
																			m_deamon->dn_exec,
																			m_argv[0],
																			m_argv[1],
																			m_argv[2],
																			m_argv[3],
																			m_argv[4],
																			m_argv[5] );
							ret_code = 6;
						}
					}
					else
					{
						ret_code = 5;
					}
				}
				else
				{
					ret_code = 4;
				}
			}
			else
			{
				ret_code = 3;
			}
		}
		else
		{
			ret_code = 2;
		}
	}
	else
	{
		ret_code = 1;
	}

	if( ret_code != 0 )
	{
		LOGE(LOCAL_LOG_HEADER" [%s] ERROR: Exit ret_code = [%u]", __FUNCTION__, ret_code);
	}

	return( ret_code );
}

static bool wano_ntp_get_server(struct schema_Node_State* srv_conf)
{
	json_t *where = NULL;
	bool errcode = true;

	if(!srv_conf)
	{
		LOGE(LOCAL_LOG_HEADER"Invalid Parameter");
	}

	where = ovsdb_where_multi(
		ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_State, module),NTP_MODULE, OCLM_STR),
		ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_State, key),"server", OCLM_STR),
		NULL);

	if( where == NULL )
	{
		LOGE(LOCAL_LOG_HEADER" [%s] ERROR: where is NULL", __FUNCTION__);
		return false;
	}

	errcode = ovsdb_table_select_one_where( &table_Node_State, where, srv_conf);
	if( errcode )
	{
		LOGD(LOCAL_LOG_HEADER" [%s] server value is [%s]", __FUNCTION__, srv_conf->value);
		return errcode;
	}
	else
	{
		LOGE(LOCAL_LOG_HEADER" [%s] ERROR status: unexpected result [%d]", __FUNCTION__, errcode);
		return errcode;
	}
}

/******************************************************************************
 *****************************************************************************/
static void wano_ntp_run( struct schema_Node_State* m_record )
{
	uint32_t		i;
	uint32_t		err_code;
	dns_ipaddr_t*	p_dns_addr;
	char            srv_list[512];

	wano_ntp_get_server(&time_data.server_work);

	if( m_record->value_exists && !strncmp("true",m_record->value,sizeof("true")) )
	{
		if( time_data.server_work.value != NULL )
		{
			snprintf(srv_list,sizeof(srv_list)-1,"%s",time_data.server_work.value);
			srv_list[sizeof(srv_list)-1] = '\0';
			LOGD(LOCAL_LOG_HEADER"srv name is %s", srv_list);

			p_dns_addr = &time_data.time_ipadd;
			wano_ntp_dns_resolve( srv_list, p_dns_addr );
			if( p_dns_addr->n_entries )
			{
				LOGI(LOCAL_LOG_HEADER" [%s] DNS found = [%u];", __FUNCTION__, p_dns_addr->n_entries);

				for( i = 0; i < p_dns_addr->n_entries; i++ )
				{
					LOGI(LOCAL_LOG_HEADER" [%s] DNS found = [%u] srv=[%s] ip=[%s]", __FUNCTION__, i, p_dns_addr->dns_info[i].server_name, p_dns_addr->dns_info[i].ip_address );
				}

				err_code = wano_ntp_run_time( &time_deamon, p_dns_addr );
				if( err_code == 0 )
				{
					time_data.time_state = TIME_STATE_NTP_STARTED;
				}
				else
				{
					wano_ntp_set_status( "error" );
					time_data.time_state = TIME_STATE_NTP_ERROR;
					LOGE(LOCAL_LOG_HEADER" [%s] ERROR: daemon_run err_code=[%u]", __FUNCTION__, err_code );
				}
			}
			else
			{
				LOGI(LOCAL_LOG_HEADER" [%s] ERROR: No DNS found", __FUNCTION__);
				wano_ntp_set_status( "error" );
				time_data.time_state = TIME_STATE_NTP_ERROR;
			}
		}
		else
		{
			wano_ntp_set_status( "disabled" );
			LOGE(LOCAL_LOG_HEADER" [%s] ERROR: Servers present=[%d]  LTZ not set", __FUNCTION__, time_data.server_work.value_present);
		}
	}
	else
	{
		wano_ntp_set_status( "disabled" );
	}

	LOGD(LOCAL_LOG_HEADER" [%s] EXIT state=[%u]", __FUNCTION__, time_data.time_state);
}

/******************************************************************************
 *****************************************************************************/
static void time_timer_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
	int					next_time_out;
	struct schema_Node_State*	m_record;

	LOGD(LOCAL_LOG_HEADER" [%s] Timer Callback state=[%d] revents=[%d]", __FUNCTION__, time_data.time_state, revents);

	m_record   = &time_data.time_work;

	switch( time_data.time_state )
	{
		case TIME_STATE_NOT_INIT:
			next_time_out = 10;
			break;
		case TIME_STATE_INIT_NO_DATA:
			next_time_out = 10;
			break;
		case TIME_STATE_INIT_DATA:
			wano_ntp_set_status( "unsynchronized" );
			//nm2_time_set_currentlocaltime( m_record );
			wano_ntp_run( m_record );
			next_time_out = 5;
			break;
		case TIME_STATE_NTP_STARTED:
			//nm2_time_set_currentlocaltime( m_record );
			next_time_out = 5;
			break;
		case TIME_STATE_NTP_RUN_NEXT:
			//nm2_time_set_currentlocaltime( m_record );
			if( wano_ntp_run_time( &time_deamon, &time_data.time_ipadd ) != 0 )
			{
				time_data.time_state = TIME_STATE_NTP_ERROR;
			}
			next_time_out = 5;
			break;
		case TIME_STATE_NTP_SET_TIMEZONE:
			wano_ntp_set_timezone( m_record );
			//nm2_time_set_currentlocaltime( m_record );
			wano_ntp_set_status( "synchronized" );
			time_data.time_state = TIME_STATE_NTP_OK;
			next_time_out = 10;
			break;
		case TIME_STATE_NTP_OK:
			//nm2_time_set_currentlocaltime( m_record );
			next_time_out = 10;
			break;
		case TIME_STATE_NTP_ERROR:
			wano_ntp_set_status( "unsynchronized" );
			//nm2_time_set_currentlocaltime( m_record );
			wano_ntp_run( m_record );
			next_time_out = 300;
			break;
		default:
			LOGE(LOCAL_LOG_HEADER" [%s] ERROR: Timer Callback Uknown State = [%d]", __FUNCTION__, time_data.time_state);
			next_time_out = 10;
			break;
	}

	ev_timer_init(&time_timer, time_timer_cb, next_time_out, 0);
	ev_timer_start(EV_DEFAULT, &time_timer);
}

/******************************************************************************
 *****************************************************************************/
static bool wano_ntp_set_server(const char* server_list)
{
	bool retcode = true;
	int rc = 0;

	struct schema_Node_State srv_conf;
	memset(&srv_conf, 0, sizeof(struct schema_Node_State));

	// Modify server list in Node_State Table for NTP module
	json_t *where = NULL;
	where = ovsdb_where_multi(
		ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_State, module),NTP_MODULE, OCLM_STR),
		ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_State, key),"server", OCLM_STR),
		NULL);

	if( where == NULL )
	{
		LOGE(LOCAL_LOG_HEADER" [%s] ERROR: where is NULL", __FUNCTION__);
		return false;
	}

	srv_conf._partial_update = true;
	SCHEMA_SET_STR(srv_conf.value, server_list);

	rc = ovsdb_table_update_where( &table_Node_State, where, &srv_conf );
	if( rc == 1 )
	{
		LOGD(LOCAL_LOG_HEADER" [%s] Server List is [%s]", __FUNCTION__, srv_conf.value);
	}
	else
	{
		LOGE(LOCAL_LOG_HEADER" [%s] ERROR status: unexpected result [%d]", __FUNCTION__, rc);
		retcode = false;
	}
	return retcode;
}

/******************************************************************************
 *****************************************************************************/
static bool wano_ntp_add_server_value(struct schema_DHCP_Option *new, char *srv_list)
{
	bool errcode = true;
	char server_url[100], ntp_server_url[500];

	LOGI(LOCAL_LOG_HEADER" [%s]", __FUNCTION__);

	server_url[0] = ntp_server_url[0] = '\0';
	snprintf(ntp_server_url, sizeof(ntp_server_url), "%s", new->value);

	/* DHCP option value could consist of multiple servers
	 * eg:time.google.com tick.uh.edu. Split the string and add each value
	 */
	char *token = strtok(ntp_server_url, " ");
	if (!token)
	{
		snprintf(server_url, sizeof(server_url) - 1, ",%s", ntp_server_url);
		strncat(srv_list, server_url, sizeof(srv_list) - strlen(srv_list) - 1);
	}
	else
	{
		while (token)
		{
			// Add the value to Servers list
			snprintf(server_url, sizeof(server_url) - 1, ",%s", token);
			strncat(srv_list, server_url, sizeof(srv_list) - strlen(srv_list) - 1);
			server_url[0] = '\0';
			token = strtok(NULL, " ");
	    }
	}
	return errcode;
}

/******************************************************************************
 *****************************************************************************/
static bool wano_ntp_delete_server_value(struct schema_DHCP_Option *old, char *srv_list)
{
	bool errcode = true;
	char server_url[200], ntp_server_url[1000];
	server_url[0] = ntp_server_url[0] = '\0';

	LOGI(LOCAL_LOG_HEADER" [%s]", __FUNCTION__);

	snprintf(ntp_server_url, sizeof(ntp_server_url), "%s", old->value);

	/* DHCP option value could consist of multiple servers to be deleted
	 * eg:time.google.com tick.uh.edu. Split the string and delete each value
	 */
	char *token = strtok(ntp_server_url, " ");
	if (!token)
	{
		char *match;
		snprintf(server_url, sizeof(server_url) - 1, ",%s", ntp_server_url);
		size_t len = strlen(server_url);
		if ((match = strstr(srv_list, server_url)))
		{
			memmove(match, match + len, 1 + strlen(match + len));
		}
	}
	else
	{
		while (token)
		{
			char *match;
			snprintf(server_url, sizeof(server_url) - 1, ",%s", token);
			size_t len = strlen(server_url);
			if ((match = strstr(srv_list, server_url)))
			{
				memmove(match, match + len, 1 + strlen(match + len));
			}

			server_url[0] = '\0';
			token = strtok(NULL, " ");
	    }
	}
	return errcode;
}

/******************************************************************************
 *****************************************************************************/
static bool wano_ntp_dhcp_option_update(ovsdb_update_monitor_t *mon, struct schema_DHCP_Option *old, struct schema_DHCP_Option *new)
{
	bool errcode = true;
	char srv_list[1000];
	struct schema_Node_State srv_conf;

	LOGI(LOCAL_LOG_HEADER" [%s] Received action %d", __FUNCTION__, mon->mon_type);

	srv_list[0] = '\0';
	memset(&srv_conf, 0, sizeof(struct schema_Node_State));

	// Get NTP Servers already configured
	errcode = wano_ntp_get_server(&srv_conf);
	snprintf(srv_list, sizeof(srv_list) - 1, "%s", srv_conf.value);

	switch (mon->mon_type)
	{
		case OVSDB_UPDATE_NEW:
			// Add the new values to server list in Node_State table
			LOGI("Add new NTP server values[%s]", new->value);
			errcode = wano_ntp_add_server_value(new, srv_list);
			break;

		case OVSDB_UPDATE_DEL:
			// Delete the old values from server list in Node_State table
			LOGI("Remove old NTP server values[%s]", old->value);
			errcode = wano_ntp_delete_server_value(old, srv_list);
			break;

		case OVSDB_UPDATE_MODIFY:
			// First delete the old values and then add the new values
			LOGI("Update NTP server values old[%s], new[%s]", old->value, new->value);
			errcode = wano_ntp_delete_server_value(old, srv_list);
			if (errcode)
			{
				errcode = wano_ntp_add_server_value(new, srv_list);
			}
			if (!errcode)
			{
				LOGE(LOCAL_LOG_HEADER" [%s] ERROR Unable to update NTP servers list [%s]", __FUNCTION__, new->value);
			}
			break;

		default:
            LOGE("DHCP_Option: Monitor update error.");
            return false;
	}

	// Modify server list in Node_State Table for NTP module
	if (errcode)
	{
		errcode = wano_ntp_set_server(srv_list);
	}
	else
	{
		LOGE(LOCAL_LOG_HEADER" [%s] ERROR Could not add new servers to NTP servers list [%s]", __FUNCTION__, new->value);
	}

	return errcode;
}


/******************************************************************************
 *****************************************************************************/
/*
 * OVSDB monitor update callback for DHCP_Option
 */
void callback_DHCP_Option(
        ovsdb_update_monitor_t *mon,
        struct schema_DHCP_Option *old,
        struct schema_DHCP_Option *new)
{
	if (!mon) {
		LOGE("DHCP_Option OVSDB event: invalid parameters");
		return;
	}
	switch (mon->mon_type)
	{
	    case OVSDB_UPDATE_NEW:
	        /* Insert case for v6 received option 56  , for v4 received option 42  */
			if ( (new->tag == 56 && (0 == strncmp(new->version,"v6",sizeof("v6"))) && (0 == strncmp(new->type,"rx",sizeof("rx")))) ||
				(new->tag == 42 && (0 == strncmp(new->version,"v4",sizeof("v4"))) && (0 == strncmp(new->type,"rx",sizeof("rx"))))) {
				// Check if server url already exists in NTP table and add
				if( !wano_ntp_dhcp_option_update(mon, old, new) )
				{
					LOGE(LOCAL_LOG_HEADER" [%s] ERROR: NTP OVSDB event: add new NTP servers failed", __FUNCTION__);
				}
			}
	        break;

	    case OVSDB_UPDATE_MODIFY:
	        /* Update case */
			if ( (new->tag == 56 && (0 == strncmp(new->version,"v6",sizeof("v6"))) && (0 == strncmp(new->type,"rx",sizeof("rx")))) ||
				(new->tag == 42 && (0 == strncmp(new->version,"v4",sizeof("v4"))) && (0 == strncmp(new->type,"rx",sizeof("rx"))))) {
				// Check if server url already exists in NTP table, delete the old one and add new entries
				if( !wano_ntp_dhcp_option_update(mon, old, new) )
				{
					LOGE(LOCAL_LOG_HEADER" [%s] ERROR: NTP OVSDB event: Update new NTP servers failed", __FUNCTION__);
				}
			}
		    break;

	    case OVSDB_UPDATE_DEL:
			/* Delete case */
			if ( (new->tag == 56 && (0 == strncmp(new->version,"v6",sizeof("v6"))) && (0 == strncmp(new->type,"rx",sizeof("rx")))) ||
				(new->tag == 42 && (0 == strncmp(new->version,"v4",sizeof("v4"))) && (0 == strncmp(new->type,"rx",sizeof("rx"))))) {
				// Check if server url already exists in NTP table and delete
				if( !wano_ntp_dhcp_option_update(mon, old, new) )
				{
					LOGE(LOCAL_LOG_HEADER" [%s] ERROR: NTP OVSDB event: delete NTP servers failed", __FUNCTION__);
				}
			}
			break;

	    default:
	        LOGE("DHCP_Option: Monitor update error.");
	        return;
	}
}

/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/
bool wano_time_table_init( void )
{
	LOGI(LOCAL_LOG_HEADER" [%s] NM3 Time client Table Init", __FUNCTION__ );

	OVSDB_TABLE_INIT_NO_KEY(Node_State);
	OVSDB_TABLE_INIT_NO_KEY(DHCP_Option);

	OVSDB_TABLE_MONITOR(Node_State, false);
	OVSDB_TABLE_MONITOR(DHCP_Option, false);

	memset( &time_data, 0, sizeof(time_data_t) );
	time_data.time_state = TIME_STATE_INIT_NO_DATA;

	if( mkdir(NTP_SYNCHRO_DIR, S_IRUSR|S_IRGRP|S_IROTH|S_IXUSR|S_IXGRP|S_IXOTH) != 0 )
	{
		LOGE(LOCAL_LOG_HEADER" [%s] Failed to create dir %s : %m", __FUNCTION__, NTP_SYNCHRO_DIR);
	}

	ev_timer_init(&time_timer, time_timer_cb, 1, 0);
	ev_timer_start(EV_DEFAULT, &time_timer);

	return( true );
}
