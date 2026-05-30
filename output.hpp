#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include "banks.hpp"
#include "market.hpp"

#include <vector>

void print_separator(int width = 120);

void print_benchmark_header();

void print_benchmark_row(int numberOfBanks, int numberOfLoans, double shockPercentage,
                            int numberOfThreads, int seqDefaults, int parDefaults, double seqTimeMs,
                            double parTimeMs, double speedup);

void print_market_demo(const std::vector<Bank>& banksBeforeMarket, 
        const std::vector<Bank>& banksAfterMarket,
        const std::vector<Loan>& loans);

void run_small_market_visualisation();

void print_small_market_intro(int numberOfBanks);

void print_no_loans_message();

void print_external_shock_message(int shockedBank);

void print_final_bank_states(const std::vector<Bank>& banks);

#endif

                    