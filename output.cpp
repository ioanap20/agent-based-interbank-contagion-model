#include "output.hpp"
#include "market.hpp"
#include "contagion.hpp"
#include <vector>
#include <iostream>
#include <iomanip>
#include <string>
#include <algorithm>


static std::string loan_type_to_string(LoanType type){
    switch(type){
        case LoanType::Overnight:
            return "Overnight";

        case LoanType::ShortTerm:
            return "ShortTerm";

        case LoanType::LongTerm:
            return "LongTerm";
    }

    return "Unknown";
}

static std::string bank_type_to_string(BankType type){
    switch(type){
        case BankType::Large:
            return "Large";
        case BankType::Small:
        return "Small";
    }

    return "Unknown";
}

static std::string bank_role_to_string(BankRole role){
    switch (role)
    {
    case BankRole::Core:
    return "Core";
    case BankRole::Peripheral:
    return "Peripheral";
    }
    return "Unknown";
}

void print_separator(int width){
    std::cout << std::string(width, '-') << std::endl;
}

void print_benchmark_header(){
    print_separator();

    std::cout << std::left
                << std::setw(10) << "Banks"
                << std::setw(10) << "Loans"
                << std::setw(10) << "Shock"
                << std::setw(10) << "Threads"
                << std::setw(14) << "SeqDefault"
                << std::setw(14) << "ParDefault"
                << std::setw(16) << "SeqTime(ms)"
                << std::setw(16) << "ParTime(ms)"
                << std::setw(10) << "Speedup"
                << std::setw(10) << "Check"
                << std::endl;
    print_separator();
}

void print_benchmark_row(int numberOfBanks, int numberOfLoans, double shockPercentage, int numberOfThreads, 
                        int seqDefaults, int parDefaults, double seqTimeMs, double parTimeMs, double speedup){
    bool sameDefaults = seqDefaults == parDefaults;
    std::cout << std::left
              << std::setw(10) << numberOfBanks
              << std::setw(10) << numberOfLoans
              << std::setw(10) << std::fixed << std::setprecision(2) << shockPercentage
              << std::setw(10) << numberOfThreads
              << std::setw(14) << seqDefaults
              << std::setw(14) << parDefaults
              << std::setw(16) << std::fixed << std::setprecision(3) << seqTimeMs
              << std::setw(16) << std::fixed << std::setprecision(3) << parTimeMs
              << std::setw(10) << std::fixed << std::setprecision(3) << speedup
              << std::setw(10) << (sameDefaults ? "OK" : "DIFF")
              << std::endl;

}

void print_market_demo(const std::vector<Bank>& banksBeforeMarket, const std::vector<Bank>& banksAfterMarket, const std::vector<Loan>& loans){
    std::cout << std::endl;
    std::cout << "================ INTERBANK MARKET DEMO ================" << std::endl;

    std::cout << "Number of banks: " << banksAfterMarket.size() << std::endl;
    std::cout << "Number of loans created: " << loans.size() << std::endl;

    std::cout << std::endl;
    std::cout << "BANKS BEFORE MARKET FORMATION" << std::endl;
    print_separator(110);

    std::cout << std::left
              << std::setw(8)  << "Bank"
              << std::setw(12) << "Type"
              << std::setw(14) << "Role"
              << std::setw(14) << "Cash"
              << std::setw(14) << "Assets"
              << std::setw(14) << "Liabilities"
              << std::setw(14) << "Equity"
              << std::endl;

    print_separator(110);

    for(const Bank& bank : banksBeforeMarket){
        std::cout << std::left
                  << std::setw(8)  << bank.id
                  << std::setw(12) << bank_type_to_string(bank.type)
                  << std::setw(14) << bank_role_to_string(bank.role)
                  << std::setw(14) << std::fixed << std::setprecision(2) << bank.balanceSheet.cash
                  << std::setw(14) << std::fixed << std::setprecision(2) << bank.balanceSheet.assets
                  << std::setw(14) << std::fixed << std::setprecision(2) << bank.balanceSheet.liabilities
                  << std::setw(14) << std::fixed << std::setprecision(2) << bank.balanceSheet.equity
                  << std::endl;
    }

    print_separator(110);

    std::cout << std::endl;
    std::cout << "LOANS CREATED: WHO LENDS TO WHO" << std::endl;
    print_separator(110);

    std::cout << std::left
              << std::setw(8)  << "Loan"
              << std::setw(12) << "Lender"
              << std::setw(12) << "Borrower"
              << std::setw(14) << "Type"
              << std::setw(14) << "Amount"
              << std::setw(14) << "PaymentDue"
              << std::setw(14) << "Remaining"
              << std::endl;

    print_separator(110);

    int maxLoansToPrint = std::min(40, static_cast<int>(loans.size()));

    for(int i = 0; i < maxLoansToPrint; i++){
        const Loan& loan = loans[i];

        std::cout << std::left
                  << std::setw(8)  << i
                  << std::setw(12) << loan.lender
                  << std::setw(12) << loan.borrower
                  << std::setw(14) << loan_type_to_string(loan.type)
                  << std::setw(14) << std::fixed << std::setprecision(2) << loan.amount
                  << std::setw(14) << std::fixed << std::setprecision(2) << loan.payment_due
                  << std::setw(14) << std::fixed << std::setprecision(2) << loan.remaining
                  << std::endl;
    
    }
     if(loans.size() > maxLoansToPrint){
        std::cout << "... only first "
                  << maxLoansToPrint
                  << " loans shown out of "
                  << loans.size()
                  << std::endl;
    }

    print_separator(110);

    std::vector<double> totalLent(banksAfterMarket.size(), 0.0);
    std::vector<double> totalBorrowed(banksAfterMarket.size(), 0.0);

    std::vector<int> loansGiven(banksAfterMarket.size(), 0);
    std::vector<int> loansReceived(banksAfterMarket.size(), 0);

    for(const Loan& loan : loans){
        totalLent[loan.lender] += loan.amount;
        totalBorrowed[loan.borrower] += loan.amount;
        loansGiven[loan.lender]++;
        loansReceived[loan.borrower]++;
    }

    std::cout << std::endl;
    std::cout << "BANK-LEVEL MARKET SUMMARY" << std::endl;
    print_separator(120);

    std::cout << std::left
              << std::setw(8)  << "Bank"
              << std::setw(14) << "LoansOut"
              << std::setw(14) << "TotalLent"
              << std::setw(14) << "LoansIn"
              << std::setw(14) << "Borrowed"
              << std::setw(16) << "CashBefore"
              << std::setw(16) << "CashAfter"
              << std::setw(16) << "CashChange"
              << std::endl;

    print_separator(120);

    for(int i = 0; i < static_cast<int>(banksAfterMarket.size()); i++){
        if(loansGiven[i] == 0 && loansReceived[i] == 0){
            continue;
        }

        double cashBefore = banksBeforeMarket[i].balanceSheet.cash;
        double cashAfter = banksAfterMarket[i].balanceSheet.cash;
        double cashChange = cashAfter - cashBefore;

        std::cout << std::left
                  << std::setw(8)  << i
                  << std::setw(14) << loansGiven[i]
                  << std::setw(14) << std::fixed << std::setprecision(2) << totalLent[i]
                  << std::setw(14) << loansReceived[i]
                  << std::setw(14) << std::fixed << std::setprecision(2) << totalBorrowed[i]
                  << std::setw(16) << std::fixed << std::setprecision(2) << cashBefore
                  << std::setw(16) << std::fixed << std::setprecision(2) << cashAfter
                  << std::setw(16) << std::fixed << std::setprecision(2) << cashChange
                  << std::endl;
    }

    print_separator(120);

    std::cout << "Interpretation:" << std::endl;
    std::cout << "  - Lender -> Borrower means the lender gave liquidity to the borrower." << std::endl;
    std::cout << "  - LoansOut / TotalLent show how exposed a lender becomes." << std::endl;
    std::cout << "  - LoansIn / Borrowed show how dependent a borrower is on the market." << std::endl;
    std::cout << "  - This loan network is the channel through which defaults propagate later." << std::endl;
    std::cout << "=======================================================" << std::endl;

}

static int count_defaulted_banks_output(const std::vector<Bank>& banks){
    int count = 0;

    for(const Bank& bank : banks){
        if(bank.defaulted){
            count++;
        }
    }
    return count;
}

void print_small_market_intro(int numberOfBanks){
    std::cout << std::endl;
    std::cout << "================ SMALL MARKET VISUALIZATION ================" << std::endl;
    std::cout << "This run is not used for benchmarking." << std::endl;
    std::cout << "It is only meant to show how the interbank market works." << std::endl;
    std::cout << "Number of banks: " << numberOfBanks << std::endl;
    std::cout << "============================================================" << std::endl;
}

void print_no_loans_message(){
   std::cout << std::endl;
    std::cout << "No loans were created, so there is no contagion channel." << std::endl;
    std::cout << "Try increasing the number of banks from 12 to 15 or 20." << std::endl;
}

void print_external_shock_message(int shockedBank){
    std::cout << std::endl;
    std::cout << "================ EXTERNAL SHOCK ================" << std::endl;
    std::cout << "For the demo, bank " << shockedBank << " is shocked first." << std::endl;
    std::cout << "This bank was chosen because it received many loans." << std::endl;
    std::cout << "If this borrower defaults, its lenders suffer losses." << std::endl;
    std::cout << "================================================" << std::endl;
}

void print_final_bank_states(const std::vector<Bank>& banks){
    std::cout << std::endl;
    std::cout << "================ FINAL BANK STATES ================" << std::endl;

    print_separator(100);

    std::cout << std::left
              << std::setw(8)  << "Bank"
              << std::setw(14) << "Defaulted"
              << std::setw(16) << "Cash"
              << std::setw(16) << "Liabilities"
              << std::setw(16) << "Equity"
              << std::setw(16) << "Loss"
              << std::endl;

    print_separator(100);
    for(const Bank& bank : banks){
        std::cout << std::left
                  << std::setw(8)  << bank.id
                  << std::setw(14) << (bank.defaulted ? "YES" : "NO")
                  << std::setw(16) << std::fixed << std::setprecision(2) << bank.balanceSheet.cash
                  << std::setw(16) << std::fixed << std::setprecision(2) << bank.balanceSheet.liabilities
                  << std::setw(16) << std::fixed << std::setprecision(2) << bank.balanceSheet.equity
                  << std::setw(16) << std::fixed << std::setprecision(2) << bank.receivedLoss
                  << std::endl;
    }

    print_separator(100);
     std::cout << "Defaulted banks: "
              << count_defaulted_banks_output(banks)
              << " / "
              << banks.size()
              << std::endl;

    std::cout << "===================================================" << std::endl;
}
