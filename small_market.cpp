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

    bank.balanceSheet.otherAssets -= assetLoss;

    if(bank.balanceSheet.otherAssets < 0.0){
        bank.balanceSheet.otherAssets = 0.0;
    }

    bank.receivedLoss += assetLoss;

    update_equity(bank);

    bank.defaulted = true;
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

