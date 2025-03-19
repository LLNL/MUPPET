wget http://www.cs.virginia.edu/~skadron/lava/Rodinia/Packages/rodinia_3.1.tar.bz2

tar -xvf rodinia_3.1.tar.bz2

rm rodinia_3.1.tar.bz2

rm -r rodinia_3.1/cuda rodinia_3.1/opencl
rm -r rodinia_3.1/openmp/bfs rodinia_3.1/openmp/mummergpu
cp rodinia_3.1/data/b+tree/*.txt rodinia_3.1/openmp/b+tree
cp rodinia_3.1/data/cfd/fvcorr.domn.193K rodinia_3.1/openmp/cfd
cp rodinia_3.1/data/heartwall/test.avi rodinia_3.1/data/heartwall/input.txt rodinia_3.1/openmp/heartwall
cp rodinia_3.1/data/hotspot/*_1024 rodinia_3.1/openmp/hotspot
cp rodinia_3.1/data/hotspot3D/*_512x8 rodinia_3.1/openmp/hotspot3D
rm rodinia_3.1/openmp/kmeans/*
cp rodinia_3.1/openmp/kmeans/kmeans_openmp/* rodinia_3.1/openmp/kmeans/
rm -r rodinia_3.1/openmp/kmeans/kmeans_*
cp rodinia_3.1/data/kmeans/kdd_cup rodinia_3.1/openmp/kmeans/
cp rodinia_3.1/data/leukocyte/testfile.avi rodinia_3.1/openmp/leukocyte
cp rodinia_3.1/data/myocyte/*.txt rodinia_3.1/openmp/myocyte
cp rodinia_3.1/data/nn/*.db rodinia_3.1/data/nn/filelist.txt rodinia_3.1/openmp/nn
rm -r rodinia_3.1/data
rm -r rodinia_3.1/openmp/hotspot3D/output.out

patch -p0 < rodinia_patch.txt