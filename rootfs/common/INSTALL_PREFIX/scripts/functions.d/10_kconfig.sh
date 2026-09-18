#!bin/sh

include_kconfig()
{
    once _INCLUDE_KCONFIG || return 0
    source "$INSTALL_PREFIX/etc/kconfig"
}

