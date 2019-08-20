OpenSync™ Release Notes
-----------------------


### Release 1.2.4.0

* Notable Enhancements
    - Extended `hello_world` manager/service with additional examples
    - Native build (using Kconfig) for faster development of new features
* Notable Fixes
    - Fixed incorrectly reported device capabilities (GW mode)
    - Fixed incorrect parsing of `pref_5g` field when not populated
    - Added reporting of a `CONNECT` event when a client is '11v-band-steered'
      from 2.4 GHz to 5 GHz on the same AP


### Release 1.2.3.0

* New Features
    - Introduces a new manager (XM, or *Exchange Manager*), which facilitates
      data transfer between OVSDB and other management systems.
      Stub code is provided, where communication with an external database can
      be implemented.  
      (see: https://github.com/plume-design/opensync/blob/osync_1.2.3/src/lib/connector/src/connector_stub.c)
* Notable Enhancements
    - Added random delay debounce (to avoid congestion)
* Notable Fixes
    - Fixed memory leaks in `ovs_mac_learn.c`


### Release 1.2.2.0

* New Features
    - Introduces a new manager (FSM, or *Flow Service Manager*), which provides
      processing of events, based on `libpcap` filtering through an extendable
      architecture using plugins.  
      (additional information: https://www.opensync.io/s/FSM-Plugin-API.pdf)
* Notable Enhancements
    - Added a demo service `hello_world` as a template for 3rd-party developers
    - Added the ability to have multiple GRE implementations
    - Enhanced low-level statistics (CPU, memory, filesystem)
    - Asynchronous DNS resolution using `c-ares`
* Notable Fixes
    - Fixed uninitialized memory access in `ovsdb_sync_api.c`


### Release 1.2.0.0

* First public release
