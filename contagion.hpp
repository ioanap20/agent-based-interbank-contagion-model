#pragma once

#ifndef CONTAGION_HPP
#define CONTAGION_HPP

#include <vector>
#include "banks.hpp"
#include "market.hpp"

void run_contagion(std::vector<Bank>& banks, const std::vector<Loan>& loans);
void run_contagion_parallel(std::vector<Bank>& banks, const std::vector<Loan>& loans, int numberOfThreads);

double total_outgoing_payment(int bank_id, const std::vector<Loan>& loans);
double total_incoming_payment(int bank_id, const std::vector<Loan>& loans);

void run_contagion_small(
    std::vector<Bank>& banks,
    const std::vector<Loan>& loans,
    double recovery_rate = 0.30,
    std::vector<char>* persistent_propagated = nullptr
);

#endif