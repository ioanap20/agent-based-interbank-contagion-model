/*
 * market.cpp
 *
 * Implements the interbank market.
 *
 * At a simulation stage, banks evaluate their liquidity position:
 *  - banks with excess liquidity become lenders,
 *  - banks with liquidity shortages become borrowers.
 *
 * The market mechanism then matches lenders and borrowers and creates
 * interbank loans between them.
 *
 * Functions:
 *  - identifying lenders and borrowers,
 *  - matching banks according to the chosen market rule,
 *  - creating loan exposures,
 *  - updating balance sheets after lending,
 *  - building the interbank network through which contagion can later spread.
 *
 */

#include "market.hpp" 

#include <vector>
#include <algorithm>
#include <random>

std::random_device rand;

std::vector<int> find_lenders(const std::vector<Bank>& banks, double liquidity_target){
   std::vector<int> lenders;

   for(int i=0; i < banks.size(); i++){
      if(!banks[i].defaulted && banks[i].liquidity > liquidity_target){
         lenders.push_back(i);
      }
   }

   return lenders;
}

std::vector<int> find_borrowers(const std::vector<Bank>& banks, double liquidity_target){
   std::vector<int> borrowers;

   for(int i=0; i < banks.size(); i++){
      if(!banks[i].defaulted && banks[i].liquidity < liquidity_target){
         borrowers.push_back(i);
      }
   }

   return borrowers;
}

std::vector<Loan> build_interbank_market(std::vector<Bank>& banks,
double liquidty_target, double max_loan_amount, int max_loans_per_borrower){

   std::vector<Loans> loans;
   std::vector<int> lenders = find_lenders(banks, liquidty_target);
   std::vector<int> borrowers = find_borrowers(banks, liquidty_target);

   std::mt19937 gen(rand());
   


}





