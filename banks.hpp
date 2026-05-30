#pragma once

#include <vector>

enum class BankType{
    Small,
    Large
};

enum class BankRiskType{
    Fragile,
    Robust
};

enum class BankRole{
    Peripheral,
    Core
};

struct BalanceSheet{
    double assets = 0.0; //what people owe you ex: buildings, shares, trades etc...
        //anything that brings income in the future
    double liabilities = 0.0; //what you owe them

    double cash = 0.0; //actual money
    double otherAssets = 0.0;//actual money
    double otherLiabilities = 0.0; //other things besides banks

    double equity = 0.0; //How much you add to your liabilities to reach your assets 
                    // it should be as big as possible
    };

struct Bank{
    int id;
    BankType type;

    BankRiskType riskType = BankRiskType::Robust;
    BankRole role = BankRole::Peripheral;

    BalanceSheet balanceSheet;

    bool defaulted = false; // no more money - can not pay dept

    double receivedLoss = 0.0;

    double targetOvernightLendingRatio = 0.0; // lending such that the tables match at the end of the day that are being given back overnight
    double targetOvernightBorrowingRatio = 0.0; // same but inversed
    double targetShortTermLendingRatio = 0.0; // lending from the guvernment
    double targetShortTermBorrowingRatio = 0.0; // borrowing from the guvernment
    double targetLongTermLendingRatio = 0.0; // lending within banks by own initiative
    double targetLongTermBorrowingRatio = 0.0; // the other way around

    //necessary for equation 7 in the paper
    double sum_log_assets_past_counterparties = 0.0;
    int count_past_counterparties = 0;

    //necessary for equation 8 in the paper
    std::vector<std::vector<double>> relationship_scores;
};

double compute_equity(const Bank& bank);
double total_assets(const Bank& bank);
double total_liabilities(const Bank& bank);

bool is_insolvent(const Bank& bank); // asstes < libilities 

bool is_illiquid(
    const Bank& bank,
    double payment_due,
    double incomingPayment
); //checks if you have or not enough cash

void update_equity(Bank& bank);
void apply_loss(Bank& bank, double loss);

