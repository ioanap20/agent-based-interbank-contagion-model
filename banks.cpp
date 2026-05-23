/*
* representation and initialization of the bank agents.
* Each bank is has:
 *  - external assets,
 *  - liquidity,
 *  - interbank assets and liabilities,
 *  - capital,
 *  - default status.
 *
 * functions:
 *  - computing total assets and capital,
 *  - checking whether a bank is solvent,
 *  - computing whether a bank has excess liquidity or a liquidity shortage,
 *  - generating the initial population of banks used in the simulation.
 *
 * Different types of banks:
 *  - small and large banks,
 *  - fragile and robust banks,
 *  - core and peripheral banks.
*/

#include <vector>
#include <random>
#include <algorithm>
#include <iostream>

enum Type{
    STRONG,
    FRAGILE
};

struct Bank{
    int id;

    double external_assets;
    double liquidity;
    double interbank_assets;

    double external_liabilities;
    double interbank_liabilities;

    double extra_liquidity;
    double founding_need;

    bool defaulted;
    Type type;
};

double total_assets(const Bank& bank){
    
}

double total_liabilities(const Bank& bank){

}

double capital(const Bank& bank){

}

bool should_default(const Bank& bank){

}

std::vector<Bank> initialize_banks(int nr_banks, double ratio_fragile){

}

int defaulted_banks(const std::vector<Bank>& banks){
    
}
