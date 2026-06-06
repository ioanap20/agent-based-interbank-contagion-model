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
        bank.balanceSheet.liabilities = 0.92 * totalAssetsBeforeLiabilities;
    } else {
        bank.riskType = BankRiskType::Robust;
        bank.balanceSheet.liabilities = 0.78 * totalAssetsBeforeLiabilities;
    }


    if(largeBank){
        bank.targetOvernightLendingRatio = 0.04;
        bank.targetOvernightBorrowingRatio = 0.02;

        bank.targetShortTermLendingRatio = 0.015;
        bank.targetShortTermBorrowingRatio = 0.01;

        bank.targetLongTermLendingRatio = 0.01;
        bank.targetLongTermBorrowingRatio = 0.005;
    } else {
        bank.targetOvernightLendingRatio = 0.01;
        bank.targetOvernightBorrowingRatio = 0.01;

        bank.targetShortTermLendingRatio = 0.005;
        bank.targetShortTermBorrowingRatio = 0.008;
        
        bank.targetLongTermLendingRatio = 0.003;
        bank.targetLongTermBorrowingRatio = 0.005;
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

static void reset_banks_for_contagion(std::vector<Bank>& banks, const std::vector<Bank>& snapshot){
    for(std::size_t i = 0; i < banks.size(); ++i){
        banks[i].defaulted = snapshot[i].defaulted;
        banks[i].receivedLoss = snapshot[i].receivedLoss;
        banks[i].balanceSheet = snapshot[i].balanceSheet;
    }
}

static std::vector<int> benchmark_thread_numbers(){
    unsigned int hardwareThreads = std::thread::hardware_concurrency();
    int maxThreads = hardwareThreads > 0 ? static_cast<int>(hardwareThreads) : 16;
    std::vector<int> candidates = {1, 2, 8, 12, 16, 20, 24, 28, 32};
    std::vector<int> threadNumbers;

    for(int candidate : candidates){
        if(candidate <= maxThreads){
            threadNumbers.push_back(candidate);
        }
    }

    if(threadNumbers.empty() || threadNumbers.back() != maxThreads){
        threadNumbers.push_back(maxThreads);
    }

    return threadNumbers;
}

static void run_one_experiment(const std::vector<Bank>& baseBanks, const std::vector<std::vector<Loan>>& preBuiltLoans, int numberOfBanks, double shockPercentage, const std::vector<int>& threadNumbers){
    std::vector<Bank> banks = baseBanks;
    std::vector<Loan> cumulativeLoans;
    const int numQuarters = 4;

    for(int q = 0; q < numQuarters; ++q){
        const std::vector<Loan>& quarterLoans = preBuiltLoans[q];
        cumulativeLoans.insert(cumulativeLoans.end(), quarterLoans.begin(), quarterLoans.end());

        if(q == 0){
            apply_random_bank_shock(banks, std::max(1, numberOfBanks / 20), shockPercentage);
        }

        advance_market_time(banks, cumulativeLoans);
    }

    const int finalLoanCount = static_cast<int>(cumulativeLoans.size());
    std::vector<Bank> experimentStartBanks = banks;
    const int timing_repeats = 64;

    {
        std::vector<Bank> warmupBanks = experimentStartBanks;
        int maxThreads = *std::max_element(threadNumbers.begin(), threadNumbers.end());
        ParallelContagionPlan warmupPlan(static_cast<int>(warmupBanks.size()), cumulativeLoans, maxThreads);
        run_contagion_parallel_prepared(warmupBanks, cumulativeLoans, warmupPlan);
    }

    std::vector<Bank> sequentialBanks = experimentStartBanks;
    auto sequential_start = std::chrono::high_resolution_clock::now();
    for(int rep = 0; rep < timing_repeats; ++rep){
        reset_banks_for_contagion(sequentialBanks, experimentStartBanks);
        run_contagion(sequentialBanks, cumulativeLoans);
    }
    auto sequential_end = std::chrono::high_resolution_clock::now();
    const double totalSeqTime = std::chrono::duration<double, std::milli>(
        sequential_end - sequential_start
    ).count();

    const int finalSeqDefaults = count_defaulted_banks(sequentialBanks);
    std::vector<int> finalParDefaults(threadNumbers.size(), 0);
    std::vector<double> totalParTimes(threadNumbers.size(), 0.0);

    for(std::size_t i = 0; i < threadNumbers.size(); i++){
        int numberOfThreads = threadNumbers[i];
        std::vector<Bank> parallelBanks = experimentStartBanks;
        ParallelContagionPlan parallelPlan(numberOfBanks, cumulativeLoans, numberOfThreads);

        auto parallel_start = std::chrono::high_resolution_clock::now();
        for(int rep = 0; rep < timing_repeats; ++rep){
            reset_banks_for_contagion(parallelBanks, experimentStartBanks);
            run_contagion_parallel_prepared(parallelBanks, cumulativeLoans, parallelPlan);
        }
        auto parallel_end = std::chrono::high_resolution_clock::now();

        totalParTimes[i] = std::chrono::duration<double, std::milli>(
            parallel_end - parallel_start
        ).count();
        finalParDefaults[i] = count_defaulted_banks(parallelBanks);
    }

    for(std::size_t i = 0; i < threadNumbers.size(); i++){
        double speedup = totalSeqTime / totalParTimes[i];

        print_benchmark_row(
            numberOfBanks,
            finalLoanCount,
            shockPercentage,
            threadNumbers[i],
            finalSeqDefaults,
            finalParDefaults[i],
            totalSeqTime,
            totalParTimes[i],
            speedup
        );
    }
}


void run_benchmarking(){
    std::vector<int> bankNumbers = {5000, 10000};
    std::vector<double> shockPercentages = {0.20, 0.40, 0.60, 0.80};
    std::vector<int> threadNumbers = benchmark_thread_numbers();

    print_benchmark_header();

    for(int numberOfBanks : bankNumbers){
        std::vector<Bank> baseBanks=generate_banks(numberOfBanks);
        std::vector<std::vector<Loan>> preBuiltLoans(4);
        std::vector<Bank> generationBanks=baseBanks;

        for(int q=0; q<4; ++q){
            preBuiltLoans[q]=build_interbank_market(generationBanks);
            apply_relationship_decay(generationBanks);
        }

        for(double shockPercentage : shockPercentages){
            run_one_experiment(baseBanks, preBuiltLoans, numberOfBanks, shockPercentage, threadNumbers);
        }
    }

    print_separator();
}
