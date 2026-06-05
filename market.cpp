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

// Uniform distribution function
static double get_uniform_random(double lower_bound, double upper_bound) {
   static std::random_device rd;
   static std::mt19937 gen(rd());
   std::uniform_real_distribution<double> dis(lower_bound, upper_bound);
   return dis(gen);
}

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

void initialize_market_memory(std::vector<Bank>& banks) {
   size_t total_banks = banks.size();
   for (auto& bank : banks) {
      //size table mapping to look up 3 independent loan timelines across all banks
      bank.relationship_scores.assign(total_banks, std::vector<double>(3, 0.0));
   }
}

void apply_relationship_decay(std::vector<Bank>& banks) {
   const double eta = 0.9; //from paper
   for (auto& bank : banks) {
      for (auto& counterparty_row : bank.relationship_scores) {
         for (auto& score: counterparty_row) {
            score *= eta;
         }
      }
   }
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

//equation 7
double size_score(const Bank& borrower, const Bank& lender){
   double lender_assets = std::max(total_assets(lender), 1.0);
   double baseline;

   if (borrower.count_past_counterparties>0) {
      baseline = borrower.sum_log_assets_past_counterparties / borrower.count_past_counterparties;
   } else {
      baseline = std::log(std::max(total_assets(borrower), 1.0));
   }
   return std::log(lender_assets) - baseline;
}
//pulls historical record
double relationship_score(const Bank& borrower, int lender_id, LoanType type) {
   int t_idx = loan_type_index(type);
   return borrower.relationship_scores[lender_id][t_idx];
}

double combined_score(double size_score_value, double relationship_score_value){
   return 0.5 * size_score_value + 0.5 * relationship_score_value;
}

double lending_probability(double score, const Bank& lender){
   double alpha = (lender.type == BankType::Large)? 0.4 : 1.0; //from paper
   double beta = -1.0; //base policy tightness baseline

   return 1.0 / (1.0 + alpha * std::exp(beta * score));
}

double repayment_fraction(LoanType type){
   switch (type)
   {
   case LoanType::Overnight:
      return get_uniform_random(1.001, 1.005);
   
   case LoanType::ShortTerm:
      return get_uniform_random(1.01, 1.03);
   
   case LoanType::LongTerm:
      return get_uniform_random(1.03, 1.08);
   }
   return 1.0;
}

struct PriorityCandidate {
   int id;
   double score;
   bool is_large_core;
   double rel_history;
};

std::vector<Loan> build_interbank_market(std::vector<Bank>& banks){
   std::vector<Loan> loans;

   std::random_device rd;
   std::mt19937 gen(rd());

   const int max_loans_per_borrower = 8;
   const double max_loan_amount = 100000.0;

   // Prevents one lender from giving away almost all its cash in one loan
   const double max_fraction_of_lender_cash = 0.90;

   const double max_fraction_of_lender_equity = 6.00;

   double min_loan_amount = 5000.0;

   std::vector<LoanType> loan_types = {
      LoanType::Overnight, LoanType::ShortTerm, LoanType::LongTerm
   };

   for(LoanType type : loan_types){
      int t_idx = loan_type_index(type);      
      std::vector<int> lenders = find_lenders(banks, type);
      std::vector<int> borrowers = find_borrowers(banks, type);
   
      std::shuffle(borrowers.begin(), borrowers.end(), gen);

      std::vector<double> lent_so_far(banks.size(), 0.0);

      for(int borrower_id: borrowers){
         double borrowing_need = borrowing_gap(banks[borrower_id], type);
         int loans_created = 0;

         std::vector<PriorityCandidate> preference_queue;

         for(int lender_id:lenders){
            if (lender_id == borrower_id) continue;

            double s_size = size_score(banks[borrower_id], banks[lender_id]);
            double s_rel = relationship_score(banks[borrower_id], lender_id, type);
            double c_score = combined_score(s_size, s_rel);

            preference_queue.push_back({
               lender_id,
               c_score,
               (banks[lender_id].type==BankType::Large),
               s_rel
            });
         }

         std::sort(preference_queue.begin(), preference_queue.end(), [](const PriorityCandidate& a, const PriorityCandidate& b) {
            //large banks first
            if (a.is_large_core != b.is_large_core) return a.is_large_core > b.is_large_core;
            //small banks with existing relationship
            if (!a.is_large_core && ((a.rel_history>0.0) != (b.rel_history>0.0))) {
               return (a.rel_history>0.0)>(b.rel_history>0.0);
            }
            return a.score>b.score;
         });

         for (const auto& candidate:preference_queue) {
            int lender_id = candidate.id;

            if(borrowing_need<=0.0 || loans_created>=max_loans_per_borrower){
               break;
            }

            double lending_capacity = lending_gap(banks[lender_id], type) - lent_so_far[lender_id];
            if(lending_capacity<=0.0) continue;

            double willingness_fraction = get_uniform_random(0.85, 1.0);
            double lender_willingness = lending_capacity*willingness_fraction;

            double roll=get_uniform_random(0.0, 1.0);
            double p_accept = lending_probability(candidate.score, banks[lender_id]);

            
            if(roll > p_accept) continue;

            double cash_limit = max_fraction_of_lender_cash * banks[lender_id].balanceSheet.cash;

            double equity_limit = max_fraction_of_lender_equity * std::max(banks[lender_id].balanceSheet.equity, 1.0);
            
            double amount = 3.0 * std::min({borrowing_need, lender_willingness, max_loan_amount, equity_limit});
            if(amount<=min_loan_amount) continue;

            Loan loan;
            loan.lender = lender_id;
            loan.borrower = borrower_id;
            loan.amount = amount;

            loan.payment_due = amount * repayment_fraction(type);

            //loan.remaining = amount - loan.payment_due;
            loan.remaining = 0.0;

            loan.type = type;

            loans.push_back(loan);

            banks[lender_id].balanceSheet.cash -= amount;
            banks[lender_id].balanceSheet.assets += amount;

            banks[borrower_id].balanceSheet.cash += amount;
            banks[borrower_id].balanceSheet.liabilities += amount;
            
            update_equity(banks[lender_id]);
            update_equity(banks[borrower_id]);

            //equation 8
            banks[borrower_id].relationship_scores[lender_id][t_idx] = std::log(amount+1.0);

            //equation 7
            banks[borrower_id].sum_log_assets_past_counterparties+=std::log(total_assets(banks[lender_id])+1.0);
            banks[borrower_id].count_past_counterparties++;

            lent_so_far[lender_id] += amount;
            borrowing_need -= amount;
            loans_created ++;
         }
      }
   }   
   return loans;

}

LoanIndex build_loan_index(const std::vector<Loan>& loans, int num_banks){
    LoanIndex index;

    index.by_borrower.assign(num_banks, {}); // at the begining 
    index.outgoing_payment.assign(num_banks, 0.0);
    index.incoming_payment.assign(num_banks, 0.0);

    for(int loan_idx = 0; loan_idx < loans.size(); loan_idx ++){
        const Loan& loan = loans[loan_idx];
        index.by_borrower[loan.borrower].push_back(loan_idx); //stores a list of loans where that bank borrowed money from
        index.outgoing_payment[loan.borrower] += loan.payment_due; //how much that bank has to repay to others
        index.incoming_payment[loan.lender] += loan.payment_due; // how much that bank expects back
    }
    return index;
}

double total_outgoing_payment(int bank_id, const std::vector<Loan>& loans){
   double total = 0.0;

   for(const Loan& loan : loans){
      if(loan.borrower == bank_id){
         total += loan.payment_due;
      }
   }

   return total;
}

double total_incoming_payment(int bank_id, const std::vector<Loan>& loans){
   double total = 0.0;

   for(const Loan& loan : loans){
      if(loan.lender == bank_id){
         total += loan.payment_due;
      }
   }

   return total;
}





