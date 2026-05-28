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

double compute_equity(const Bank& bank){
    double total_assest = bank.assets + bank.cash + bank.otherAssets;
    double total_liabilities = bank.liabilities + bank.otherLiabilities;

    return total_assest - total_liabilities;
}

void update_equity(Bank& bank){
    bank.equity = compute_equity(bank);

    if(bank.equity < 0.0){
        bank.defaulted = true;
    }
}

bool is_insolvent(const Bank& bank){
    return compute_equity(bank) < 0.0;
}

bool is_illiquid(const Bank& bank, double payment_due){
    return bank.cash < payment_due;
}

