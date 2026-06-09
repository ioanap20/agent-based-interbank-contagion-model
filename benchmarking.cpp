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
#include <fstream>
#include <sstream>
#include <cstdint>
#include <iterator>

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

std::vector<Bank> generate_banks(int numberOfBanks, std::mt19937& gen);

std::vector<Bank> generate_banks(int numberOfBanks){
    std::random_device rd;
    std::mt19937 gen(rd());
    return generate_banks(numberOfBanks, gen);
}

std::vector<Bank> generate_banks(int numberOfBanks, std::mt19937& gen){
    std::vector<Bank> banks;

    banks.reserve(numberOfBanks);

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

static std::vector<std::uint32_t> default_experiment_seeds(){
    return {
        104729u,
        130363u,
        15485863u,
        32452843u,
        49979687u,
        67867967u,
        86028121u,
        104395303u,
        122949829u,
        141650939u
    };
}

static void write_seed_file(const std::string& path, const std::vector<std::uint32_t>& seeds){
    std::ofstream out(path);
    out << "{\n  \"seeds\": [";
    for(std::size_t i = 0; i < seeds.size(); ++i){
        if(i > 0){
            out << ", ";
        }
        out << seeds[i];
    }
    out << "]\n}\n";
}

static std::vector<std::uint32_t> read_seed_file(const std::string& path){
    std::ifstream in(path);
    if(!in){
        std::vector<std::uint32_t> seeds = default_experiment_seeds();
        write_seed_file(path, seeds);
        return seeds;
    }

    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for(char& ch : contents){
        if(ch < '0' || ch > '9'){
            ch = ' ';
        }
    }

    std::vector<std::uint32_t> seeds;
    std::stringstream parser(contents);
    unsigned long value = 0;
    while(parser >> value && seeds.size() < 10){
        seeds.push_back(static_cast<std::uint32_t>(value));
    }

    if(seeds.size() != 10){
        seeds = default_experiment_seeds();
        write_seed_file(path, seeds);
    }

    return seeds;
}

static std::uint32_t derive_shock_seed(std::uint32_t seed, std::size_t shockIndex){
    return seed ^ static_cast<std::uint32_t>(0x9e3779b9u + 1000003u * static_cast<std::uint32_t>(shockIndex + 1));
}

static std::vector<std::vector<Loan>> build_seeded_quarter_loans(std::vector<Bank>& generationBanks, std::mt19937& gen){
    std::vector<std::vector<Loan>> preBuiltLoans(4);

    for(int q = 0; q < 4; ++q){
        preBuiltLoans[q] = build_interbank_market(generationBanks, gen);
        apply_relationship_decay(generationBanks);
    }

    return preBuiltLoans;
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

static void prepare_contagion_start(const std::vector<Bank>& baseBanks,
                                    const std::vector<std::vector<Loan>>& preBuiltLoans,
                                    int numberOfBanks,
                                    double shockPercentage,
                                    std::uint32_t shockSeed,
                                    std::vector<Bank>& experimentStartBanks,
                                    std::vector<Loan>& cumulativeLoans){
    std::vector<Bank> banks = baseBanks;
    cumulativeLoans.clear();
    std::mt19937 shockGen(shockSeed);

    for(int q = 0; q < 4; ++q){
        const std::vector<Loan>& quarterLoans = preBuiltLoans[q];
        cumulativeLoans.insert(cumulativeLoans.end(), quarterLoans.begin(), quarterLoans.end());

        if(q == 0){
            apply_random_bank_shock(banks, std::max(1, numberOfBanks / 20), shockPercentage, shockGen);
        }

        advance_market_time(banks, cumulativeLoans);
    }

    experimentStartBanks = banks;
}

void run_seeded_experiment(int numberOfBanks,
                           int numberOfThreads,
                           const std::string& seedsPath,
                           const std::string& resultsPath,
                           bool runSequential){
    const std::vector<std::uint32_t> seeds = read_seed_file(seedsPath);
    write_seed_file(seedsPath, seeds);
    const std::vector<double> shockPercentages = {0.20, 0.40, 0.60, 0.80};
    const int timingRepeats = 64;

    std::ofstream out(resultsPath);
    if(!out){
        std::cerr << "Could not write results JSON: " << resultsPath << std::endl;
        return;
    }

    out << "{\n";
    out << "  \"number_of_banks\": " << numberOfBanks << ",\n";
    out << "  \"number_of_threads\": " << numberOfThreads << ",\n";
    out << "  \"run_sequential\": " << (runSequential ? "true" : "false") << ",\n";
    out << "  \"timing_repeats\": " << timingRepeats << ",\n";
    out << "  \"seeds_file\": \"" << seedsPath << "\",\n";
    out << "  \"seeds\": [";
    for(std::size_t i = 0; i < seeds.size(); ++i){
        if(i > 0){
            out << ", ";
        }
        out << seeds[i];
    }
    out << "],\n";
    out << "  \"results\": [\n";

    bool firstResult = true;
    for(std::size_t seedIndex = 0; seedIndex < seeds.size(); ++seedIndex){
        const std::uint32_t seed = seeds[seedIndex];
        std::mt19937 gen(seed);
        std::vector<Bank> baseBanks = generate_banks(numberOfBanks, gen);
        std::vector<Bank> generationBanks = baseBanks;
        std::vector<std::vector<Loan>> preBuiltLoans = build_seeded_quarter_loans(generationBanks, gen);

        for(std::size_t shockIndex = 0; shockIndex < shockPercentages.size(); ++shockIndex){
            const double shockPercentage = shockPercentages[shockIndex];
            const std::uint32_t shockSeed = derive_shock_seed(seed, shockIndex);
            std::vector<Bank> experimentStartBanks;
            std::vector<Loan> cumulativeLoans;
            prepare_contagion_start(
                baseBanks,
                preBuiltLoans,
                numberOfBanks,
                shockPercentage,
                shockSeed,
                experimentStartBanks,
                cumulativeLoans
            );

            int sequentialDefaults = -1;
            double sequentialTimeMs = 0.0;
            if(runSequential){
                std::vector<Bank> sequentialBanks = experimentStartBanks;
                auto sequentialStart = std::chrono::high_resolution_clock::now();
                for(int rep = 0; rep < timingRepeats; ++rep){
                    reset_banks_for_contagion(sequentialBanks, experimentStartBanks);
                    run_contagion(sequentialBanks, cumulativeLoans);
                }
                auto sequentialEnd = std::chrono::high_resolution_clock::now();
                sequentialTimeMs = std::chrono::duration<double, std::milli>(
                    sequentialEnd - sequentialStart
                ).count();
                sequentialDefaults = count_defaulted_banks(sequentialBanks);
            }

            std::vector<Bank> parallelBanks = experimentStartBanks;
            ParallelContagionPlan parallelPlan(numberOfBanks, cumulativeLoans, numberOfThreads);
            auto parallelStart = std::chrono::high_resolution_clock::now();
            for(int rep = 0; rep < timingRepeats; ++rep){
                reset_banks_for_contagion(parallelBanks, experimentStartBanks);
                run_contagion_parallel_prepared(parallelBanks, cumulativeLoans, parallelPlan);
            }
            auto parallelEnd = std::chrono::high_resolution_clock::now();
            const double parallelTimeMs = std::chrono::duration<double, std::milli>(
                parallelEnd - parallelStart
            ).count();
            const int parallelDefaults = count_defaulted_banks(parallelBanks);
            const double speedup = runSequential && parallelTimeMs > 0.0 ? sequentialTimeMs / parallelTimeMs : 0.0;

            if(!firstResult){
                out << ",\n";
            }
            firstResult = false;

            out << "    {\n";
            out << "      \"seed\": " << seed << ",\n";
            out << "      \"shock_seed\": " << shockSeed << ",\n";
            out << "      \"shock_percentage\": " << std::fixed << std::setprecision(2) << shockPercentage << ",\n";
            out << "      \"effective_threads\": " << parallelPlan.thread_count() << ",\n";
            out << "      \"number_of_loans\": " << cumulativeLoans.size() << ",\n";
            if(runSequential){
                out << "      \"sequential_defaults\": " << sequentialDefaults << ",\n";
            }else{
                out << "      \"sequential_defaults\": null,\n";
            }
            out << "      \"parallel_defaults\": " << parallelDefaults << ",\n";
            if(runSequential){
                out << "      \"sequential_time_ms\": " << std::fixed << std::setprecision(6) << sequentialTimeMs << ",\n";
            }else{
                out << "      \"sequential_time_ms\": null,\n";
            }
            out << "      \"parallel_time_ms\": " << std::fixed << std::setprecision(6) << parallelTimeMs << ",\n";
            if(runSequential){
                out << "      \"speedup\": " << std::fixed << std::setprecision(6) << speedup << ",\n";
                out << "      \"check\": \"" << (sequentialDefaults == parallelDefaults ? "OK" : "DIFF") << "\"\n";
            }else{
                out << "      \"speedup\": null,\n";
                out << "      \"check\": \"SKIPPED\"\n";
            }
            out << "    }";
        }
    }

    out << "\n  ]\n";
    out << "}\n";

    std::cout << "Seeded experiment results written to " << resultsPath << std::endl;
    std::cout << "Seeds written/read from " << seedsPath << std::endl;
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
