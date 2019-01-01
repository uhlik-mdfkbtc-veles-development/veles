// Copyright (c) 2009-2017 The Bitcoin Core developers
// Copyright (c) 2018 FXTC developers
// Copyright (c) 2018 The Veles Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

//#include <base58.h>
#include <clientversion.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <core_io.h>
#include <key_io.h> // new
#include <keystore.h>
#include <policy/policy.h>
#include <policy/rbf.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/sign.h>
#include <univalue.h>
#include <util.h>
#include <utilmoneystr.h>
#include <utilstrencodings.h>
#include <memory>
#include <stdio.h>
#include <boost/algorithm/string.hpp>

#include "validation.h"

static std::map<std::string,UniValue> registers;
static const int CONTINUE_EXECUTION=-1;

// From validation.cpp
double ConvertBitsToDouble(unsigned int nBits);

static void SetupBlockTesterTxArgs()
{
    // Basic options
    gArgs.AddArg("-?", "This help message", false, OptionsCategory::OPTIONS);
    gArgs.AddArg("-subsidy", "Calculates subsidy amount for a block with specified parameters.", false, OptionsCategory::OPTIONS);
    SetupChainParamsBaseOptions();

    // Hidden
    gArgs.AddArg("-h", "", false, OptionsCategory::HIDDEN);
    gArgs.AddArg("-help", "", false, OptionsCategory::HIDDEN);
}

//
// This function returns either one of EXIT_ codes when it's expected to stop the process or
// CONTINUE_EXECUTION when it's expected to continue further.
//
static int AppInitBlockTester(int argc, char* argv[])
{
    std::string error;
    SetupBlockTesterTxArgs();
    
    if (!gArgs.ParseParameters(argc, argv, error)) {
        fprintf(stderr, "Error parsing command line arguments: %s\n", error.c_str());
        return EXIT_FAILURE;
    }

    // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
    try {
        SelectParams(gArgs.GetChainName());
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return EXIT_FAILURE;
    }

    if (argc < 2 || HelpRequested(gArgs)) {
        // First part of help message is specific to this utility
        std::string strUsage = PACKAGE_NAME " veles-testblock utility version " + FormatFullVersion() + "\n\n" +
            "Usage:  veles-testblock [options] -subsidy [height] [bits]\n" +
            "\n";
        strUsage += gArgs.GetHelpMessage();

        fprintf(stdout, "%s", strUsage.c_str());

        if (argc < 2) {
            fprintf(stderr, "Error: too few parameters\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    return CONTINUE_EXECUTION;
}

CAmount TesterGetBlockSubsidy(int nHeight, int nBits)
{
    CBlockHeader *header = new CBlockHeader();
    header->nBits = (uint32_t) nBits;
   // const CChainParams& chainparams = Params();

    return GetBlockSubsidy(nHeight, *header, Params().GetConsensus());
}

static int CommandLineRawTx(int argc, char* argv[])
{
    while (argc > 1 && IsSwitchChar(argv[1][0])) {
        argc--;
        argv++;
    }
    std::vector<std::string> args = std::vector<std::string>(&argv[1], &argv[argc]);

    if (gArgs.GetBoolArg("-subsidy", false)) {
        fprintf(stdout, "Calculating subsidy ...\n");

        if (args.size() < 2) {
            throw std::runtime_error("too few parameters (need at least block height and number of bits)");
        }
        fprintf(stdout, "In: %s %s\n", args[0].c_str(), args[1].c_str());
        fprintf(stdout, "Out: %.8f\n", (double) TesterGetBlockSubsidy(std::stoi(args[0]), std::stoi(args[1])) / COIN);

    } else {
        throw std::runtime_error("too few parameters"); // we shouldn't be here
    }

    return EXIT_FAILURE;
}

int main(int argc, char* argv[])
{
    SetupEnvironment();

    try {
        int ret = AppInitBlockTester(argc, argv);
        if (ret != CONTINUE_EXECUTION)
            return ret;
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInitBlockTester()");
        return EXIT_FAILURE;
    } catch (...) {
        PrintExceptionContinue(nullptr, "AppInitBlockTester()");
        return EXIT_FAILURE;
    }

    int ret = EXIT_FAILURE;
    try {
        ret = CommandLineRawTx(argc, argv);
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "CommandLineRawTx()");
    } catch (...) {
        PrintExceptionContinue(nullptr, "CommandLineRawTx()");
    }
    return ret;
}
