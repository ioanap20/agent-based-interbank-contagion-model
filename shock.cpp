/*
 * shock.cpp
 *
 * External shock applied to the financial system.
 *
 * A shock reduces the assets or capital of one or more selected banks.
 * Depending on its intensity, the shock may cause some banks to default.
 *
 * Functions:
 *  - selecting the initially shocked banks,
 *  - reducing their balance-sheet quantities,
 *  - detecting banks that fail directly because of the shock,
 *  - returning the initial list of defaulted banks that will start
 *    the contagion cascade.
 *
 * Possible shock scenarios:
 *  - shocking a random bank,
 *  - shocking the largest bank,
 *  - shocking a core bank,
 *  - shocking several banks simultaneously.
 */

 #include "shock.hpp"
 #include <algorithm>
 #include <random>

 void apply_asset_shock(std::vector<Bank>& banks, double shock_percentage) {
    //check percentage is within the correct range
    if (shock_percentage < 0.0 || shock_percentage > 1.0) return;

    //apply shock
    for (auto& bank : banks) {
        if (!bank.defaulted) {
            //apply uniform haircut
            double loss = bank.balanceSheet.assets * shock_percentage;
            bank.balanceSheet.assets -= loss;

            //recalculate equity and update default status
            update_equity(bank);
            if (is_insolvent(bank)) {
                bank.defaulted = true;
            }
        }
    }
 }

 void apply_shock_to_large_banks(std::vector<Bank>& banks, double shock_percentage) {
    //check percentage is within the correct range
    if (shock_percentage < 0.0 || shock_percentage > 1.0) return;

    //apply shock
    for (auto& bank : banks) {
        if (!bank.defaulted) {
            //target=large for systemic core risk test
            if (bank.type == BankType::Large && !bank.defaulted) {
                //apply uniform haircut
                double loss = bank.balanceSheet.assets * shock_percentage;
                bank.balanceSheet.assets -= loss;

                //recalculate equity and update default status
                update_equity(bank);
                if (is_insolvent(bank)) {
                    bank.defaulted = true;
                }
            }
        }
    }
 }

void apply_random_bank_shock(std::vector<Bank>& banks, int number_of_banks, double shock_percentage) {
    std::random_device rd;
    std::mt19937 gen(rd());
    apply_random_bank_shock(banks, number_of_banks, shock_percentage, gen);
}

void apply_random_bank_shock(std::vector<Bank>& banks, int number_of_banks, double shock_percentage, std::mt19937& gen) {
    //check percentage is within the correct range
    if (shock_percentage < 0.0 || shock_percentage > 1.0) return;

    //collect indices of non-defaulted banks
    std::vector<size_t> active_indices;
    active_indices.reserve(banks.size());
    for (size_t i = 0; i < banks.size(); ++i) {
        if (!banks[i].defaulted) {
            active_indices.push_back(i);
        }
    }

    //shuffle indices
    std::shuffle(active_indices.begin(), active_indices.end(), gen);

    //set safe bounds
    int target_count = std::min(number_of_banks, static_cast<int>(active_indices.size()));

    //shock the selected banks
    for (int i=0; i < target_count; ++i) {
        size_t bank_idx = active_indices[i];
        Bank& bank = banks[bank_idx];

        double loss = bank.balanceSheet.assets * shock_percentage;
            bank.balanceSheet.assets -= loss;

        //recalculate equity and update default status
        update_equity(bank);
        if (is_insolvent(bank)) {
            bank.defaulted = true;
        }
    }
}
