# Build environment for OpenSync native target
# Base: Ubuntu 16.04 64bit (official image)
FROM ubuntu:16.04

# Run apt-get update at each install step because of docker cache
ARG PKGINSTALL="eval apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install"

# Python
RUN $PKGINSTALL python python3 python3-pip python3-dev python3-pexpect python3-tabulate \
                python3-termcolor python3-paramiko python3-pydot
RUN pip3 install kconfiglib 'MarkupSafe<2.0.0' 'Jinja2<3.0.0'

# Development packages
RUN $PKGINSTALL build-essential gcc g++ gdb lldb-6.0 clang-6.0 flex bison gettext texinfo patch \
                ncurses-dev perl automake autoconf libtool cmake zlib1g-dev liblzo2-dev uuid-dev \
                openssl libssl-dev binutils binutils-gold bzip2 make pkg-config libc6-dev subversion \
                libmosquitto-dev libev4 libev-dev libjansson4 libjansson-dev libncurses5-dev libpcap-dev \
                openvswitch-switch libcurl4-openssl-dev libmxml-dev libmnl-dev libzmq3-dev patchutils \
                sharutils wget unzip rsync git-core gawk dos2unix ccache keychain vim file tree

# Install protobuf-3.14.0
WORKDIR /usr/src
RUN wget https://github.com/protocolbuffers/protobuf/releases/download/v3.14.0/protobuf-cpp-3.14.0.tar.gz
RUN md5sum protobuf-cpp*.tar.gz >> remote-protobuf-cpp.md5
COPY protobuf-cpp.md5 local.md5
RUN diff remote-protobuf-cpp.md5 local.md5
RUN tar xvf protobuf-cpp-3.14.0.tar.gz \
    && cd protobuf-3.14.0 \
    && ./configure --prefix=/usr && make V=s -j $(nproc) && make V=s -j $(nproc) install

# Install protobuf-c-1.3.3
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

# Install netlink library nl-3
RUN $PKGINSTALL libnl-3-dev libnl-route-3-dev

# OVS directory setup
RUN mkdir -p /var/run/openvswitch
RUN chmod 0777 /var/run/openvswitch

# Open interactive bash
CMD bash -i
