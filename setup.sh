# fetch third-party libraries
apt update -y
apt install -y python3-pip libyaml-dev gfortran-9

pip3 install numpy==1.23.5
pip3 install matplotlib==3.7.2
pip3 install -U scikit-learn==1.2.0
pip3 install scikit-optimize==0.10.1
pip3 install pygments==2.15.1
pip3 install PyYAML==6.0
pip3 install pygad==3.1.0

arch=$(uname -i)
if [[ $arch == x86_64* ]]; then
    cd /tmp
    wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB | gpg --dearmor | tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null
    echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" | tee /etc/apt/sources.list.d/oneAPI.list
    apt update -y
    apt install intel-basekit intel-hpckit -y
elif  [[ $arch == arm* ]]; then
    echo "ARM Architecture"
fi


cd ~/muppet-docker/thirdparty
git clone https://github.com/llnl/faros
cd faros
git checkout e6c9ece1356c93418a04452f98f0f55497f4bf4d
git apply ../faros_patch.txt
cd ../..

cd ~/muppet-docker/extra
git clone https://github.com/ecmwf-ifs/dwarf-p-cloudsc/ ./cloudsc/
cd cloudsc
git checkout 95125c267e5baed113ef7671c9f346979bd84029
git apply ../cloudsc_patch.txt
rm -r arch cloudsc-dwarf serialbox bundle.yml
cd ../..

cd extra
sh setup_rodinia.sh
cd ..