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
 *  6. collect and display the final results../
 *
 */

#include "banks.hpp"
#include "market.hpp"
#include "shock.hpp"
#include "contagion.hpp"
#include "small_market.hpp"

#include <iostream>
#include <string>
#include <cstdlib>

void run_benchmarking();
void run_seeded_experiment(int numberOfBanks,
                           int numberOfThreads,
                           const std::string& seedsPath,
                           const std::string& resultsPath);

int main(int argc, char* argv[]){
    if(argc > 1 && std::string(argv[1]) == "benchmark"){
        std::cout<<"Running interbank contagion benchmark" << std::endl;

        run_benchmarking();

        std::cout << "Benchmark finished" << std::endl;
    }
    else if(argc > 3 && std::string(argv[1]) == "seeded"){
        int numberOfBanks = std::atoi(argv[2]);
        int numberOfThreads = std::atoi(argv[3]);
        std::string seedsPath = argc > 4 ? argv[4] : "experiment_seeds.json";
        std::string resultsPath = argc > 5 ? argv[5] : "seeded_results.json";

        if(numberOfBanks <= 0 || numberOfThreads <= 0){
            std::cerr << "Usage: ./contagion seeded <number_of_banks> <number_of_threads> [seeds.json] [results.json]" << std::endl;
            return 1;
        }

        std::cout << "Running seeded experiment" << std::endl;
        run_seeded_experiment(numberOfBanks, numberOfThreads, seedsPath, resultsPath);
        std::cout << "Seeded experiment finished" << std::endl;
    }
    else if(argc > 1 && std::string(argv[1]) == "demo-random"){
        std::cout << "Running random small market demo" << std::endl;
        run_random_small_market_visualization();
        std::cout << "Demo finished" << std::endl;
    }
    else{
        std::cout << "Running extreme multi-quarter demo" << std::endl;
        run_extreme_small_market_visualization();
        std::cout << "Demo finished" << std::endl;
    }

    return 0;
}

/* clang++ -std=c++17 -O3 -march=native -DNDEBUG -pthread \              
main.cpp benchmarking.cpp banks.cpp market.cpp shock.cpp contagion.cpp output.cpp small_market.cpp \
-o contagion*/
