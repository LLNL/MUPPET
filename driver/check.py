from cmath import nan
import os
import math
import subprocess
import time
from posixpath import split
import sys
import shutil
import json
import random
from datetime import datetime
import warnings

def correctness_check(program, base_name, ver_name):
    if not os.path.isfile(base_name) or not os.path.isfile(ver_name):
        return False
    
    with open(base_name, "r") as f:
        base_txt = f.readlines()
    with open(ver_name, "r") as f:
        ver_txt = f.readlines()

    if program == "LULESH":
        base_it = 0
        base_foe = 0.0
        ver_it = 0
        ver_foe = 0.0
        for line in base_txt:
            if "Iteration count" in line:
                base_it = int(line.strip().split("=")[1])
            if "Final Origin Energy" in line:
                base_foe = float(line.strip().split("=")[1])
            
        for line in ver_txt:
            if "Iteration count" in line:
                ver_it = int(line.strip().split("=")[1])
            if "Final Origin Energy" in line:
                ver_foe = float(line.strip().split("=")[1])

        return base_it == ver_it and base_foe == ver_foe
    elif program == "matmul":
        for line in base_txt:
            if "verify result:" in line:
                base_val = float(line.strip().split(":")[1])
        for line in ver_txt:
            if "verify result:" in line:
                ver_val = float(line.strip().split(":")[1])                
        if base_val == ver_val:
            return True
        return False
    elif program in ("BT", "CG", "FT", "LU", "MG", "SP"):
        for line in ver_txt:
            if "verification successful" in line.lower():
                return True
        return False
    elif program == "hpcg":
        for line in ver_txt:
            if "Results are valid" in line:
                return True
        return False
    elif program == "CoMD":
        noAtomsLost = False
        energyRatio = 0
        for line in ver_txt:
            if "no atoms lost" in line:
                noAtomsLost = True
            if "eFinal/eInitial" in line:
                energyRatio = float(line.strip().split(":")[1])
        if noAtomsLost and energyRatio < 1.00001 and energyRatio > 0.99999:
            return True
        else:
            return False
    elif False: #program.startswith('poly-'):
        stage = 0
        base_nums = []
        ver_nums = []
        for line in base_txt:
            if line.startswith("path"):
                stage = 1
            elif stage == 1: # skip time print
                stage = 2
            elif stage == 2:
                if "Time" in line:
                    break
                base_nums.append([float(x) for x in line.strip().split()])
        stage = 0
        for line in ver_txt:
            if line.startswith("path"):
                stage = 1
            elif stage == 1: # skip time print
                stage = 2
            elif stage == 2:
                if "Time" in line:
                    break
                ver_nums.append([float(x) for x in line.strip().split()])     
        if len(base_nums) != len(ver_nums):
            print("not the same size", end="")
            return False
        for i in range(len(base_nums)):
            if len(base_nums[i]) != len(ver_nums[i]):
                print("not the same size", end="")
                return False
            for j in range(len(base_nums[i])):
                bn = base_nums[i][j]
                vn = ver_nums[i][j]
                if bn == 0.0 or vn == 0.0:
                    if abs(bn - vn) > 1e-1:
                        print("error off:", i, j, bn, vn, end="")
                        return False
                else:
                    if abs((bn - vn) / bn) > 1e-2:
                        print("error off:", i, j, bn, vn, end="")
                        return False    
        return True                    
    else:
        return True
    
def time_check(program, base_time, best_time, current_time):
    if base_time == best_time:
        if current_time < best_time:
            return True
        else:
            return False        
    elif current_time < best_time and current_time < base_time * 0.997:
        return True
    else:
        return False
    
def printstats(suffix, program, avg_runtime, plugin_time, run_time, total_time):
    best_min = 9999.0
    best_avg = 9999.0
    best_avg_for_min = 9999.0
    for i in range(len(avg_runtime)):
        if i == 0:
            original_min = min(avg_runtime[0])
            original_avg = sum(avg_runtime[0]) / len(avg_runtime[0])
        else:
            current_min = min(avg_runtime[i])
            current_avg = sum(avg_runtime[i]) / len(avg_runtime[i])
            if current_min < best_min:
                best_min = current_min
                best_avg_for_min = current_avg
            if current_avg < best_avg:
                best_avg = current_avg
    with open("../../results" + suffix + ".csv", "a") as f:
        f.write(str(program) + ',' + str(original_min) + ',' + str(original_avg) + ',' + str(best_min) + ',' + str(best_avg) + ',' + str(best_avg_for_min) \
                + ',' + str((original_min / best_min - 1.0) * 100.0) + '%,' + str((original_avg / best_avg - 1.0)*100.0) + '%,' \
                + str((original_avg / best_avg_for_min - 1.0) * 100.0) \
                + '%,' + str(plugin_time) + ',' + str(run_time) + ',' + str(total_time - plugin_time - run_time) + ',' + str(total_time) + '\n')   
    with open("../../test" + suffix + "/" + program + ".csv", "w") as f:
        min_avg = 9999.0
        for avg in avg_runtime:
            if sum(avg) / len(avg) < min_avg:
                min_avg = sum(avg) / len(avg)
            f.write(str(min_avg) + "\n")
