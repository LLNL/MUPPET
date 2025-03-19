ifndef $(CC)
CC = clang++
endif

ifndef $(OMP)
OMP = 0
endif

OPT = -O0

ifeq ($(OPT_LEVEL), 0)
OPT = -O0
endif

ifeq ($(OPT_LEVEL), 1)
OPT = -O1
endif

ifeq ($(OPT_LEVEL), 2)
OPT = -O2
endif

ifeq ($(OPT_LEVEL), 3)
OPT = -O3
endif

ifeq ($(FASTMATH), 1)
OPT += -ffast-math
endif

ifeq ($(OMP), 1)
ifeq ($(CC), icpx)
CFLAGS = -qopenmp -fopenmp-version=51
else
CFLAGS = -std=c++17 -fopenmp -fopenmp-version=51 #-fopenmp-targets=nvptx64 -fopenmp-extensions
endif
else
CFLAGS =
endif
LDFLAGS = $(LIBS) -lm

default: main

mk_workspace:
	mkdir -p workspace
	mkdir -p workspace/func_analysis
	mkdir -p workspace/original_files

run:
	./matmul

main: mk_workspace
	$(CC) test.cpp -o matmul $(CFLAGS) $(OPT) $(LDFLAGS) 

clean:
	rm -rf *.o matmul *.out
	rm -rf workspace

func_analysis: mk_workspace
	clang -Xclang -load -Xclang mpopt-plugin.so -Xclang -plugin -Xclang mpopt-function-analysis $(CFLAGS) -c ./test.cpp -emit-llvm

trans_mutations: 
	clang -Xclang -load -Xclang mpopt-plugin.so -Xclang -plugin -Xclang mpopt-trans-mutations $(CFLAGS) -c ./test.cpp -emit-llvm