// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018 FXTC developers
// Copyright (c) 2018-2019 The Veles Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//#define ENABLE_DASH_DEBUG

#include <activemasternode.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <governance.h>
#include <governance-vote.h>
#include <governance-classes.h>
#include <governance-validators.h>
#include <init.h>
#include <validation.h>
#include <masternode.h>
#include <masternode-sync.h>
#include <masternodeconfig.h>
#include <masternodeman.h>
#include <messagesigner.h>
#include <primitives/block.h>
// FXTC BEGIN
#include <rpc/util.h>
// FXTC END
#include <rpc/server.h>
#include <util/system.h>
#include <util/moneystr.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET
// FXTC BEGIN
#include <wallet/rpcwallet.h>
// FXTC END

#include <boost/lexical_cast.hpp>

UniValue gobject(const JSONRPCRequest& request)
{
#ifdef ENABLE_WALLET
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
#endif // ENABLE_WALLET

    std::string strCommand;
    if (request.params.size() >= 1)
        strCommand = request.params[0].get_str();

    if (request.fHelp  ||
        (
#ifdef ENABLE_WALLET
         strCommand != "prepare" &&
#endif // ENABLE_WALLET
         strCommand != "vote-many" && strCommand != "vote-conf" && strCommand != "vote-alias" && strCommand != "submit" && strCommand != "count" &&
         strCommand != "deserialize" && strCommand != "get" && strCommand != "getvotes" && strCommand != "getcurrentvotes" && strCommand != "list" && strCommand != "diff" &&
         strCommand != "check" ))
        throw std::runtime_error(
            RPCHelpMan{"gobject",
                "\nManage governance objects\n",
                {
                    {"command", RPCArg::Type::STR, RPCArg::Optional::NO, "Command must be one of:\n"
            "       \"check\"              - Validate governance object data (proposal only)\n"
#ifdef ENABLE_WALLET
            "       \"prepare\"            - Prepare governance object by signing and creating tx\n"
#endif // ENABLE_WALLET
            "       \"submit\"             - Submit governance object to network\n"
            "       \"deserialize\"        - Deserialize governance object from hex string to JSON\n"
            "       \"count\"              - Count governance objects and votes\n"
            "       \"get\"                - Get governance object by hash\n"
            "       \"getvotes\"           - Get all votes for a governance object hash (including old votes)\n"
            "       \"getcurrentvotes\"    - Get only current (tallying) votes for a governance object hash (does not include old votes)\n"
            "       \"list\"               - List governance objects (can be filtered by signal and/or object type)\n"
            "       \"diff\"               - List differences since last diff\n"
            "       \"vote-alias\"         - Vote on a governance object by masternode alias (using masternode.conf setup)\n"
            "       \"vote-conf\"          - Vote on a governance object by masternode configured in dash.conf\n"
            "       \"vote-many\"          - Vote on a governance object by all masternodes (using masternode.conf setup)"},
                },
                RPCResult{
            "Not documented by Dash developers.\n"
                },
                RPCExamples{
                    HelpExampleCli("gobject", "count")
            + HelpExampleRpc("gobject", "\"count\"")
                },
            }.ToString());


    if(strCommand == "count")
        return governance.ToString();
    /*
        ------ Example Governance Item ------

        gobject submit 6e622bb41bad1fb18e7f23ae96770aeb33129e18bd9efe790522488e580a0a03 0 1 1464292854 "beer-reimbursement" 5b5b22636f6e7472616374222c207b2270726f6a6563745f6e616d65223a20225c22626565722d7265696d62757273656d656e745c22222c20227061796d656e745f61646472657373223a20225c225879324c4b4a4a64655178657948726e34744744514238626a6876464564615576375c22222c2022656e645f64617465223a202231343936333030343030222c20226465736372697074696f6e5f75726c223a20225c227777772e646173687768616c652e6f72672f702f626565722d7265696d62757273656d656e745c22222c2022636f6e74726163745f75726c223a20225c22626565722d7265696d62757273656d656e742e636f6d2f3030312e7064665c22222c20227061796d656e745f616d6f756e74223a20223233342e323334323232222c2022676f7665726e616e63655f6f626a6563745f6964223a2037342c202273746172745f64617465223a202231343833323534303030227d5d5d1
    */

    // DEBUG : TEST DESERIALIZATION OF GOVERNANCE META DATA
    if(strCommand == "deserialize")
    {
        if (request.params.size() != 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gobject deserialize <data-hex>'");
        }

        std::string strHex = request.params[1].get_str();

        std::vector<unsigned char> v = ParseHex(strHex);
        std::string s(v.begin(), v.end());

        UniValue u(UniValue::VOBJ);
        u.read(s);

        return u.write().c_str();
    }

    // VALIDATE A GOVERNANCE OBJECT PRIOR TO SUBMISSION
    if(strCommand == "check")
    {
        if (request.params.size() != 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gobject check <data-hex>'");
        }

        // ASSEMBLE NEW GOVERNANCE OBJECT FROM USER PARAMETERS

        uint256 hashParent;

        int nRevision = 1;

        int64_t nTime = GetAdjustedTime();
        std::string strData = request.params[1].get_str();

        CGovernanceObject govobj(hashParent, nRevision, nTime, uint256(), strData);

        if(govobj.GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
            CProposalValidator validator(strData);
            if(!validator.Validate())  {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid proposal data, error messages:" + validator.GetErrorMessages());
            }
        }
        else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid object type, only proposals can be validated");
        }

        UniValue objResult(UniValue::VOBJ);

        objResult.pushKV("Object status", "OK");

        return objResult;
    }

#ifdef ENABLE_WALLET
    // PREPARE THE GOVERNANCE OBJECT BY CREATING A COLLATERAL TRANSACTION
    if(strCommand == "prepare")
    {
        if (request.params.size() != 5) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gobject prepare <parent-hash> <revision> <time> <data-hex>'");
        }

        // ASSEMBLE NEW GOVERNANCE OBJECT FROM USER PARAMETERS

        uint256 hashParent;

        // -- attach to root node (root node doesn't really exist, but has a hash of zero)
        if(request.params[1].get_str() == "0") {
            hashParent = uint256();
        } else {
            hashParent = ParseHashV(request.params[1], "fee-txid, parameter 1");
        }

        std::string strRevision = request.params[2].get_str();
        std::string strTime = request.params[3].get_str();
        int nRevision = boost::lexical_cast<int>(strRevision);
        int nTime = boost::lexical_cast<int>(strTime);
        std::string strData = request.params[4].get_str();

        // CREATE A NEW COLLATERAL TRANSACTION FOR THIS SPECIFIC OBJECT

        CGovernanceObject govobj(hashParent, nRevision, nTime, uint256(), strData);

        if(govobj.GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
            CProposalValidator validator(strData);
            if(!validator.Validate())  {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid proposal data, error messages:" + validator.GetErrorMessages());
            }
        }

        if((govobj.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER) ||
           (govobj.GetObjectType() == GOVERNANCE_OBJECT_WATCHDOG)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Trigger and watchdog objects need not be prepared (however only masternodes can create them)");
        }

        {
            LOCK(cs_main);
            std::string strError = "";
            if(!govobj.IsValidLocally(strError, false))
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Governance object is not valid - " + govobj.GetHash().ToString() + " - " + strError);
        }

        CTransactionRef tx_new;
        if(!pwallet->GetBudgetSystemCollateralTX(tx_new, govobj.GetHash(), govobj.GetMinCollateralFee(), false)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Error making collateral transaction for governance object. Please check your wallet balance and make sure your wallet is unlocked.");
        }

        // -- make our change address
        CReserveKey reservekey(pwallet);
        // -- send the tx to the network
        CValidationState state;
        mapValue_t mapValue;
        // FXTC BEGIN
        //pwallet->CommitTransaction(tx_new, std::move(mapValue), {} /* orderForm */, {} /* fromAccount */, reservekey, g_connman.get(), state, NetMsgType::TX);
        pwallet->CommitTransaction(tx_new, std::move(mapValue), {} /* orderForm */, reservekey, g_connman.get(), state, NetMsgType::TX);
        // FXTC END

        DBG( cout << "gobject: prepare "
             << " strData = " << govobj.GetDataAsString()
             << ", hash = " << govobj.GetHash().GetHex()
             << ", txidFee = " << tx_new->GetHash().GetHex()
             << endl; );

        return tx_new->GetHash().ToString();
    }
#endif // ENABLE_WALLET

    // AFTER COLLATERAL TRANSACTION HAS MATURED USER CAN SUBMIT GOVERNANCE OBJECT TO PROPAGATE NETWORK
    if(strCommand == "submit")
    {
        if ((request.params.size() < 5) || (request.params.size() > 6))  {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gobject submit <parent-hash> <revision> <time> <data-hex> <fee-txid>'");
        }

        if(!masternodeSync.IsBlockchainSynced()) {
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Must wait for client to sync with masternode network. Try again in a minute or so.");
        }

        bool fMnFound = mnodeman.Has(activeMasternode.outpoint);

        DBG( cout << "gobject: submit activeMasternode.pubKeyMasternode = " << activeMasternode.pubKeyMasternode.GetHash().ToString()
             << ", outpoint = " << activeMasternode.outpoint.ToStringShort()
             << ", params.size() = " << request.params.size()
             << ", fMnFound = " << fMnFound << endl; );

        // ASSEMBLE NEW GOVERNANCE OBJECT FROM USER PARAMETERS

        uint256 txidFee;

        if(request.params.size() == 6) {
            txidFee = ParseHashV(request.params[5], "fee-txid, parameter 6");
        }
        uint256 hashParent;
        if(request.params[1].get_str() == "0") { // attach to root node (root node doesn't really exist, but has a hash of zero)
            hashParent = uint256();
        } else {
            hashParent = ParseHashV(request.params[1], "parent object hash, parameter 2");
        }

        // GET THE PARAMETERS FROM USER

        std::string strRevision = request.params[2].get_str();
        std::string strTime = request.params[3].get_str();
        int nRevision = boost::lexical_cast<int>(strRevision);
        int nTime = boost::lexical_cast<int>(strTime);
        std::string strData = request.params[4].get_str();

        CGovernanceObject govobj(hashParent, nRevision, nTime, txidFee, strData);

        DBG( cout << "gobject: submit "
             << " strData = " << govobj.GetDataAsString()
             << ", hash = " << govobj.GetHash().GetHex()
             << ", txidFee = " << txidFee.GetHex()
             << endl; );

        if(govobj.GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
            CProposalValidator validator(strData);
            if(!validator.Validate())  {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid proposal data, error messages:" + validator.GetErrorMessages());
            }
        }

        // Attempt to sign triggers if we are a MN
        if((govobj.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER) ||
           (govobj.GetObjectType() == GOVERNANCE_OBJECT_WATCHDOG)) {
            if(fMnFound) {
                govobj.SetMasternodeVin(activeMasternode.outpoint);
                govobj.Sign(activeMasternode.keyMasternode, activeMasternode.pubKeyMasternode);
            }
            else {
                LogPrintf("gobject(submit) -- Object submission rejected because node is not a masternode\n");
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Only valid masternodes can submit this type of object");
            }
        }
        else {
            if(request.params.size() != 6) {
                LogPrintf("gobject(submit) -- Object submission rejected because fee tx not provided\n");
                throw JSONRPCError(RPC_INVALID_PARAMETER, "The fee-txid parameter must be included to submit this type of object");
            }
        }

        std::string strHash = govobj.GetHash().ToString();

        std::string strError = "";
        bool fMissingMasternode;
        bool fMissingConfirmations;
        {
            LOCK(cs_main);
            if(!govobj.IsValidLocally(strError, fMissingMasternode, fMissingConfirmations, true) && !fMissingConfirmations) {
                LogPrintf("gobject(submit) -- Object submission rejected because object is not valid - hash = %s, strError = %s\n", strHash, strError);
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Governance object is not valid - " + strHash + " - " + strError);
            }
        }

        // RELAY THIS OBJECT
        // Reject if rate check fails but don't update buffer
        if(!governance.MasternodeRateCheck(govobj)) {
            LogPrintf("gobject(submit) -- Object submission rejected because of rate check failure - hash = %s\n", strHash);
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Object creation rate limit exceeded");
        }

        LogPrintf("gobject(submit) -- Adding locally created governance object - %s\n", strHash);

        if(fMissingConfirmations) {
            governance.AddPostponedObject(govobj);
            govobj.Relay(*g_connman);
        } else {
            governance.AddGovernanceObject(govobj, *g_connman);
        }

        return govobj.GetHash().ToString();
    }

    if(strCommand == "vote-conf")
    {
        if(request.params.size() != 4)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gobject vote-conf <governance-hash> [funding|valid|delete] [yes|no|abstain]'");

        uint256 hash;
        std::string strVote;

        hash = ParseHashV(request.params[1], "Object hash");
        std::string strVoteSignal = request.params[2].get_str();
        std::string strVoteOutcome = request.params[3].get_str();

        vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(strVoteSignal);
        if(eVoteSignal == VOTE_SIGNAL_NONE) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Invalid vote signal. Please using one of the following: "
                               "(funding|valid|delete|endorsed) OR `custom sentinel code` ");
        }

        vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(strVoteOutcome);
        if(eVoteOutcome == VOTE_OUTCOME_NONE) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid vote outcome. Please use one of the following: 'yes', 'no' or 'abstain'");
        }

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);

        std::vector<unsigned char> vchMasterNodeSignature;
        std::string strMasterNodeSignMessage;

        UniValue statusObj(UniValue::VOBJ);
        UniValue returnObj(UniValue::VOBJ);

        CMasternode mn;
        bool fMnFound = mnodeman.Get(activeMasternode.outpoint, mn);

        if(!fMnFound) {
            nFailed++;
            statusObj.pushKV("result", "failed");
            statusObj.pushKV("errorMessage", "Can't find masternode by collateral output");
            resultsObj.pushKV("dash.conf", statusObj);
            returnObj.pushKV("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", nSuccessful, nFailed));
            returnObj.pushKV("detail", resultsObj);
            return returnObj;
        }

        CGovernanceVote vote(mn.vin.prevout, hash, eVoteSignal, eVoteOutcome);
        if(!vote.Sign(activeMasternode.keyMasternode, activeMasternode.pubKeyMasternode)) {
            nFailed++;
            statusObj.pushKV("result", "failed");
            statusObj.pushKV("errorMessage", "Failure to sign.");
            resultsObj.pushKV("dash.conf", statusObj);
            returnObj.pushKV("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", nSuccessful, nFailed));
            returnObj.pushKV("detail", resultsObj);
            return returnObj;
        }

        CGovernanceException exception;
        if(governance.ProcessVoteAndRelay(vote, exception, *g_connman)) {
            nSuccessful++;
            statusObj.pushKV("result", "success");
        }
        else {
            nFailed++;
            statusObj.pushKV("result", "failed");
            statusObj.pushKV("errorMessage", exception.GetMessage());
        }

        resultsObj.pushKV("dash.conf", statusObj);

        returnObj.pushKV("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", nSuccessful, nFailed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }

    if(strCommand == "vote-many")
    {
        if(request.params.size() != 4)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gobject vote-many <governance-hash> [funding|valid|delete] [yes|no|abstain]'");

        uint256 hash;
        std::string strVote;

        hash = ParseHashV(request.params[1], "Object hash");
        std::string strVoteSignal = request.params[2].get_str();
        std::string strVoteOutcome = request.params[3].get_str();


        vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(strVoteSignal);
        if(eVoteSignal == VOTE_SIGNAL_NONE) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Invalid vote signal. Please using one of the following: "
                               "(funding|valid|delete|endorsed) OR `custom sentinel code` ");
        }

        vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(strVoteOutcome);
        if(eVoteOutcome == VOTE_OUTCOME_NONE) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid vote outcome. Please use one of the following: 'yes', 'no' or 'abstain'");
        }

        int nSuccessful = 0;
        int nFailed = 0;

        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();

        UniValue resultsObj(UniValue::VOBJ);

        for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
            std::string strError;
            std::vector<unsigned char> vchMasterNodeSignature;
            std::string strMasterNodeSignMessage;

            CPubKey pubKeyCollateralAddress;
            CKey keyCollateralAddress;
            CPubKey pubKeyMasternode;
            CKey keyMasternode;

            UniValue statusObj(UniValue::VOBJ);

            if(!CMessageSigner::GetKeysFromSecret(mne.getPrivKey(), keyMasternode, pubKeyMasternode)){
                nFailed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("errorMessage", "Masternode signing error, could not set key correctly");
                resultsObj.pushKV(mne.getAlias(), statusObj);
                continue;
            }

            uint256 nTxHash;
            nTxHash.SetHex(mne.getTxHash());

            int nOutputIndex = 0;
            if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
                continue;
            }

            COutPoint outpoint(nTxHash, nOutputIndex);

            CMasternode mn;
            bool fMnFound = mnodeman.Get(outpoint, mn);

            if(!fMnFound) {
                nFailed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("errorMessage", "Can't find masternode by collateral output");
                resultsObj.pushKV(mne.getAlias(), statusObj);
                continue;
            }

            CGovernanceVote vote(mn.vin.prevout, hash, eVoteSignal, eVoteOutcome);
            if(!vote.Sign(keyMasternode, pubKeyMasternode)){
                nFailed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("errorMessage", "Failure to sign.");
                resultsObj.pushKV(mne.getAlias(), statusObj);
                continue;
            }

            CGovernanceException exception;
            if(governance.ProcessVoteAndRelay(vote, exception, *g_connman)) {
                nSuccessful++;
                statusObj.pushKV("result", "success");
            }
            else {
                nFailed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("errorMessage", exception.GetMessage());
            }

            resultsObj.pushKV(mne.getAlias(), statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", nSuccessful, nFailed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }


    // MASTERNODES CAN VOTE ON GOVERNANCE OBJECTS ON THE NETWORK FOR VARIOUS SIGNALS AND OUTCOMES
    if(strCommand == "vote-alias")
    {
        if(request.params.size() != 5)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gobject vote-alias <governance-hash> [funding|valid|delete] [yes|no|abstain] <alias-name>'");

        uint256 hash;
        std::string strVote;

        // COLLECT NEEDED PARAMETRS FROM USER

        hash = ParseHashV(request.params[1], "Object hash");
        std::string strVoteSignal = request.params[2].get_str();
        std::string strVoteOutcome = request.params[3].get_str();
        std::string strAlias = request.params[4].get_str();

        // CONVERT NAMED SIGNAL/ACTION AND CONVERT

        vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(strVoteSignal);
        if(eVoteSignal == VOTE_SIGNAL_NONE) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Invalid vote signal. Please using one of the following: "
                               "(funding|valid|delete|endorsed) OR `custom sentinel code` ");
        }

        vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(strVoteOutcome);
        if(eVoteOutcome == VOTE_OUTCOME_NONE) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid vote outcome. Please use one of the following: 'yes', 'no' or 'abstain'");
        }

        // EXECUTE VOTE FOR EACH MASTERNODE, COUNT SUCCESSES VS FAILURES

        int nSuccessful = 0;
        int nFailed = 0;

        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();

        UniValue resultsObj(UniValue::VOBJ);

        for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries())
        {
            // IF WE HAVE A SPECIFIC NODE REQUESTED TO VOTE, DO THAT
            if(strAlias != mne.getAlias()) continue;

            // INIT OUR NEEDED VARIABLES TO EXECUTE THE VOTE
            std::string strError;
            std::vector<unsigned char> vchMasterNodeSignature;
            std::string strMasterNodeSignMessage;

            CPubKey pubKeyCollateralAddress;
            CKey keyCollateralAddress;
            CPubKey pubKeyMasternode;
            CKey keyMasternode;

            // SETUP THE SIGNING KEY FROM MASTERNODE.CONF ENTRY

            UniValue statusObj(UniValue::VOBJ);

            if(!CMessageSigner::GetKeysFromSecret(mne.getPrivKey(), keyMasternode, pubKeyMasternode)) {
                nFailed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("errorMessage", strprintf("Invalid masternode key %s.", mne.getPrivKey()));
                resultsObj.pushKV(mne.getAlias(), statusObj);
                continue;
            }

            // SEARCH FOR THIS MASTERNODE ON THE NETWORK, THE NODE MUST BE ACTIVE TO VOTE

            uint256 nTxHash;
            nTxHash.SetHex(mne.getTxHash());

            int nOutputIndex = 0;
            if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
                continue;
            }

            COutPoint outpoint(nTxHash, nOutputIndex);

            CMasternode mn;
            bool fMnFound = mnodeman.Get(outpoint, mn);

            if(!fMnFound) {
                nFailed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("errorMessage", "Masternode must be publically available on network to vote. Masternode not found.");
                resultsObj.pushKV(mne.getAlias(), statusObj);
                continue;
            }

            // CREATE NEW GOVERNANCE OBJECT VOTE WITH OUTCOME/SIGNAL

            CGovernanceVote vote(outpoint, hash, eVoteSignal, eVoteOutcome);
            if(!vote.Sign(keyMasternode, pubKeyMasternode)) {
                nFailed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("errorMessage", "Failure to sign.");
                resultsObj.pushKV(mne.getAlias(), statusObj);
                continue;
            }

            // UPDATE LOCAL DATABASE WITH NEW OBJECT SETTINGS

            CGovernanceException exception;
            if(governance.ProcessVoteAndRelay(vote, exception, *g_connman)) {
                nSuccessful++;
                statusObj.pushKV("result", "success");
            }
            else {
                nFailed++;
                statusObj.pushKV("result", "failed");
                statusObj.pushKV("errorMessage", exception.GetMessage());
            }

            resultsObj.pushKV(mne.getAlias(), statusObj);
        }

        // REPORT STATS TO THE USER

        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", nSuccessful, nFailed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }

    // USERS CAN QUERY THE SYSTEM FOR A LIST OF VARIOUS GOVERNANCE ITEMS
    if(strCommand == "list" || strCommand == "diff")
    {
        if (request.params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gobject [list|diff] ( signal type )'");

        // GET MAIN PARAMETER FOR THIS MODE, VALID OR ALL?

        std::string strCachedSignal = "valid";
        if (request.params.size() >= 2) strCachedSignal = request.params[1].get_str();
        if (strCachedSignal != "valid" && strCachedSignal != "funding" && strCachedSignal != "delete" && strCachedSignal != "endorsed" && strCachedSignal != "all")
            return "Invalid signal, should be 'valid', 'funding', 'delete', 'endorsed' or 'all'";

        std::string strType = "all";
        if (request.params.size() == 3) strType = request.params[2].get_str();
        if (strType != "proposals" && strType != "triggers" && strType != "watchdogs" && strType != "all")
            return "Invalid type, should be 'proposals', 'triggers', 'watchdogs' or 'all'";

        // GET STARTING TIME TO QUERY SYSTEM WITH

        int nStartTime = 0; //list
        if(strCommand == "diff") nStartTime = governance.GetLastDiffTime();

        // SETUP BLOCK INDEX VARIABLE / RESULTS VARIABLE

        UniValue objResult(UniValue::VOBJ);

        // GET MATCHING GOVERNANCE OBJECTS

        LOCK2(cs_main, governance.cs);

        std::vector<CGovernanceObject*> objs = governance.GetAllNewerThan(nStartTime);
        governance.UpdateLastDiffTime(GetTime());

        // CREATE RESULTS FOR USER

        for (CGovernanceObject* pGovObj : objs)
        {
            if(strCachedSignal == "valid" && !pGovObj->IsSetCachedValid()) continue;
            if(strCachedSignal == "funding" && !pGovObj->IsSetCachedFunding()) continue;
            if(strCachedSignal == "delete" && !pGovObj->IsSetCachedDelete()) continue;
            if(strCachedSignal == "endorsed" && !pGovObj->IsSetCachedEndorsed()) continue;

            if(strType == "proposals" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
            if(strType == "triggers" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;
            if(strType == "watchdogs" && pGovObj->GetObjectType() != GOVERNANCE_OBJECT_WATCHDOG) continue;

            UniValue bObj(UniValue::VOBJ);
            bObj.pushKV("DataHex",  pGovObj->GetDataAsHex());
            bObj.pushKV("DataString",  pGovObj->GetDataAsString());
            bObj.pushKV("Hash",  pGovObj->GetHash().ToString());
            bObj.pushKV("CollateralHash",  pGovObj->GetCollateralHash().ToString());
            bObj.pushKV("ObjectType", pGovObj->GetObjectType());
            bObj.pushKV("CreationTime", pGovObj->GetCreationTime());
            const CTxIn& masternodeVin = pGovObj->GetMasternodeVin();
            if(masternodeVin != CTxIn()) {
                bObj.pushKV("SigningMasternode", masternodeVin.prevout.ToStringShort());
            }

            // REPORT STATUS FOR FUNDING VOTES SPECIFICALLY
            bObj.pushKV("AbsoluteYesCount",  pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING));
            bObj.pushKV("YesCount",  pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING));
            bObj.pushKV("NoCount",  pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING));
            bObj.pushKV("AbstainCount",  pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

            // REPORT VALIDITY AND CACHING FLAGS FOR VARIOUS SETTINGS
            std::string strError = "";
            bObj.pushKV("fBlockchainValidity",  pGovObj->IsValidLocally(strError, false));
            bObj.pushKV("IsValidReason",  strError.c_str());
            bObj.pushKV("fCachedValid",  pGovObj->IsSetCachedValid());
            bObj.pushKV("fCachedFunding",  pGovObj->IsSetCachedFunding());
            bObj.pushKV("fCachedDelete",  pGovObj->IsSetCachedDelete());
            bObj.pushKV("fCachedEndorsed",  pGovObj->IsSetCachedEndorsed());

            objResult.pushKV(pGovObj->GetHash().ToString(), bObj);
        }

        return objResult;
    }

    // GET SPECIFIC GOVERNANCE ENTRY
    if(strCommand == "get")
    {
        if (request.params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gobject get <governance-hash>'");

        // COLLECT VARIABLES FROM OUR USER
        uint256 hash = ParseHashV(request.params[1], "GovObj hash");

        LOCK2(cs_main, governance.cs);

        // FIND THE GOVERNANCE OBJECT THE USER IS LOOKING FOR
        CGovernanceObject* pGovObj = governance.FindGovernanceObject(hash);

        if(pGovObj == NULL)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown governance object");

        // REPORT BASIC OBJECT STATS

        UniValue objResult(UniValue::VOBJ);
        objResult.pushKV("DataHex",  pGovObj->GetDataAsHex());
        objResult.pushKV("DataString",  pGovObj->GetDataAsString());
        objResult.pushKV("Hash",  pGovObj->GetHash().ToString());
        objResult.pushKV("CollateralHash",  pGovObj->GetCollateralHash().ToString());
        objResult.pushKV("ObjectType", pGovObj->GetObjectType());
        objResult.pushKV("CreationTime", pGovObj->GetCreationTime());
        const CTxIn& masternodeVin = pGovObj->GetMasternodeVin();
        if(masternodeVin != CTxIn()) {
            objResult.pushKV("SigningMasternode", masternodeVin.prevout.ToStringShort());
        }

        // SHOW (MUCH MORE) INFORMATION ABOUT VOTES FOR GOVERNANCE OBJECT (THAN LIST/DIFF ABOVE)
        // -- FUNDING VOTING RESULTS

        UniValue objFundingResult(UniValue::VOBJ);
        objFundingResult.pushKV("AbsoluteYesCount",  pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING));
        objFundingResult.pushKV("YesCount",  pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING));
        objFundingResult.pushKV("NoCount",  pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING));
        objFundingResult.pushKV("AbstainCount",  pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));
        objResult.pushKV("FundingResult", objFundingResult);

        // -- VALIDITY VOTING RESULTS
        UniValue objValid(UniValue::VOBJ);
        objValid.pushKV("AbsoluteYesCount",  pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_VALID));
        objValid.pushKV("YesCount",  pGovObj->GetYesCount(VOTE_SIGNAL_VALID));
        objValid.pushKV("NoCount",  pGovObj->GetNoCount(VOTE_SIGNAL_VALID));
        objValid.pushKV("AbstainCount",  pGovObj->GetAbstainCount(VOTE_SIGNAL_VALID));
        objResult.pushKV("ValidResult", objValid);

        // -- DELETION CRITERION VOTING RESULTS
        UniValue objDelete(UniValue::VOBJ);
        objDelete.pushKV("AbsoluteYesCount",  pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_DELETE));
        objDelete.pushKV("YesCount",  pGovObj->GetYesCount(VOTE_SIGNAL_DELETE));
        objDelete.pushKV("NoCount",  pGovObj->GetNoCount(VOTE_SIGNAL_DELETE));
        objDelete.pushKV("AbstainCount",  pGovObj->GetAbstainCount(VOTE_SIGNAL_DELETE));
        objResult.pushKV("DeleteResult", objDelete);

        // -- ENDORSED VIA MASTERNODE-ELECTED BOARD
        UniValue objEndorsed(UniValue::VOBJ);
        objEndorsed.pushKV("AbsoluteYesCount",  pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_ENDORSED));
        objEndorsed.pushKV("YesCount",  pGovObj->GetYesCount(VOTE_SIGNAL_ENDORSED));
        objEndorsed.pushKV("NoCount",  pGovObj->GetNoCount(VOTE_SIGNAL_ENDORSED));
        objEndorsed.pushKV("AbstainCount",  pGovObj->GetAbstainCount(VOTE_SIGNAL_ENDORSED));
        objResult.pushKV("EndorsedResult", objEndorsed);

        // --
        std::string strError = "";
        objResult.pushKV("fLocalValidity",  pGovObj->IsValidLocally(strError, false));
        objResult.pushKV("IsValidReason",  strError.c_str());
        objResult.pushKV("fCachedValid",  pGovObj->IsSetCachedValid());
        objResult.pushKV("fCachedFunding",  pGovObj->IsSetCachedFunding());
        objResult.pushKV("fCachedDelete",  pGovObj->IsSetCachedDelete());
        objResult.pushKV("fCachedEndorsed",  pGovObj->IsSetCachedEndorsed());
        return objResult;
    }

    // GETVOTES FOR SPECIFIC GOVERNANCE OBJECT
    if(strCommand == "getvotes")
    {
        if (request.params.size() != 2)
            throw std::runtime_error(
                "Correct usage is 'gobject getvotes <governance-hash>'"
                );

        // COLLECT PARAMETERS FROM USER

        uint256 hash = ParseHashV(request.params[1], "Governance hash");

        // FIND OBJECT USER IS LOOKING FOR

        LOCK(governance.cs);

        CGovernanceObject* pGovObj = governance.FindGovernanceObject(hash);

        if(pGovObj == NULL) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown governance-hash");
        }

        // REPORT RESULTS TO USER

        UniValue bResult(UniValue::VOBJ);

        // GET MATCHING VOTES BY HASH, THEN SHOW USERS VOTE INFORMATION

        std::vector<CGovernanceVote> vecVotes = governance.GetMatchingVotes(hash);
        for (CGovernanceVote vote : vecVotes) {
            bResult.pushKV(vote.GetHash().ToString(),  vote.ToString());
        }

        return bResult;
    }

    // GETVOTES FOR SPECIFIC GOVERNANCE OBJECT
    if(strCommand == "getcurrentvotes")
    {
        if (request.params.size() != 2 && request.params.size() != 4)
            throw std::runtime_error(
                "Correct usage is 'gobject getcurrentvotes <governance-hash> [txid vout_index]'"
                );

        // COLLECT PARAMETERS FROM USER

        uint256 hash = ParseHashV(request.params[1], "Governance hash");

        COutPoint mnCollateralOutpoint;
        if (request.params.size() == 4) {
            uint256 txid = ParseHashV(request.params[2], "Masternode Collateral hash");
            std::string strVout = request.params[3].get_str();
            uint32_t vout = boost::lexical_cast<uint32_t>(strVout);
            mnCollateralOutpoint = COutPoint(txid, vout);
        }

        // FIND OBJECT USER IS LOOKING FOR

        LOCK(governance.cs);

        CGovernanceObject* pGovObj = governance.FindGovernanceObject(hash);

        if(pGovObj == NULL) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown governance-hash");
        }

        // REPORT RESULTS TO USER

        UniValue bResult(UniValue::VOBJ);

        // GET MATCHING VOTES BY HASH, THEN SHOW USERS VOTE INFORMATION

        std::vector<CGovernanceVote> vecVotes = governance.GetCurrentVotes(hash, mnCollateralOutpoint);
        for (CGovernanceVote vote : vecVotes) {
            bResult.pushKV(vote.GetHash().ToString(),  vote.ToString());
        }

        return bResult;
    }

    return NullUniValue;
}

UniValue voteraw(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 7)
        throw std::runtime_error(
            RPCHelpMan{"voteraw",
                "\nCompile and relay a governance vote with provided external signature instead of signing vote internally\n",
                {
                    {"masternode-tx-hash", RPCArg::Type::STR, RPCArg::Optional::NO, "Masternode TX hash."},
                    {"masternode-tx-index", RPCArg::Type::NUM, RPCArg::Optional::NO, "Masternode TX index."},
                    {"governance-hash", RPCArg::Type::STR, RPCArg::Optional::NO, "Governance hash."},
                    {"vote-signal", RPCArg::Type::STR, RPCArg::Optional::NO, "Vote signal."},
                    {"vote-outcome", RPCArg::Type::STR, RPCArg::Optional::NO, "Vote outcome, must be one of:\n"
            "       \"yes\"\n"
            "       \"no\"\n"
            "       \"abstain\""},
                    {"time", RPCArg::Type::NUM, RPCArg::Optional::NO, "Time."},
                    {"vote-sig", RPCArg::Type::STR, RPCArg::Optional::NO, "Vote signature."},
                },
                RPCResult{
            "Not documented by Dash developers.\n"
                },
                RPCExamples{
                    HelpExampleCli("voteraw", "<masternode-tx-hash> <masternode-tx-index> <governance-hash> <vote-signal> [yes|no|abstain] <time> <vote-sig>")
            + HelpExampleRpc("voteraw", "\"masternode-tx-hash\" masternode-tx-index \"governance-hash\" \"vote-signal\" \"yes|no|abstain\" time \"vote-sig\"")
                },
            }.ToString());

    uint256 hashMnTx = ParseHashV(request.params[0], "mn tx hash");
    int nMnTxIndex = request.params[1].get_int();
    COutPoint outpoint = COutPoint(hashMnTx, nMnTxIndex);

    uint256 hashGovObj = ParseHashV(request.params[2], "Governance hash");
    std::string strVoteSignal = request.params[3].get_str();
    std::string strVoteOutcome = request.params[4].get_str();

    vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(strVoteSignal);
    if(eVoteSignal == VOTE_SIGNAL_NONE)  {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid vote signal. Please using one of the following: "
                           "(funding|valid|delete|endorsed) OR `custom sentinel code` ");
    }

    vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(strVoteOutcome);
    if(eVoteOutcome == VOTE_OUTCOME_NONE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid vote outcome. Please use one of the following: 'yes', 'no' or 'abstain'");
    }

    int64_t nTime = request.params[5].get_int64();
    std::string strSig = request.params[6].get_str();
    bool fInvalid = false;
    std::vector<unsigned char> vchSig = DecodeBase64(strSig.c_str(), &fInvalid);

    if (fInvalid) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");
    }

    CMasternode mn;
    bool fMnFound = mnodeman.Get(outpoint, mn);

    if(!fMnFound) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failure to find masternode in list : " + outpoint.ToStringShort());
    }

    CGovernanceVote vote(outpoint, hashGovObj, eVoteSignal, eVoteOutcome);
    vote.SetTime(nTime);
    vote.SetSignature(vchSig);

    if(!vote.IsValid(true)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failure to verify vote.");
    }

    CGovernanceException exception;
    if(governance.ProcessVoteAndRelay(vote, exception, *g_connman)) {
        return "Voted successfully";
    }
    else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Error voting : " + exception.GetMessage());
    }
}

UniValue getgovernanceinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            RPCHelpMan{"getgovernanceinfo",
                "\nReturns an object containing governance parameters.\n",
                {},
                RPCResult{
            "{\n"
            "  \"governanceminquorum\": xxxxx,           (numeric) the absolute minimum number of votes needed to trigger a governance action\n"
            "  \"masternodewatchdogmaxseconds\": xxxxx,  (numeric) sentinel watchdog expiration time in seconds\n"
            "  \"proposalfee\": xxx.xx,                  (numeric) the collateral transaction fee which must be paid to create a proposal in " + CURRENCY_UNIT + "\n"
            "  \"superblockcycle\": xxxxx,               (numeric) the number of blocks between superblocks\n"
            "  \"lastsuperblock\": xxxxx,                (numeric) the block number of the last superblock\n"
            "  \"nextsuperblock\": xxxxx,                (numeric) the block number of the next superblock\n"
            "  \"maxgovobjdatasize\": xxxxx,             (numeric) maximum governance object data size in bytes\n"
            "}"
                },
                RPCExamples{
                    HelpExampleCli("getgovernanceinfo", "")
            + HelpExampleRpc("getgovernanceinfo", "")
                },
            }.ToString());
    }

    // Compute last/next superblock
    int nLastSuperblock, nNextSuperblock;

    // Get current block height
    int nBlockHeight = 0;
    {
        LOCK(cs_main);
        nBlockHeight = (int)chainActive.Height();
    }

    // Get chain parameters
    int nSuperblockStartBlock = Params().GetConsensus().nSuperblockStartBlock;
    int nSuperblockCycle = Params().GetConsensus().nSuperblockCycle;

    // Get first superblock
    int nFirstSuperblockOffset = (nSuperblockCycle - nSuperblockStartBlock % nSuperblockCycle) % nSuperblockCycle;
    int nFirstSuperblock = nSuperblockStartBlock + nFirstSuperblockOffset;

    if(nBlockHeight < nFirstSuperblock){
        nLastSuperblock = 0;
        nNextSuperblock = nFirstSuperblock;
    } else {
        nLastSuperblock = nBlockHeight - nBlockHeight % nSuperblockCycle;
        nNextSuperblock = nLastSuperblock + nSuperblockCycle;
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("governanceminquorum", Params().GetConsensus().nGovernanceMinQuorum);
    obj.pushKV("masternodewatchdogmaxseconds", MASTERNODE_WATCHDOG_MAX_SECONDS);
    obj.pushKV("proposalfee", ValueFromAmount(GOVERNANCE_PROPOSAL_FEE_TX));
    obj.pushKV("superblockcycle", Params().GetConsensus().nSuperblockCycle);
    obj.pushKV("lastsuperblock", nLastSuperblock);
    obj.pushKV("nextsuperblock", nNextSuperblock);
    obj.pushKV("maxgovobjdatasize", MAX_GOVERNANCE_OBJECT_DATA_SIZE);

    return obj;
}

UniValue getsuperblockbudget(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            RPCHelpMan{"getsuperblockbudget",
                "\nReturns the absolute maximum sum of superblock payments allowed.\n",
                {
                    {"index", RPCArg::Type::NUM, RPCArg::Optional::NO, "The block index."},
                },
                RPCResult{
            "n                (numeric) The absolute maximum sum of superblock payments allowed, in " + CURRENCY_UNIT + "\n"
                },
                RPCExamples{
                    HelpExampleCli("getsuperblockbudget", "1000")
            + HelpExampleRpc("getsuperblockbudget", "1000")
                },
            }.ToString());
    }

    int nBlockHeight = request.params[0].get_int();
    if (nBlockHeight < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
    }

    CBlockHeader pblock;
    pblock.nBits = 1;

    CAmount nBudget = CSuperblock::GetPaymentsLimit(nBlockHeight, pblock);
    std::string strBudget = FormatMoney(nBudget);

    return strBudget;
}

// Dash
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "dash",               "gobject",                &gobject,                {"command"}  },
    { "dash",               "getgovernanceinfo",      &getgovernanceinfo,      {}  },
    { "dash",               "getsuperblockbudget",    &getsuperblockbudget,    {"index"}  },
    { "dash",               "voteraw",                &voteraw,                {"masternode-tx-hash", "masternode-tx-index", "governance-hash", "vote-signal", "yes-no-abstain", "time", "vote-sig"}  },
};

void RegisterDashGovernanceRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
//
