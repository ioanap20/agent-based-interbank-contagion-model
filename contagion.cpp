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