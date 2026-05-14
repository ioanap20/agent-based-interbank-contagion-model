/*
 * shock.cpp
 *
 * External shock applied to the financial system.
 *
 * A shock reduces the assets or capital of one or more selected banks.
 * Depending on its intensity, the shock may cause some banks to default.
 *
 * Functions:
 *  - selecting the initially shocked banks,
 *  - reducing their balance-sheet quantities,
 *  - detecting banks that fail directly because of the shock,
 *  - returning the initial list of defaulted banks that will start
 *    the contagion cascade.
 *
 * Possible shock scenarios:
 *  - shocking a random bank,
 *  - shocking the largest bank,
 *  - shocking a core bank,
 *  - shocking several banks simultaneously.
 */