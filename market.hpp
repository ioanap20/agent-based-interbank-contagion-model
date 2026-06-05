#pragma once

#ifndef MARKET_HPP
#define MARKET_HPP

#include <vector>
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

//initialises hsitorical multiperiod memory
void initialize_market_memory(std::vector<Bank>& banks);

//end of period routine to decay elemtns
void apply_relationship_decay(std::vector<Bank>& banks);

std::vector<Loan> build_interbank_market(std::vector<Bank>& banks);

std::vector<int> find_lenders(const std::vector<Bank>& banks, LoanType type);
std::vector<int> find_borrowers(const std::vector<Bank>& bank, LoanType type);

double lending_gap(const Bank& bank, LoanType type);
double borrowing_gap(const Bank& bank, LoanType type);

double size_score(const Bank& borrower, const Bank& lender);
double relationship_score(int borrower_id, int lender_id, LoanType type, const std::vector<Loan>& previous_loans);
double combined_score(double size_score_value, double relationship_score_value);

double lending_probability(double score, const Bank& lender, const Bank& borrower);
double repayment_fraction(LoanType type);

void advance_market_time(std::vector<Bank>& banks, std::vector<Loan>& loans);

#endif