#pragma once

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


                    