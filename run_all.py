import os
import sys
import time
from subprocess import Popen, PIPE, CalledProcessError

if __name__ == '__main__':
    benchmarks = []
    benchmarks = benchmarks + ["CoSP2", "backprop", "cfd", "b+tree", \
                    "heartwall", "hotspot", "hotspot3D", "kmeans", \
                    "lavaMD", "leukocyte", "lud", "myocyte", "nn", "nw", \
                    "particlefilter", "pathfinder", "srad", "streamcluster", \
                    "cloudsc", ]
    benchmarks = benchmarks + ["LULESH", "FT", "LU", "MG", "SP", \
                    "CoMD", "hpcg", "BT", "CG", "EP"]
    method = "dd"

    for b in benchmarks:
        os.system("python3 driver/mutate_" + method + ".py -p " + b + " -r 3 -m -n")
        os.system("python3 driver/mutate_" + method + ".py -p " + b + " -c")