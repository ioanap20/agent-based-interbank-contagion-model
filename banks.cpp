/*
* banks.cpp
 *
 * Implements the basic financial behaviour of bank agents.
 *
 * Each bank has:
 *  - assets: what the bank owns or what others owe to it,
 *  - liabilities: what the bank owes to others,
 *  - cash: immediately available money,
 *  - equity: the difference between assets and liabilities,
 *  - default status.
 *
 * Main ideas:
 *  - A bank is insolvent if its assets are smaller than its liabilities.
 *  - A bank is illiquid if it does not have enough cash to make a required payment.
 *  - Equity is recomputed whenever assets or liabilities change.
 *
*/

#include "banks.hpp"

double total_assets(const Bank& bank){
        return bank.balanceSheet.assets + bank.balanceSheet.cash + bank.balanceSheet.otherAssets;
}

double total_liabilities(const Bank& bank){
        return  bank.balanceSheet.liabilities + bank.balanceSheet.otherLiabilities;

}
double compute_equity(const Bank& bank){
    return total_assets(bank) - total_liabilities(bank);
}

void update_equity(Bank& bank){
    bank.balanceSheet.equity = compute_equity(bank);

    if(bank.balanceSheet.equity < 0.0){
        bank.defaulted = true;
    }
}

bool is_insolvent(const Bank& bank){
    return compute_equity(bank) < 0.0;
}

bool is_illiquid(const Bank& bank, double payment_due, double incomingPayment){
    double available_cash = bank.balanceSheet.cash + incomingPayment;
    
    return available_cash < payment_due;
}

void apply_loss(Bank& bank, double loss){
    if(loss <= 0) return;

    double remaining = loss;

    double from_assets = std::min(remaining, bank.balanceSheet.assets);
    bank.balanceSheet.assets -= from_assets;
    remaining -= from_assets;

    double from_other = std::min(remaining, bank.balanceSheet.otherAssets);
    bank.balanceSheet.otherAssets -= from_other;
    remaining -= from_other;

    double from_cash = std::min(remaining, bank.balanceSheet.cash);
    bank.balanceSheet.cash -= from_cash;
    
    bank.receivedLoss += loss;
    update_equity(bank);
}

