FROM ubuntu:20.04

# Run apt-get update at each install step because of docker cache
ARG PKGINSTALL="eval apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install"

# Python3
RUN $PKGINSTALL python3 python3-pip python3-dev python3-pexpect python3-tabulate \
                python3-termcolor python3-paramiko python3-pydot
RUN pip3 install kconfiglib 'MarkupSafe<2.0.0' 'Jinja2<3.0.0'

# Python2 - there is no python2-pip package in Ubuntu 20.04
RUN $PKGINSTALL python2 wget && wget -q https://bootstrap.pypa.io/pip/2.7/get-pip.py && python2 get-pip.py
RUN pip2 install six 'pip==20.3.4' 'requests<2.28' 'certifi<=2021.10.08'

# Development packages
RUN $PKGINSTALL build-essential gcc g++ gdb flex bison gettext texinfo patch \
                libncurses-dev perl automake autoconf libtool cmake zlib1g-dev liblzo2-dev uuid-dev \
                libssl-dev binutils binutils-gold bzip2 make pkg-config libc6-dev subversion \
                libmosquitto-dev libev4 libev-dev libjansson4 libjansson-dev libncurses5-dev libpcap-dev \
                openvswitch-switch libcurl4-openssl-dev libmxml-dev libmnl-dev libzmq3-dev patchutils \
                sharutils wget unzip rsync git gawk dos2unix ccache keychain vim file tree

# Install protobuf-3.14.0 from source
WORKDIR /usr/src
RUN wget https://github.com/protocolbuffers/protobuf/releases/download/v3.14.0/protobuf-cpp-3.14.0.tar.gz
RUN md5sum protobuf-cpp*.tar.gz >> remote-protobuf-cpp.md5
COPY protobuf-cpp.md5 local.md5
RUN diff remote-protobuf-cpp.md5 local.md5
RUN tar xvf protobuf-cpp-3.14.0.tar.gz \
    && cd protobuf-3.14.0 \
    && ./configure --prefix=/usr && make V=s -j $(nproc) && make V=s -j $(nproc) install

# Install protobuf-c-1.3.3 from source
WORKDIR /usr/src
RUN wget https://github.com/protobuf-c/protobuf-c/releases/download/v1.3.3/protobuf-c-1.3.3.tar.gz
RUN md5sum protobuf-c-*.tar.gz >> remote-protobuf-c.md5
COPY protobuf-c.md5 local.md5
RUN diff remote-protobuf-c.md5 local.md5
RUN tar xvf protobuf-c-1.3.3.tar.gz \
    && cd protobuf-c-1.3.3 \
    && ./configure --prefix=/usr && make V=s -j $(nproc) && make V=s -j $(nproc) install

# Install curl-7.68.0 with http/2 support
RUN $PKGINSTALL nghttp2 libnghttp2-dev
RUN wget https://curl.haxx.se/download/curl-7.68.0.tar.gz
RUN md5sum curl-*.tar.gz >> remote-curl.md5
COPY curl.md5 local.md5
RUN diff remote-curl.md5 local.md5
RUN tar xzf curl-7.68.0.tar.gz \
    && cd curl-7.68.0 \
    && ./configure --with-nghttp2 --prefix=/usr/local --with-ssl \
    && make V=s -j $(nproc) \
    && make V=s -j $(nproc) install \
    && ldconfig

WORKDIR /usr/src
RUN git clone https://w1.fi/hostap.git
WORKDIR /usr/src/hostap
RUN eval $PKGINSTALL libnl-3-dev libnl-genl-3-dev libnl-route-3-dev iw
RUN cat tests/hwsim/example-hostapd.config | grep -v TRACE | grep -v SQLITE > hostapd/.config
RUN cat tests/hwsim/example-wpa_supplicant.config | grep -v TRACE | grep -v SQLITE | grep -v DBUS > wpa_supplicant/.config
COPY hostap_base_commit ./
RUN git checkout $(cat hostap_base_commit)
RUN make -C hostapd -j $(nproc)
RUN make -C wpa_supplicant -j $(nproc)
RUN make -C hostapd install
RUN make -C wpa_supplicant install
ENV HOSTAP_SOURCE=/usr/src/hostap

# Install openvswitch-2.8.7 from source
WORKDIR /usr/src
RUN wget https://www.openvswitch.org/releases/openvswitch-2.8.7.tar.gz
RUN md5sum openvswitch-2.8.7.tar.gz >> remote-openvswitch.md5
COPY openvswitch.md5 local.md5
RUN diff remote-openvswitch.md5 local.md5
RUN tar xvf openvswitch-2.8.7.tar.gz && cd openvswitch-2.8.7 \
    && ./boot.sh && ./configure --prefix=/usr --enable-shared && make install
ENV OVS_SOURCE=/usr/src/openvswitch-2.8.7
RUN mkdir -p /usr/var/run/openvswitch
RUN chmod 0777 /usr/var/run/openvswitch

# Install miniupnpc 2.2.1 from source (apt-get pulls V. 1.9)
WORKDIR /usr/src
RUN wget -O miniupnpc-2.2.1.tar.gz https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_2_1.tar.gz && tar xf miniupnpc-2.2.1.tar.gz
WORKDIR /usr/src/miniupnp-miniupnpc_2_2_1/miniupnpc
RUN make install

# UBUS
# ubus requires json-c and ubox
RUN $PKGINSTALL libjson-c-dev
# ubox
ARG LIBUBOX=libubox-2022-09-27-ea560134
WORKDIR /usr/src
RUN wget https://sources.openwrt.org/$LIBUBOX.tar.xz
RUN tar xf $LIBUBOX.tar.xz
RUN mkdir $LIBUBOX/build
WORKDIR /usr/src/$LIBUBOX/build
RUN cmake \
        -D BUILD_LUA=OFF \
        -D CMAKE_BUILD_TYPE=Debug \
        -D CMAKE_FIND_ROOT_PATH=/usr \
        -D CMAKE_INSTALL_PREFIX=/usr \
        -D CMAKE_C_FLAGS="-DJSONC -I/usr/include/json-c" \
        ..
RUN make install
# ubus
ARG LIBUBUS=ubus-2022-06-15-9913aa61
WORKDIR /usr/src
RUN wget https://sources.openwrt.org/$LIBUBUS.tar.xz
RUN tar xf $LIBUBUS.tar.xz
RUN mkdir $LIBUBUS/build
WORKDIR /usr/src/$LIBUBUS/build
RUN cmake \
        -D BUILD_LUA=OFF \
        -D CMAKE_BUILD_TYPE=Debug \
        -D CMAKE_FIND_ROOT_PATH=/usr \
        -D CMAKE_INSTALL_PREFIX=/usr \
        -D CMAKE_C_FLAGS="-DJSONC -I/usr/include/json-c -fno-omit-frame-pointer" \
        ..
RUN make install

# RBUS
ARG LIBRBUS=rbus-2.0.2
WORKDIR /usr/src
RUN wget https://github.com/rdkcentral/rbus/archive/refs/tags/v2.0.2.tar.gz -O $LIBRBUS.tar.gz
RUN tar xf $LIBRBUS.tar.gz
RUN mkdir $LIBRBUS/build
WORKDIR /usr/src/$LIBRBUS/build
RUN cmake \
        -D CC=$(CC) \
        -D BUILD_FOR_DESKTOP=ON \
        -D CMAKE_BUILD_TYPE=Debug \
        -D CMAKE_FIND_ROOT_PATH=/usr \
        -D CMAKE_INSTALL_PREFIX=/usr \
        ..
RUN make install

# Install clang
ENV LLVM_VERSION=17
RUN $PKGINSTALL lsb-release wget software-properties-common apt-utils
COPY llvm.sh llvm.sh
RUN ./llvm.sh ${LLVM_VERSION}
RUN $PKGINSTALL clang-${LLVM_VERSION}
RUN $PKGINSTALL clang-tidy-${LLVM_VERSION} bear jq
RUN $PKGINSTALL clang-format-${LLVM_VERSION}

# libcares
RUN $PKGINSTALL libc-ares-dev

# Open interactive bash
CMD bash -i
