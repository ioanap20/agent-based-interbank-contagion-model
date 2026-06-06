/*
 * run_small_market.cpp
 *
 * Small visual run of the interbank contagion model.
 *
 * This file does not contain pretty-printing logic.
 * It only controls the scenario:
 *  1. create a small number of banks,
 *  2. build the interbank market,
 *  3. print the market using output.cpp,
 *  4. shock one important borrower,
 *  5. run contagion with verbose output,
 *  6. print the final state.
 */

#include "small_market.hpp"
#include "banks.hpp"
#include "market.hpp"
#include "contagion.hpp"
#include "output.hpp"

#include <vector>
#include <iostream>
#include <random>
#include <algorithm>

std::vector<Bank> generate_banks(int numberOfBanks);

static int count_defaulted(const std::vector<Bank>& banks){
    int count = 0;
    for(const Bank& bank : banks){
        if(bank.defaulted){
            count++;
        }
    }
    return count;
}

static Bank create_demo_bank(int id, std::mt19937& gen, bool largeBank){
    Bank bank{};

    bank.id = id;
    bank.defaulted = false;
    bank.receivedLoss = 0.0;

    if(largeBank){
        bank.type = BankType::Large;
        bank.role = BankRole::Core;
    } else {
        bank.type = BankType::Small;
        bank.role = BankRole::Peripheral;
    }

    std::uniform_real_distribution<double> assetDistribution(50000.0, 2500000.0);
    std::uniform_real_distribution<double> cashDistribution(5000.0, 250000.0);

    if(largeBank){
        bank.balanceSheet.assets = assetDistribution(gen) * 10.0;
        bank.balanceSheet.otherAssets = assetDistribution(gen) * 10.0;
        bank.balanceSheet.cash = cashDistribution(gen) * 10.0;
    } else {
        bank.balanceSheet.assets = assetDistribution(gen);
        bank.balanceSheet.otherAssets = assetDistribution(gen);
        bank.balanceSheet.cash = cashDistribution(gen);
    }

    double totalAssetsBeforeLiabilities = total_assets(bank);

    if(id % 5 == 0){
        bank.riskType = BankRiskType::Fragile;
        bank.balanceSheet.liabilities = 0.97 * totalAssetsBeforeLiabilities;
    } else {
        bank.riskType = BankRiskType::Robust;
        bank.balanceSheet.liabilities = 0.90 * totalAssetsBeforeLiabilities;
    }

    if(largeBank){
        bank.targetOvernightLendingRatio = 0.08;
        bank.targetOvernightBorrowingRatio = 0.04;

        bank.targetShortTermLendingRatio = 0.05;
        bank.targetShortTermBorrowingRatio = 0.03;

        bank.targetLongTermLendingRatio = 0.03;
        bank.targetLongTermBorrowingRatio = 0.02;
    } else {
        bank.targetOvernightLendingRatio = 0.04;
        bank.targetOvernightBorrowingRatio = 0.03;

        bank.targetShortTermLendingRatio = 0.02;
        bank.targetShortTermBorrowingRatio = 0.025;

        bank.targetLongTermLendingRatio = 0.015;
        bank.targetLongTermBorrowingRatio = 0.02;
    }

    update_equity(bank);

    return bank;
}

static std::vector<Bank> generate_demo_banks(int numberOfBanks){
    std::vector<Bank> banks;
    banks.reserve(numberOfBanks);

    std::random_device rd;
    std::mt19937 gen(rd());

    int numberOfLargeBanks = std::max(1, numberOfBanks / 10);

    for(int i = 0; i < numberOfBanks; i++){
        bool largeBank = i < numberOfLargeBanks;
        banks.push_back(create_demo_bank(i, gen, largeBank));
    }

    initialize_market_memory(banks);

    return banks;
}

static int choose_bank_with_most_borrowing(const std::vector<Loan>& loans, int numberOfBanks){
    std::vector<int> numberOfIncomingLoans(numberOfBanks, 0);

    for(const Loan& loan : loans){
        numberOfIncomingLoans[loan.borrower]++;
    }

    int chosenBank = 0;
    int maximumIncomingLoans = numberOfIncomingLoans[0];

    for(int bank_id = 1; bank_id < numberOfBanks; bank_id++){
        if(numberOfIncomingLoans[bank_id] > maximumIncomingLoans){
            maximumIncomingLoans = numberOfIncomingLoans[bank_id];
            chosenBank = bank_id;
        }
    }
    return chosenBank;
}

static int choose_top_lender(const std::vector<Loan>& loans, int numberOfBanks){
    std::vector<int> numberOfOutgoingLoans(numberOfBanks, 0);

    for(const Loan& loan : loans){
        numberOfOutgoingLoans[loan.lender]++;
    }

    int chosenBank = 0;
    int maximumOutgoingLoans = numberOfOutgoingLoans[0];

    for(int bank_id = 1; bank_id < numberOfBanks; bank_id++){
        if(numberOfOutgoingLoans[bank_id] > maximumOutgoingLoans){
            maximumOutgoingLoans = numberOfOutgoingLoans[bank_id];
            chosenBank = bank_id;
        }
    }
    return chosenBank;
}

static void apply_asset_shock(Bank& bank, double shockPercentage){
    double assetLoss = shockPercentage * bank.balanceSheet.otherAssets;

    bank.balanceSheet.otherAssets -= assetLoss;

    if(bank.balanceSheet.otherAssets < 0.0){
        bank.balanceSheet.otherAssets = 0.0;
    }

    bank.receivedLoss += assetLoss;

    update_equity(bank);

    if(is_insolvent(bank)){
        bank.defaulted = true;
    }
}

static void apply_demo_shock(Bank& bank, double shockPercentage){
    double assetsLoss = shockPercentage * bank.balanceSheet.assets;
    double otherAssetsLoss = shockPercentage * bank.balanceSheet.otherAssets;
    double cashLoss = 0.40 * shockPercentage * bank.balanceSheet.cash;

    bank.balanceSheet.assets -= assetsLoss;
    bank.balanceSheet.otherAssets -= otherAssetsLoss;
    bank.balanceSheet.cash -= cashLoss;

    if(bank.balanceSheet.assets < 0.0){
        bank.balanceSheet.assets = 0.0;
    }
    if(bank.balanceSheet.otherAssets < 0.0){
        bank.balanceSheet.otherAssets = 0.0;
    }
    if(bank.balanceSheet.cash < 0.0){
        bank.balanceSheet.cash = 0.0;
    }

    bank.receivedLoss += assetsLoss + otherAssetsLoss + cashLoss;
    update_equity(bank);
    bank.defaulted = true;
}

static std::vector<int> choose_top_borrowers(const std::vector<Loan>& loans, int numberOfBanks, int numberToChoose){
    std::vector<int> numberOfIncomingLoans(numberOfBanks, 0);

    for(const Loan& loan : loans){
        numberOfIncomingLoans[loan.borrower]++;
    }

    std::vector<int> bankIds(numberOfBanks);

    for(int i = 0; i < numberOfBanks; i++){
        bankIds[i] = i;
    }

    std::sort(bankIds.begin(), bankIds.end(), [&](int a, int b){
        return numberOfIncomingLoans[a] > numberOfIncomingLoans[b];
    });

    if(numberToChoose > numberOfBanks){
        numberToChoose = numberOfBanks;
    }

    std::vector<int> chosenBanks;

    for(int i = 0; i < numberToChoose; i++){
        chosenBanks.push_back(bankIds[i]);
    }

    return chosenBanks;
}

void run_small_market_visualization(){
    const int numberOfBanks = 22;

    print_small_market_intro(numberOfBanks);

    std::vector<Bank> banks = generate_banks(numberOfBanks);

    std::vector<Bank> banksBeforeMarket = banks;

    std::vector<Loan> loans = build_interbank_market(banks);

    print_market_demo(banksBeforeMarket, banks, loans);

    if(loans.empty()){
        print_no_loans_message();
        return;
    }

    int shockedBank = choose_bank_with_most_borrowing(loans, numberOfBanks);

    print_external_shock_message(shockedBank);

    double shockPercentage = 0.7;

    apply_asset_shock(banks[shockedBank], shockPercentage);

    run_contagion_small(banks, loans);

    print_final_bank_states(banks);
}

void run_random_small_market_visualization(){
    std::random_device rd;
    std::mt19937 generator(rd());

    std::uniform_int_distribution<int> bankDistribution(15, 25);
    std::uniform_real_distribution<double> shockDistribution(0.65, 0.95);
    std::uniform_int_distribution<int> shockedBanksDistribution(2, 4);

    int numberOfBanks = bankDistribution(generator);
    int numberOfShockedBanks = shockedBanksDistribution(generator);
    double shockPercentage = shockDistribution(generator);

    print_small_market_intro(numberOfBanks);

    std::vector<Bank> banks = generate_banks(numberOfBanks);

    std::vector<Bank> banksBeforeMarket = banks;

    std::vector<Loan> loans = build_interbank_market(banks);

    print_market_demo(banksBeforeMarket, banks, loans);

    if(loans.empty()){
        print_no_loans_message();
        return;
    }

    std::vector<int> shockedBanks = choose_top_borrowers(loans, numberOfBanks, numberOfShockedBanks);

    std::cout << std::endl;
    std::cout << "================ RANDOM STRESS EXPERIMENT ================" << std::endl;
    std::cout << "Number of banks: " << numberOfBanks << std::endl;
    std::cout << "Number of loans: " << loans.size() << std::endl;
    std::cout << "Shock percentage: " << shockPercentage * 100.0 << "%" << std::endl;
    std::cout << "Initially shocked banks: ";

    for(int bankId : shockedBanks){
        std::cout << bankId << " ";
    }

    std::cout << std::endl;
    std::cout << "==========================================================" << std::endl;

    for(int shockedBank : shockedBanks){
        apply_asset_shock(banks[shockedBank], shockPercentage);
    }

    run_contagion_small(banks, loans);

    print_final_bank_states(banks);
}

void run_extreme_small_market_visualization(){
    const int numberOfBanks = 20;
    const int numQuarters = 4;
    const double shockPercentage = 0.90;
    const double secondaryShockPercentage = 0.40;
    const double demoRecoveryRate = 0.15;

    const MarketConfig demoMarketConfig{
        8,
        100000.0,
        0.85,
        5.0,
        5000.0
    };

    print_small_market_intro(numberOfBanks);

    std::cout << "Mode: EXTREME multi-quarter stress scenario (demo only)" << std::endl;
    std::cout << "Benchmark parameters are unchanged." << std::endl;
    std::cout << "============================================================" << std::endl;

    std::vector<Bank> banks = generate_demo_banks(numberOfBanks);
    std::vector<Bank> banksBeforeMarket = banks;
    std::vector<Loan> cumulativeLoans;
    std::vector<char> propagated(banks.size(), false);

    for(int quarter = 1; quarter <= numQuarters; ++quarter){
        print_quarter_header(quarter, static_cast<int>(cumulativeLoans.size()));

        std::vector<Loan> newLoans = build_interbank_market(banks, demoMarketConfig);
        cumulativeLoans.insert(cumulativeLoans.end(), newLoans.begin(), newLoans.end());

        std::cout << "New loans this quarter: " << newLoans.size()
                  << " | Total loans: " << cumulativeLoans.size()
                  << std::endl;

        if(quarter == 1){
            print_market_demo(banksBeforeMarket, banks, cumulativeLoans);

            if(cumulativeLoans.empty()){
                print_no_loans_message();
                return;
            }

            std::vector<int> shockedBanks = choose_top_borrowers(cumulativeLoans, numberOfBanks, 4);
            int topLender = choose_top_lender(cumulativeLoans, numberOfBanks);

            if(std::find(shockedBanks.begin(), shockedBanks.end(), topLender) == shockedBanks.end()){
                shockedBanks.push_back(topLender);
            }

            print_demo_shock_message(shockedBanks, shockPercentage);

            for(int bankId : shockedBanks){
                apply_demo_shock(banks[bankId], shockPercentage);
            }
        }

        if(quarter == 3){
            int target = choose_bank_with_most_borrowing(cumulativeLoans, numberOfBanks);
            if(!banks[target].defaulted){
                std::cout << std::endl;
                std::cout << "Secondary shock in Q3 on bank " << target
                          << " (" << secondaryShockPercentage * 100.0 << "%)"
                          << std::endl;
                apply_demo_shock(banks[target], secondaryShockPercentage);
            }
        }

        run_contagion_small(banks, cumulativeLoans, demoRecoveryRate, &propagated);

        print_quarter_summary(quarter, banks);
        print_cumulative_defaults(count_defaulted(banks), numberOfBanks);

        advance_market_time(banks, cumulativeLoans);
    }

    print_final_bank_states(banks);
}
