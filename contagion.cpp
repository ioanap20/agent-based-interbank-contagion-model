/*
 * contagion.cpp
 *
 * Contagion cascade caused by bank defaults.
 *
 * When a bank defaults, its creditors recover only part of the money
 * they lent to it. The remaining unpaid exposure becomes a loss for the
 * creditor banks.
 *
 * These losses reduce the creditors' capital and trigger additional
 * defaults. The process continues iteratively until no new bank defaults.
 *
 * Functions:
 *  - propagating losses from defaulted borrowers to their creditors,
 *  - updating creditor banks' balance sheets,
 *  - identifying newly defaulted banks,
 *  - repeating contagion rounds until the system stabilizes.
 *
 * We use both:
 *  - a sequential version of the contagion algorithm,
 *  - a parallel version used for performance comparison.
 *
 */

 #include "contagion.hpp"

 #include <vector>

 static const double recovery_rate = 0.4;

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

 void propagate_losses(std::vector<Bank>& banks, const std::vector<Loan>& loans){
    for(const Loan& loan : loans){
        int borrower_id = loan.borrower;
        int lender_id = loan.lender;

        if(banks[borrower_id].defaulted){
            double loss = (1.0 - recovery_rate) * loan.payment_due;

            apply_loss(banks[lender_id], loss);
        }
    }
 }

 void run_contagion(std::vector<Bank>& banks, const std::vector<Loan>& loans){
    bool new_default = true;

    while(new_default){
        new_default = false;

        for(int i=0; i<banks.size(); i++){
            if(banks[i].defaulted){
                continue;
            }

            double outgoing_payment = total_outgoing_payment(i, loans);
            double incoming_payment = total_incoming_payment(i, loans);

            bool insolvent = is_insolvent(banks[i]);
            bool illiquid = is_illiquid(banks[i], outgoing_payment, incoming_payment);

            if(insolvent || illiquid){
                banks[i].defaulted = true;
                new_default = true;
            }
        }

        if(new_default){
            propagate_losses(banks, loans);
        }
    }
 }