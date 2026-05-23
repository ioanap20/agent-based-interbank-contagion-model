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

 #include <vector>
 #include <algorithm>

 struct Loan{
    int lender;
    int borrower;
    double amount;
 };

