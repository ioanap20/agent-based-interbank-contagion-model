#pragma once

#ifndef MARKET_HPP
#define MARKET_HPP

#include <vector>
#include <random>
#include "banks.hpp"

enum class LoanType{
    Overnight,
    ShortTerm,
    LongTerm
};

struct Loan{
    int lender;
    int borrower;

    double amount;
    double payment_due;
    double remaining;

    LoanType type;

    int remaining_quarters;
};
// we create loan_indexes to be able to viasualise the small market and to keep track of all loans and trasactions between banks
struct LoanIndex{
    std::vector<std::vector<int>> by_borrower;
    std::vector<double> outgoing_payment;
    std::vector<double> incoming_payment;
};

struct MarketConfig{
    int max_loans_per_borrower = 6;
    double max_loan_amount = 100000.0;
    double max_fraction_of_lender_cash = 0.65;
    double max_fraction_of_lender_equity = 3.50;
    double min_loan_amount = 5000.0;
};

//initialises hsitorical multiperiod memory
void initialize_market_memory(std::vector<Bank>& banks);

//end of period routine to decay elemtns
void apply_relationship_decay(std::vector<Bank>& banks);

std::vector<Loan> build_interbank_market(std::vector<Bank>& banks, const MarketConfig& config = MarketConfig{});
std::vector<Loan> build_interbank_market(std::vector<Bank>& banks, std::mt19937& gen, const MarketConfig& config = MarketConfig{});

std::vector<int> find_lenders(const std::vector<Bank>& banks, LoanType type);
std::vector<int> find_borrowers(const std::vector<Bank>& banks, LoanType type);

double lending_gap(const Bank& bank, LoanType type);
double borrowing_gap(const Bank& bank, LoanType type);

double size_score(const Bank& borrower, const Bank& lender);
double relationship_score(const Bank& borrower, int lender_id, LoanType type);
double combined_score(double size_score_value, double relationship_score_value);

double lending_probability(double score, const Bank& lender, const Bank& borrower);
double repayment_fraction(LoanType type);
double repayment_fraction(LoanType type, std::mt19937& gen);

void advance_market_time(std::vector<Bank>& banks, std::vector<Loan>& loans);
LoanIndex build_loan_index(const std::vector<Loan>& loans, int num_banks);

double total_outgoing_payment(int bank_id, const std::vector<Loan>& loans);
double total_incoming_payment(int bank_id, const std::vector<Loan>& loans);

#endif
