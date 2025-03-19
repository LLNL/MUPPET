apt-get update -y
apt-get install -y apt-utils
DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get -y install tzdata
apt-get upgrade -y
apt-get install -y python3 \
                    git \
                    wget \
                    build-essential \
                    gcc \
                    make \
                    cmake \
                    gfortran \
                    libboost-all-dev \
                    lsb-release \
                    software-properties-common

git clone https://github.com/llvm/llvm-project/ /root/llvm-project
cd /root/llvm-project
git checkout tags/llvmorg-16.0.6

mkdir build
cd build
cmake -DLLVM_BUILD_EXAMPLES=1 -DCLANG_BUILD_EXAMPLES=1 -DLLVM_ENABLE_PROJECTS="clang;lld;lldb;openmp" -DCMAKE_INSTALL_PREFIX="" -DCMAKE_BUILD_TYPE="Release" -G "Unix Makefiles" ../llvm
make -j4
make install