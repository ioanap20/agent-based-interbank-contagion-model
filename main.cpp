/*
 * main.cpp
 *
 * One complete run of the interbank contagion model.
 *
 * Different stages of the simulation:
 *  1. initialize the bank agents,
 *  2. evaluate which banks have excess liquidity or shortages,
 *  3. form the interbank lending network,
 *  4. apply an external shock,
 *  5. simulate the default contagion cascade,
 *  6. collect and display the final results.
 *
 */

#include "banks.hpp"
#include "market.hpp"
#include "shock.hpp"
#include "contagion.hpp"
#include "small_market.hpp"

#include <iostream>
#include <string>

void run_benchmarking();

int main(int argc, char* argv[]){
    if(argc > 1 && std::string(argv[1]) == "benchmark"){
    std::cout<<"Running interbank contagion benchmark" << std::endl;

    run_benchmarking();

    std::cout << "Benchmark finished" << std::endl;
}
else{
    std::cout << "Running small visual market demo" << std::endl;
    run_small_market_visualization();
    std::cout << "Demo finished" << std::endl;
}

    return 0;
}

// CHECK BRANCH PUSH NADIA