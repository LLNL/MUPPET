#!/usr/bin/env python3
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
import argparse
import check
import skopt
import yaml
import numpy
import statistics
from os.path import expanduser
try:
    from yaml import CLoader as Loader, CDumper as Dumper
except ImportError:
    from yaml import Loader, Dumper
warnings.filterwarnings('ignore', message='The objective has been evaluated at this point before.')

run_idx = 0
shuffle_list = []
print_threshold = 60

plugin_time = 0.0
run_time = 0.0

total_tests = 100

def randomize_mutations(numMutations):
    global shuffle_list
    shuffle_list = [x for x in range(numMutations)]
    random.shuffle(shuffle_list)

def get_mutation(index):
    return shuffle_list[index]

def print(*args, **kwargs):
    if not 'file' in kwargs:
        if not 'print_time' in kwargs or kwargs['print_time']:
            now = datetime.now()
            __builtins__.print('[' + now.strftime("%H:%M:%S") + ']', end='')
    kwargs.pop('print_time', None)
    return  __builtins__.print(*args, **kwargs)

def faros_gather_results(args):
    ret = []
    filename = "./results/results-" + args.program + ".yaml"
    if os.path.isfile(filename):
        with open("./results/results-" + args.program + ".yaml", 'r') as f:
            results = yaml.load(f, Loader=Loader)
            config_text = 'omp'
            if args.intel:
                config_text = 'omp_intel'
            for result in results[args.program][config_text]:
                if math.isinf(float(result)):
                    ret.append(9999.0)
                else:
                    ret.append(float(result))
        return ret            
    return [9999.0]

# Note it relies on some __main__ variables
def test_routine(switches, force=False):
    global run_idx, plugin_time, run_time
    if args.shuffle:
        switches_shuffled = [0] * len(switches)
        for idx, x in enumerate(switches):
            switches_shuffled[get_mutation(idx)] = x
        switches = switches_shuffled
        
    switches_str = ''.join([str(x) for x in switches])
    if run_idx > total_tests:
        return [9999.0]
    if len(switches_str) > print_threshold:
        print(run_idx, "test input:", switches_str[:print_threshold] + "...", end='')
    else:
        print(run_idx, "test input:", switches_str, end='')
    if force == False and switches_str in fitness_dict:
        print(" already run", print_time=False)
        return fitness_dict[switches_str]

    # make store log directory
    os.system("mkdir -p " + homedir + "/muppet-docker/logs/")
    log_dir = homedir + "/muppet-docker/logs/" + str(run_idx)
    os.system("mkdir -p " + log_dir)

    run_idx += 1

    # write config to the log directory
    print(switches_str, file=open(os.path.join(log_dir, "config.txt"), "w"))

    index = 0
    chooseBox = [False, True]
    for mutationInFile in allMutations.values():
        for mutation in mutationInFile["list"]:
            mutation["enabled"] = chooseBox[switches[index]]
            index += 1

    for mutationFilename, mutationFile in allMutations.items():
        fullPath = os.path.join(dir_path, mutationFilename)
        with open(fullPath, "w") as f:
            f.write(json.dumps(mutationFile, indent=4))

    start_time = time.time()
    if "1" in switches_str:
        try:
            mutate_text = subprocess.check_output((base_command + "-m").split(" "), stderr=subprocess.STDOUT, cwd=currentdir).decode("utf-8").strip()
            print(mutate_text, file=open(os.path.join(log_dir, "mutate.txt"), "w"))
        except subprocess.CalledProcessError as e:
            print(e.output.decode(), file=open(os.path.join(log_dir, "mutate.txt"), "w"))
    plugin_time += time.time() - start_time

    # copy source files to logs directory
    start_time = time.time()
    os.system("cp -r repos/" + args.program + "/ " + log_dir)
    try:
        build_text = subprocess.check_output((base_command + "-b").split(" "), stderr=subprocess.STDOUT, cwd=currentdir).decode("utf-8").strip()
        print(build_text, file=open(os.path.join(log_dir, "build.txt"), "w"))
    except subprocess.CalledProcessError as e:
        print(e.output.decode(), file=open(os.path.join(log_dir, "build.txt"), "w"))
        print(" build error", print_time=False)
        start_time = time.time()
        subprocess.run((base_command + "-c").split(" "), cwd=currentdir, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        run_time += time.time() - start_time
        return [9999.0]
    try:
        run_text = subprocess.check_output((base_command + "-r " + run_times).split(" "), stderr=subprocess.STDOUT, cwd=currentdir).decode("utf-8").strip()
        print(run_text, file=open(os.path.join(log_dir, "run.txt"), "w"))
    except subprocess.CalledProcessError as e:
        print(e.output.decode(), file=open(os.path.join(log_dir, "run.txt"), "w"))
        print(" run error")
        start_time = time.time()
        subprocess.run((base_command + "-c").split(" "), cwd=currentdir, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        run_time += time.time() - start_time
        return [9999.0]
    run_time += time.time() - start_time

    # run correctness check
    if check.correctness_check(args.program, homedir + "/muppet-docker/logs/base/run.txt", os.path.join(log_dir, "run.txt")):
        results = faros_gather_results(args)
        if results[0] == 9999.0:
            print(" cannot find time stats", print_time=False)
            start_time = time.time()
            subprocess.run((base_command + "-c").split(" "), cwd=currentdir, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
            run_time += time.time() - start_time
            return results
    else:
        results = faros_gather_results(args)
        stdev = 0
        if len(results) > 1:
            stdev = statistics.stdev(results)
        print(" min:", "{:.6f}".format(min(results)), "max:", "{:.6f}".format(max(results)), "avg:", "{:.6f}".format(sum(results) / len(results)), "std:", "{:.6f}".format(stdev), "result incorrect", print_time=False)        
        start_time = time.time()
        subprocess.run((base_command + "-c").split(" "), cwd=currentdir, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        run_time += time.time() - start_time
        return [9999.0]
            
    start_time = time.time()
    subprocess.run((base_command + "-c").split(" "), cwd=currentdir, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    run_time += time.time() - start_time
    fitness_dict[switches_str] = results   
    if force == False:
        avg_runtime.append(results) 
    stdev = 0
    if len(results) > 1:
        stdev = statistics.stdev(results)
    print(" min:", "{:.6f}".format(min(results)), "max:", "{:.6f}".format(max(results)), "avg:", "{:.6f}".format(sum(results) / len(results)), "std:", "{:.6f}".format(stdev), print_time=False)
    if args.minimum:
        return [min(results)]
    else:
        return results

def fitness_func(solution):
    results = test_routine(solution)
    fitness = sum(results) / len(results)
    return fitness

def func_callback(result):
    if run_idx % 10 == 0:
        result_str = ''.join([str(xx) for xx in result.x])
        result_y = -result.fun

        print("Original runtime : {original}".format(original=initial_time))
        print("Parameters of the best solution : {solution}".format(solution=result_str))
        print("Runtime of the best solution = {solution_fitness}".format(solution_fitness=-result_y))
        print("Speedup: {speedup}".format(speedup=-(initial_time + result_y)/result_y))


if __name__ == '__main__':
    # set affinity to core 0 - 1
    affinity_mask = {0, 1} 
    os.sched_setaffinity(0, affinity_mask) 
    homedir = expanduser("~")
        
    driverPath = os.path.dirname(os.path.realpath(__file__))
    currentdir = os.getcwd()

    randomSeed = int(datetime.now().timestamp()) % 300000
    random.seed(randomSeed)
    shuffle_list = []

    parser = argparse.ArgumentParser(
        description='OpenMP performance oriented mutation testing.')
    parser.add_argument('-p', '--program', dest='program',
                        type=str, help='set the program to be tested')    
    parser.add_argument('-r', '--repeat', dest='repeat', nargs='?', const=1,
                        type=int, help='run <repetitions> repetitions for each test')
    parser.add_argument('-t', '--times', dest='times',
                        type=int, help='mutation <times> times')
    parser.add_argument('-c', '--clean', dest='clean', action='store_true',
                        help='clean the repo')
    parser.add_argument('-f', '--reference', dest='ref', action='store_true',
                        help='reference run')
    parser.add_argument('-s', '--shuffle', dest='shuffle', action='store_true',
                        help='shuffle mutations')
    parser.add_argument('-m', '--minimum', dest='minimum', action='store_true',
                        help='use the minimum run time for result of each run')
    parser.add_argument('-a', '--all', dest='all', action='store_true',
                        help='debug option: set all mutations to true')
    parser.add_argument('-i', '--intel', dest='intel', action='store_true',
                        help='use intel compiler instead of clang')
    parser.add_argument('-cp', '--compare', dest='compare', 
                        type=str, help='compare between original and input config')
        
    args = parser.parse_args()

    print("random seed:", randomSeed)

    run_times = "1"
    if args.repeat:
        run_times = str(args.repeat)
    elif args.ref:
        run_times = str(10)

    avg_runtime = []

    # pull code
    currentdir = os.path.join(os.getcwd(), "thirdparty/faros")
    os.chdir(os.path.join(os.getcwd(), "thirdparty/faros"))
    os.environ["PLUGIN_RUN_ROOT"] = os.path.join(currentdir, "repos", args.program)
    base_command = "python3 faros-config.py -p " + args.program
    if args.intel:
        base_command += " -i "
    else:
        base_command += " "
    os.system("rm -r " + homedir + "/muppet-docker/logs/")

    os.system(base_command + "-f")

    if args.clean:
        os.system(base_command + "-e")
        exit(0)

    os.system(base_command + "-c")

    os.system("mkdir -p " + homedir + "/muppet-docker/logs/")
    log_dir = homedir + "/muppet-docker/logs/base"
    os.system("mkdir -p " + log_dir)

    start_time = time.time()

    if args.all == False or int(run_times) > 1:
        try:
            build_text = subprocess.check_output((base_command + "-b").split(" "), stderr=subprocess.STDOUT, cwd=currentdir).decode("utf-8").strip()
            print(build_text, file=open(os.path.join(log_dir, "build.txt"), "w"))
        except subprocess.CalledProcessError as e:
            print(e.output.decode(), file=open(os.path.join(log_dir, "build.txt"), "w"))
            raise

        try:
            run_text = subprocess.check_output((base_command + "-r " + run_times).split(" "), stderr=subprocess.STDOUT, cwd=currentdir).decode("utf-8").strip()
            print(run_text, file=open(os.path.join(log_dir, "run.txt"), "w"))
        except subprocess.CalledProcessError as e:
            print(e.output.decode(), file=open(os.path.join(log_dir, "run.txt"), "w"))
            raise

        run_time += time.time() - start_time

        results = faros_gather_results(args)
        avg_runtime.append(results)
        if args.minimum:
            best_time = min(results)
        else:
            best_time = sum(results) / len(results)
        initial_time = best_time

        stdev = 0
        if len(results) > 1:
            stdev = statistics.stdev(results)
        print("==== reference min:", min(results), "max:", max(results), "avg:", sum(results) / len(results), "std:", stdev)
        if args.ref:
            exit(0)

    stime = time.time()
    os.system(base_command + "-c")
    run_time += time.time() - stime

    # run func_analysis
    stime = time.time()
    try:
        analysis_text = subprocess.check_output((base_command + "-a").split(" "), stderr=subprocess.STDOUT, cwd=currentdir).decode("utf-8").strip()
        print(analysis_text, file=open(os.path.join(log_dir, "analysis.txt"), "w"))
    except subprocess.CalledProcessError as e:
        print(e.output.decode(), file=open(os.path.join(log_dir, "analysis.txt"), "w"))
    plugin_time += time.time() - stime
        
    dir_path = "./repos/" + args.program + "/workspace/func_analysis"

    removedMutations = set()
    removedFile = os.path.join(driverPath, "patches", args.program, "removed.json")
    if os.path.isfile(removedFile):
        with open(removedFile) as f:
            content = json.load(f)
            for item in content["list"]:
                removedMutations.add(item["id_str"])

    allMutations = {}
    numMutations = 0
    numTypeMutations = [0] * 10
    for path in os.listdir(dir_path):
        # check if current path is a file
        if os.path.isfile(os.path.join(dir_path, path)) and ".json" in path:
            print("reading", os.path.join(dir_path, path))
            with open(os.path.join(dir_path, path)) as f:
                content = json.load(f)
                if content:
                    selected_content = []
                    for item in content["list"]:
                        if not item["id_str"] in removedMutations and item["type"] < 8:
                            selected_content.append(item)
                    content["list"] = selected_content
                    allMutations[path] = content                
                    numMutations += len(content["list"])

    for mutationInFile in allMutations.values():
        for mutation in mutationInFile["list"]:
            numTypeMutations[mutation["type"]] += 1

    print("total mutations:", numMutations)    
    print("type of mutations:", numTypeMutations)
    
    print("removed set:", removedMutations)

    if args.shuffle:
        randomize_mutations(numMutations)

    numRepeats = 2 * numMutations
    if args.times:
        numRepeats = args.times

    fitness_dict = {}

    # if all is selected, perform all mutations at once, then exit
    if args.all:
        test_routine([1] * numMutations, True)
        exit(0)

    if args.compare:
        input = []
        for c in args.compare:
            input.append(int(c))
        test_routine(input, True)
        exit(0)

    # start bayesian optimization

    test_func = fitness_func
    dimensions = [skopt.space.Integer(low=0, high=1, prior='uniform', transform='identity')] * numMutations

    result = skopt.gp_minimize(func=test_func, dimensions=dimensions, n_calls=total_tests, \
                    n_initial_points=10, random_state=randomSeed, callback=func_callback)
    total_time = time.time() - start_time
    print("Time spent on Clang plugins:", plugin_time)
    print("Time spent on running programs:", run_time)
    print("Total time:", total_time)

    result_str = ''.join([str(xx) for xx in result.x])
    result_y = -result.fun

    print("Original runtime : {original}".format(original=initial_time))
    print("Parameters of the best solution : {solution}".format(solution=result_str))
    print("Runtime of the best solution = {solution_fitness}".format(solution_fitness=-result_y))
    print("Speedup: {speedup}".format(speedup=-(initial_time + result_y)/result_y))

    bestTypeMutations = [0] * 10
    muIndex = 0
    for mutationInFile in allMutations.values():
        startTile = False
        startSche = False
        startProc = False
        for mutation in mutationInFile["list"]:
            if mutation["type"] >= 8:
                if mutation["type"] == 8:
                    startSche = False
                if result.x[muIndex] == 1 and startProc == False:
                    startProc = True
                    bestTypeMutations[8] += 1
            elif mutation["type"] >= 6:
                if mutation["type"] == 6:
                    startSche = False
                if result.x[muIndex] == 1 and startSche == False:
                    startSche = True
                    bestTypeMutations[6] += 1
            elif mutation["type"] >= 3:
                if mutation["type"] == 3:
                    startTile = False
                if result.x[muIndex] == 1 and startTile == False:
                    startTile = True
                    bestTypeMutations[3] += 1
            else:
                startTile = False
                if result.x[muIndex] == 1:
                    bestTypeMutations[mutation["type"]] += 1
            muIndex += 1

    print("mutations:", numTypeMutations, bestTypeMutations)
    
    check.printstats("_bo", args.program, avg_runtime, plugin_time, run_time, total_time)

    exit(0)
