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

static void propagate_losses_from_frontier_parallel(std::vector<Bank>& banks, const std::vector<Loan>& loans, const std::vector<int>& frontier, int numberOfThreads);

#endif