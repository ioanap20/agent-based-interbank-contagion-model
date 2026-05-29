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

#include "banks.cpp"
#include "market.cpp"
#include "shock.cpp"
#include "contagion.cpp"

#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <iomanip>
#include <algorithm>

struct BenchmarkResult{
    int numberOfBanks;
    int numberOfLoans;
    int numberOfDefaults;
    double shockPercentage;
    double executionTimeMs;
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
    Bank bank;

    bank.id = id;

    if(largeBank){
        bank.type = BankType :: Large;
        bank.role = BankRole :: Core;
    } else {
        bank.type = BankType::Small;
        bank.role = BankRole::Peripheral;
    }

    std::uniform_real_distribution<double> assetDistribution(1000.0, 10000.0);
    std::uniform_real_distribution<double> cashDistribution(100.0, 1000.0);

    double totalAssetsBeforeLiabilities = bank.balanceSheet.assets + bank.balanceSheet.cash + bank.balanceSheet.otherAssets;

    if(id % 5 == 0){
        bank.riskType = BankRiskType::Fragile;
        bank.balanceSheet.liabilities = 0.95 * totalAssetsBeforeLiabilities;
    } else {
        bank.riskType = BankRiskType::Robust;
        bank.balanceSheet.liabilities = 0.8 * totalAssetsBeforeLiabilities;
    }

    bank.balanceSheet.otherLiabilities = 0.0;

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

    return banks;
}

static BenchmarkResult run_one_experiment(int numberOfBanks, double shockPercentage){
    BenchmarkResult result;

    result.numberOfBanks = numberOfBanks;
    result.shockPercentage = shockPercentage;

    std::vector<Bank> banks = generate_banks(numberOfBanks);
    std::vector<Loan> loans = build_interbank_market(banks);

    result.numberOfLoans = loans.size();

    auto start = std::chrono::high_resolution_clock::now();

    apply_random_bank_shock(banks, 1, shockPercentage);

    run_contagion(banks, loans);

    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double, std::milli> duration = end - start;

    result.executionTimeMs = duration.count();
    result.numberOfDefaults = count_defaulted_banks(banks);

    return result;
}

void run_benchmarking(){
    std::vector<int> bankNumbers = {100, 500, 1000, 5000};

    std::vector<double> shockPercentages = {0.05, 0.10, 0.20, 0.30};

    std::cout<<"Banks, Loans, Shock, Defaults, Time(ms)" << std::endl;

    for(int numberOfBanks : bankNumbers){
        for(double shockPercentage : shockPercentages){
            BenchmarkResult result = run_one_experiment(numberOfBanks, shockPercentage);

            std::cout<<result.numberOfBanks<<","<<result.numberOfLoans<<","<<result.shockPercentage<<","<<result.numberOfDefaults<<","
            <<std::fixed<<std::setprecision(3)<<result.executionTimeMs<<std::endl;

        }
    }
}