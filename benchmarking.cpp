/*
 * benchmarking.cpp
 *
 * Systematic performance experiments for the project.
 *
 * Different parameter regimes 
 *  - sequential and parallel implementations,
 *  - different numbers of threads,
 *  - different numbers of banks,
 *  - different interbank network densities,
 *  - different shock intensities or bank compositions.
 *
 * Records:
 *  - execution time,
 *  - number of defaulted banks,
 *  - size and depth of the contagion cascade,
 *  - speedup obtained from parallelization.
 *
 */
#include "banks.hpp"
#include "market.hpp"
#include "shock.hpp"
#include "contagion.hpp"
#include "output.hpp"

#include <string>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <thread>

struct BenchmarkResult{
    int numberOfBanks;
    int numberOfLoans;
    int numberOfSequentialDefaults;
    int numberOfParallelDefaults;
    int numberOfThreads;
    double shockPercentage;
    double sequentialTimeMs;
    double parallelTimeMs;
    double speedup;
};

static int count_defaulted_banks(const std::vector<Bank>&banks){
    int count = 0;

    for(const Bank& bank : banks){
        if(bank.defaulted){
            count ++;
        }
    }

    return count;
}

static Bank create_random_bank(int id, std::mt19937& gen, bool largeBank){
    Bank bank{};

    bank.id = id;
    bank.defaulted = false;
    bank.receivedLoss = 0.0;

    if(largeBank){
        bank.type = BankType :: Large;
        bank.role = BankRole :: Core;
    } else {
        bank.type = BankType::Small;
        bank.role = BankRole::Peripheral;
    }

    std::uniform_real_distribution<double> assetDistribution(50000.0, 2500000.0);
    std::uniform_real_distribution<double> cashDistribution(5000.0, 250000.0);

    if(largeBank){
        bank.balanceSheet.assets = assetDistribution(gen)*10.0;
        bank.balanceSheet.otherAssets=assetDistribution(gen)*10.0;
        bank.balanceSheet.cash=cashDistribution(gen)*10.0;
    }else{
        bank.balanceSheet.assets = assetDistribution(gen);
        bank.balanceSheet.otherAssets=assetDistribution(gen);
        bank.balanceSheet.cash=cashDistribution(gen);
    }

    double totalAssetsBeforeLiabilities = total_assets(bank);

    if(id % 5 == 0){
        bank.riskType = BankRiskType::Fragile;
        bank.balanceSheet.liabilities = 0.95 * totalAssetsBeforeLiabilities;
    } else {
        bank.riskType = BankRiskType::Robust;
        bank.balanceSheet.liabilities = 0.8 * totalAssetsBeforeLiabilities;
    }


    if(largeBank){
        bank.targetOvernightLendingRatio = 0.05;
        bank.targetOvernightBorrowingRatio = 0.03;

        bank.targetShortTermLendingRatio = 0.03;
        bank.targetShortTermBorrowingRatio = 0.02;

        bank.targetLongTermLendingRatio = 0.02;
        bank.targetLongTermBorrowingRatio = 0.01;
    } else {
        bank.targetOvernightLendingRatio = 0.02;
        bank.targetOvernightBorrowingRatio = 0.02;

        bank.targetShortTermLendingRatio = 0.01;
        bank.targetShortTermBorrowingRatio = 0.015;
        
        bank.targetLongTermLendingRatio = 0.005;
        bank.targetLongTermBorrowingRatio = 0.01;
    }

    update_equity(bank);

    return bank;
}

std::vector<Bank> generate_banks(int numberOfBanks){
    std::vector<Bank> banks;

    banks.reserve(numberOfBanks);

    std::random_device rd;
    std::mt19937 gen(rd());

    int numberOfLargeBanks = std::max(1, numberOfBanks / 20);

    for(int i=0; i<numberOfBanks; i++){
        bool largeBank = i < numberOfLargeBanks;

        Bank bank = create_random_bank(i, gen, largeBank);

        banks.push_back(bank);
    }

    initialize_market_memory(banks);

    return banks;
}

static BenchmarkResult run_one_experiment(int numberOfBanks, double shockPercentage, int numberOfThreads){
    BenchmarkResult result;

    result.numberOfBanks = numberOfBanks;
    result.shockPercentage = shockPercentage;
    result.numberOfThreads = numberOfThreads;

    std::vector<Bank> banks=generate_banks(numberOfBanks);
    std::vector<Loan> loans;
    const int numQuarters=4;

    double totalSeqTime=0.0;
    double totalParTime=0.0;

    int finalSeqDefaults=0;
    int finalParDefaults=0;

    for(int q=0; q<numQuarters; ++q){
        std::vector<Loan> newLoans=build_interbank_market(banks);
        loans.insert(loans.end(), newLoans.begin(), newLoans.end());

        if(q==0){
            apply_random_bank_shock(banks, numberOfBanks/10, shockPercentage);
        }

        std::vector<Bank> sequentialBanks=banks;
        std::vector<Bank> parallelBanks=banks;

        auto startSeq=std::chrono::high_resolution_clock::now();
        run_contagion(sequentialBanks, loans);
        auto endSeq=std::chrono::high_resolution_clock::now();
        totalSeqTime+=std::chrono::duration<double, std::milli>(endSeq-startSeq).count();

        auto startPar=std::chrono::high_resolution_clock::now();
        run_contagion_parallel(parallelBanks, loans, numberOfThreads);
        auto endPar=std::chrono::high_resolution_clock::now();
        totalParTime+=std::chrono::duration<double, std::milli>(endPar-startPar).count();

        if(q==numQuarters-1){
            finalSeqDefaults=count_defaulted_banks(sequentialBanks);
            finalParDefaults=count_defaulted_banks(parallelBanks);
            result.numberOfLoans=static_cast<int>(loans.size());
        }

        banks=sequentialBanks;
        advance_market_time(banks, loans);
    }

    result.sequentialTimeMs=totalSeqTime;
    result.parallelTimeMs=totalParTime;

    result.speedup=result.sequentialTimeMs/result.parallelTimeMs;
    
    result.numberOfSequentialDefaults=finalSeqDefaults;
    result.numberOfParallelDefaults=finalParDefaults;

    return result;
}

void run_benchmarking(){
    std::vector<int> bankNumbers = {5000, 10000, 20000, 50000};
    std::vector<double> shockPercentages = {0.20, 0.40, 0.60, 0.80};
    std::vector<int> threadNumbers = {2, 4, 8, 10};

    print_benchmark_header();

    for(int numberOfBanks : bankNumbers){
        for(double shockPercentage : shockPercentages){
            for(int numberOfThreads : threadNumbers){
                BenchmarkResult result = run_one_experiment(numberOfBanks, shockPercentage, numberOfThreads);

                print_benchmark_row(
                    result.numberOfBanks,
                    result.numberOfLoans,
                    result.shockPercentage,
                    result.numberOfThreads,
                    result.numberOfSequentialDefaults,
                    result.numberOfParallelDefaults,
                    result.sequentialTimeMs,
                    result.parallelTimeMs,
                    result.speedup
                );
            }
        }
    }

    print_separator();
}