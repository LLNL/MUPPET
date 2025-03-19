#include <stdio.h>
#include <omp.h>
#include <chrono>
#include <ctime>
#include <cmath>
#include <iostream>
#include <fstream>

#define ARRAY_SIZE    (2048)
double A[ARRAY_SIZE][ARRAY_SIZE];
double B[ARRAY_SIZE][ARRAY_SIZE];
double C[ARRAY_SIZE][ARRAY_SIZE];

int main(void)
{   
    for (int i = 0; i < ARRAY_SIZE; i++)
    {
        for (int j = 0; j < ARRAY_SIZE; j++)
        {
            A[i][j] = i * 2.3 + j * 0.1;
            B[i][j] = i + j + 4.6;
            C[i][j] = 0.0;
        }
    }

    std::chrono::time_point<std::chrono::system_clock> start, end;
    start = std::chrono::system_clock::now();
    float var = 2.3f;
    #pragma omp parallel for shared(var)    
    for (int i = 0; i < ARRAY_SIZE; ++i)
        for (int j = 0; j < ARRAY_SIZE; ++j)            
            for(int k = 0; k < ARRAY_SIZE; ++k)
            {
                C[i][j] += var * A[i][k] * B[k][j];
            }
    end = std::chrono::system_clock::now();
    std::ifstream inFile;
    inFile.open("afile.dat");
    bool verified = true;
    for (int i = 0; i < ARRAY_SIZE; ++i) {
        for (int j = 0; j < ARRAY_SIZE; ++j) {
            float f;
            inFile >> f;
            if (fabs(f - C[i][j]) / C[i][j] > 1e-5) {
                verified = false;
                break;
            }
        }
        if (!verified) break;
    }
    std::cout << "verify result: " << verified << std::endl;
    inFile.close();
    double total = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    printf("Work consumed %.17g seconds\n", total / 1000000.0);
    return 0;
}