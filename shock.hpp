#pragma once

#ifndef SHOCK_HPP
#define SHOCK_HPP

#include <vector>
#include "banks.hpp"

void apply_asset_shock(std::vector<Bank>& banks, double shock_percentage);
void apply_shock_to_large_banks(std::vector<Bank>& banks, double shock_percentage);
void apply_random_bank_shock(std::vector<Bank>& banks, int number_of_banks, double shock_pecentage);

#endif