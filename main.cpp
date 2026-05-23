/*
 * main.cpp
 *
 * One complete run of the interbank contagion model.
 *
 * Different stages of the simulation:
 *  1. initialize the bank agents,
 *  2. evaluate which banks have excess liquidity or shortages,
 *  3. form the interbank lending network,
 *  4. apply an external shock,
 *  5. simulate the default contagion cascade,
 *  6. collect and display the final results.
 *
 */

#include "banks.cpp"
#include "market.cpp"
#include "shock.cpp"
#include "contagion.cpp"

