#!/bin/sh

BOOST_LIBRARIES="system date_time filesystem thread chrono"

if [ $# -lt 3 ]; then
  echo "Usage: $0 boost_archive boost_version destination_dir [32|64]" 1>&2
  exit 1
fi

variant=${VARIANT:-debug}
arch=$(${CROSS_PREFIX}g++ -dumpmachine | cut -d '-' -f 1)

BOOST_ARCHIVE=$(realpath $1)
BOOST_VERSION=$2
BOOST_SOURCE=boost_${BOOST_VERSION}
DIST_DIR=$(realpath $3)
address_model=$4

BOOST_BUILD_DIR=$(realpath .)/boost.build
BOOST_STAGE_DIR=$(realpath .)/boost.stage

cores=$(nproc 2> /dev/null || echo 1)

if [ ! -d ${BOOST_SOURCE} ]; then
  echo "[*] Decompressing ${BOOST_ARCHIVE}"
  bzip2 -d ${BOOST_ARCHIVE} -c | tar xv
fi

if [ ! -x ${BOOST_SOURCE}/b2 ]; then
  echo "[*] Bootstrapping Boost"
  (cd ${BOOST_SOURCE} && sh bootstrap.sh)
fi

sed -i.old "s/using gcc \(.*\); $/using gcc : ${arch} : ${CROSS_PREFIX}g++ ; /" ${BOOST_SOURCE}/project-config.jam

echo "[*] Building Boost"
B2_ARGS="--build-dir=${BOOST_BUILD_DIR} --stagedir=${BOOST_STAGE_DIR} -j ${cores}"
for l in ${BOOST_LIBRARIES}; do
  B2_ARGS="${B2_ARGS} --with-${l}"
done
if [ "${address_model}" != "" ]; then
  B2_ARGS="${B2_ARGS} address-model=${address_model}"
fi
B2_ARGS="${B2_ARGS} toolset=gcc-${arch} link=static runtime-link=static variant=${variant} cxxflags=-fno-rtti"

(cd ${BOOST_SOURCE} && ./b2 ${B2_ARGS} --prefix=${DIST_DIR} stage install)
