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