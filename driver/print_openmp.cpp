#include <unordered_map>
#include <iostream>
#include <omp.h>

int main(int argc, char *argv[])
{
  std::unordered_map<unsigned,std::string> map{
    {199810,"1.0"},
    {200203,"2.0"},
    {200505,"2.5"},
    {200805,"3.0"},
    {201107,"3.1"},
    {201307,"4.0"},
    {201511,"4.5"},
    {201811,"5.0"},
    {202011,"5.1"},
    {202111,"5.2"}
  };
  #if defined(__INTEL_LLVM_COMPILER)
  std::cout << "Intel OpenMP " << map.at(_OPENMP) << ".\n";
  #elif defined(__clang__)
  std::cout << "Clang OpenMP " << map.at(_OPENMP) << ".\n";  
  #elif defined( __GNUC__)
  std::cout << "GCC OpenMP " << map.at(_OPENMP) << ".\n";
  #else
  std::cout << "Unknown OpenMP " << map.at(_OPENMP) << ".\n";
  #endif
  return 0;
}