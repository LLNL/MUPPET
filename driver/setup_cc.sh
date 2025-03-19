# python script for setup
export CUDA_CC_VERSION=`~/muppet-docker/driver/setup_cc.cpp`

echo "Set CUDA Compute Capability to $CUDA_CC_VERSION."

clang++ -std=c++11 -fopenmp ~/muppet-docker/driver/print_openmp.cpp -o b.out
./b.out

source /opt/intel/oneapi/setvars.sh 

export PATH="/opt/intel/oneapi/compiler/latest/linux/bin/intel64:$PATH"

icpx -std=c++11 -qopenmp ~/muppet-docker/driver/print_openmp.cpp -o b.out
./b.out

rm a.out b.out