#pragma once

#ifndef CONTAGION_HPP
#define CONTAGION_HPP

#include <vector>
#include "banks.hpp"
#include "market.hpp"

void run_contagion(std::vector<Bank>& banks, const std::vector<Loan>& loans);

double total_outgoing_payment(int bank_id, const std::vector<Loan>& loans);
double total_incoming_payment(int bank_id, const std::vector<Loan>& loans);

void propagate_losses(std::vector<Bank>& banks, const std::vector<Loan>& loans);

#endif