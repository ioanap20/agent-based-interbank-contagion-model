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
#include <filesystem>

void run_benchmarking();
void run_seeded_experiment(int numberOfBanks,
                           int numberOfThreads,
                           const std::string& seedsPath,
                           const std::string& resultsPath,
                           bool runSequential);

static std::string default_seeded_results_path(int numberOfBanks, int numberOfThreads){
    return "results/seeded_results_" + std::to_string(numberOfBanks)
         + "_" + std::to_string(numberOfThreads) + ".json";
}

static void print_usage(){
    std::cerr << "Usage:\n"
              << "  ./contagion benchmark\n"
              << "  ./contagion seeded <number_of_banks> <number_of_threads> [seeds.json] [results.json] [--parallel-only]\n"
              << "  ./contagion demo-random\n"
              << "  ./contagion demo-extreme\n";
}

int main(int argc, char* argv[]){
    if(argc > 1 && std::string(argv[1]) == "benchmark"){
        std::cout<<"Running interbank contagion benchmark" << std::endl;

        run_benchmarking();

        std::cout << "Benchmark finished" << std::endl;
    }
    else if(argc > 3 && std::string(argv[1]) == "seeded"){
        int numberOfBanks = std::atoi(argv[2]);
        int numberOfThreads = std::atoi(argv[3]);
        std::string seedsPath = "experiment_seeds.json";
        std::string resultsPath;
        bool runSequential = true;
        int pathCount = 0;

        for(int i = 4; i < argc; ++i){
            std::string arg = argv[i];
            if(arg == "--parallel-only" || arg == "--no-sequential"){
                runSequential = false;
                continue;
            }

            if(pathCount == 0){
                seedsPath = arg;
            }else if(pathCount == 1){
                resultsPath = arg;
            }else{
                std::cerr << "Usage: ./contagion seeded <number_of_banks> <number_of_threads> [seeds.json] [results.json] [--parallel-only]" << std::endl;
                return 1;
            }
            pathCount++;
        }

        if(numberOfBanks <= 0 || numberOfThreads <= 0){
            std::cerr << "Usage: ./contagion seeded <number_of_banks> <number_of_threads> [seeds.json] [results.json] [--parallel-only]" << std::endl;
            return 1;
        }

        if(resultsPath.empty()){
            resultsPath = default_seeded_results_path(numberOfBanks, numberOfThreads);
        }

        if(std::filesystem::exists(resultsPath)){
            std::cout << "Skipping seeded experiment because results already exist: "
                      << resultsPath << std::endl;
            return 0;
        }

        std::filesystem::path resultFile(resultsPath);
        if(resultFile.has_parent_path()){
            std::filesystem::create_directories(resultFile.parent_path());
        }

        std::cout << "Running seeded experiment" << std::endl;
        run_seeded_experiment(numberOfBanks, numberOfThreads, seedsPath, resultsPath, runSequential);
        std::cout << "Seeded experiment finished" << std::endl;
    }
    else if(argc > 1 && std::string(argv[1]) == "demo-random"){
        std::cout << "Running random small market demo" << std::endl;
        run_random_small_market_visualization();
        std::cout << "Demo finished" << std::endl;
    }
    else if(argc == 1 || (argc > 1 && std::string(argv[1]) == "demo-extreme")){
        std::cout << "Running extreme multi-quarter demo" << std::endl;
        run_extreme_small_market_visualization();
        std::cout << "Demo finished" << std::endl;
    }
    else{
        print_usage();
        return 1;
    }

    return 0;
}

/* clang++ -std=c++17 -O3 -march=native -DNDEBUG -pthread \              
main.cpp benchmarking.cpp banks.cpp market.cpp shock.cpp contagion.cpp output.cpp small_market.cpp \
-o contagion*/
