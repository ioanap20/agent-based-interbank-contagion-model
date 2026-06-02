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

std::vector<Bank> generate_banks(int numberOfBanks);

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

static void apply_asset_shock(Bank& bank, double shockPercentage){
    double assetLoss = shockPercentage * bank.balanceSheet.otherAssets;

    bank.balanceSheet.otherAssets -= assetLoss;

    if(bank.balanceSheet.otherAssets < 0.0){
        bank.balanceSheet.otherAssets = 0.0;
    }

    bank.receivedLoss += assetLoss;

    update_equity(bank);

    bank.defaulted = true;
}

static std::vector<int>choose_top_borrowers(const std::vector<Loan>& loans, int numberOfBanks, int numberToChoose){
    
    std::vector<int> numberOfIncomingLoans(numberOfBanks, 0);

    for(const Loan& loan : loans){
        numberOfIncomingLoans[loan.borrower] ++;
    }

    std::vector<int> bankIds(numberOfBanks);

    for(int i=0; i < numberOfBanks; i++){
        bankIds[i] = i;
    }

    std::sort(bankIds.begin(), bankIds.end(), [&](int a, int b){
        return numberOfIncomingLoans[a] > numberOfIncomingLoans[b];
    });

    if(numberToChoose > numberOfBanks){
        numberToChoose = numberOfBanks;
    }

    std::vector<int> chosenBanks;

    for(int i=0; i<numberToChoose; i++){
        chosenBanks.push_back(bankIds[i]);
    }

    return chosenBanks;
}


void run_small_market_visualization(){
    const int numberOfBanks = 12;

    print_small_market_intro(numberOfBanks);

    std::vector<Bank> banks = generate_banks(numberOfBanks);

    std::vector<Bank>banksBeforeMarket = banks;

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

    std::vector<Bank>banksBeforeMarket = banks;

    std::vector<Loan> loans = build_interbank_market(banks);

    print_market_demo(banksBeforeMarket, banks, loans);

    if(loans.empty()){
        print_no_loans_message();
        return;
    }

    std::vector<int> shockedBanks = choose_top_borrowers(loans, numberOfBanks, numberOfShockedBanks);

    //print_external_shock_message(shockedBank);

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

    //double shockPercentage = 0.7;
    for(int shockedBank : shockedBanks){
        apply_asset_shock(banks[shockedBank], shockPercentage);
    }

    run_contagion_small(banks, loans);

    print_final_bank_states(banks);
}



