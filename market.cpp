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
#include <cmath>

std::random_device randgen;

static int loan_type_index(LoanType type){
   switch(type){
      case LoanType::Overnight:
         return 0;

      case LoanType::ShortTerm:
         return 1;

      case LoanType::LongTerm:
         return 2;
   }
   return 0;
}

static double target_lending_amount(const Bank& bank, LoanType type){
   double assets = total_assets(bank);

   switch (type)
   {
   case LoanType::Overnight:
      return bank.targetOvernightLendingRatio * assets;
   
   case LoanType::ShortTerm:
      return bank.targetShortTermLendingRatio * assets;
   
   case LoanType::LongTerm:
      return bank.targetLongTermLendingRatio * assets;

   }
   return 0.0;
}

static double target_borrowing_amount(const Bank& bank, LoanType type){
   double liabilities = total_liabilities(bank);

   switch (type)
   {
   case LoanType::Overnight:
      return bank.targetOvernightBorrowingRatio * liabilities;
   
   case LoanType::ShortTerm:
      return bank.targetShortTermBorrowingRatio * liabilities;
   
   case LoanType::LongTerm:
      return bank.targetLongTermBorrowingRatio * liabilities;

   }
   return 0.0;
}

double lending_gap(const Bank& bank, LoanType type){
   if(bank.defaulted) return 0.0;

   double target = target_lending_amount(bank, type);

   double available_cash = bank.balanceSheet.cash;

   if(available_cash < 0.0)
      available_cash = 0.0;

   return std::min(target, available_cash);
}

double borrowing_gap(const Bank& bank, LoanType type){
   if(bank.defaulted) return 0.0;

   double target = target_borrowing_amount(bank, type);

   if(target < 0.0) return 0.0;

   return target;
}

std::vector<int> find_lenders(const std::vector<Bank>& banks, LoanType type){
   std::vector<int> lenders;

   for(int i=0; i < banks.size(); i++){
      if(!banks[i].defaulted && lending_gap(banks[i], type) > 0.0){
         lenders.push_back(i);
      }
   }

   return lenders;
}

std::vector<int> find_borrowers(const std::vector<Bank>& banks, LoanType type){
   std::vector<int> borrowers;

   for(int i=0; i < banks.size(); i++){
      if(!banks[i].defaulted && borrowing_gap(banks[i], type) > 0.0){
         borrowers.push_back(i);
      }
   }

   return borrowers;
}

double size_score(const Bank& borrower, const Bank& lender){
   double borrower_assets = std::max(total_assets(borrower), 1.0);
   double lender_assets  = std::max(total_assets(lender), 1.0);

   return std::log(lender_assets) - std::log(borrower_assets);
}

double relationship_score(int borrower_id, int lender_id, LoanType type, const std::vector<Loan>& previous_loans){
   double score = 0.0;

   for(const Loan& loan : previous_loans){
      if(loan.borrower == borrower_id && loan.lender == lender_id && loan.type == type){
         score += std::log(loan.amount + 1.0);
      }
   }

   return score;
}

double combined_score(double size_score_value, double relationship_score_value){
   return 0.5 * size_score_value + 0.5 * relationship_score_value;
}

double lending_propability(double score, const Bank& lender){
   double alpha;

   if(lender.type == BankType::Large){
      alpha = 0.4;
   }
   else{
      alpha = 1.0;
   }

   double beta = -1.0;

   return 1.0 / (1.0 + alpha * std::exp(beta * score));
}

double repayment_fraction(LoanType type){
   switch (type)
   {
   case LoanType::Overnight:
      return 1.0;
   
   case LoanType::ShortTerm:
      return 0.995;
   
   case LoanType::LongTerm:
      return 0.25;
   }
   return 1.0;
}

std::vector<Loan> build_interbank_market(std::vector<Bank>& banks,
double liquidty_target, double max_loan_amount, int max_loans_per_borrower){

   std::vector<Loan> loans;

   std::mt19937 gen(randgen());

   const int max_loans_per_borrower = 3;
   const double max_loan_amount = 1000.0;

   std::vector<LoanType> loan_types = {
      LoanType::Overnight, LoanType::ShortTerm, LoanType::LongTerm
   };

   for(LoanType type : loan_types){
      std::vector<int> lenders = find_lenders(banks, type);
      std::vector<int> borrowers = find_borrowers(banks, type);
   
      std::shuffle(lenders.begin(), lenders.end(), gen);
      std::shuffle(borrowers.begin(), borrowers.end(), gen);

      std::vector<double> lent_so_far(banks.size(), 0.0);

      for(int borrower_id: borrowers){
         double borrowing_need = borrowing_gap(banks[borrower_id], type);
         int loans_created = 0;

         for(int lender_id:lenders){
            if(borrowing_need <= 0.0){
               break;
            }

            if(loans_created >= max_loans_per_borrower){
               break;
            }

            if(lender_id == borrower_id) continue;

            double lending_capacity = lending_gap(banks[lender_id], type) - lent_so_far[lender_id];

            if(lending_capacity <= 0.0) continue;

            double amount = std::min(borrowing_need, lending_capacity);

            if(amount > max_loan_amount){
               amount = max_loan_amount;
            }

            if(amount <= 0.0) continue;

            Loan loan;
            loan.lender = lender_id;
            loan.borrower = borrower_id;
            loan.amount = amount;
            loan.payment_due = amount * repayment_fraction(type);
            loan.remaining = amount - loan.payment_due;
            loan.type = type;

            loans.push_back(loan);

            banks[lender_id].balanceSheet.cash -= amount;
            banks[lender_id].balanceSheet.assets +=  amount;

            banks[borrower_id].balanceSheet.cash += amount;
            banks[borrower_id].balanceSheet.assets +=   amount;
            
            update_equity(banks[lender_id]);
            update_equity(banks[borrower_id]);

            lent_so_far[lender_id] += amount;
            borrowing_need -= amount;
            loans_created ++;
         }
      }
   }   
   return loans;

}





