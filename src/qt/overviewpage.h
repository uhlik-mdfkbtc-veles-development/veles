// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_OVERVIEWPAGE_H
#define BITCOIN_QT_OVERVIEWPAGE_H

#include <interfaces/wallet.h>

#include <QWidget>
#include <memory>

class ClientModel;
class TransactionFilterProxy;
class TxViewDelegate;
class PlatformStyle;
class WalletModel;

namespace Ui {
    class OverviewPage;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Overview ("home") page widget */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~OverviewPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void showOutOfSyncWarning(bool fShow);

public Q_SLOTS:
    // PRIVATESEND BEGIN
    void privateSendStatus(const interfaces::WalletBalances& balances);
    void setBalance(const interfaces::WalletBalances& balances);
    // PRIVATESEND END

Q_SIGNALS:
    void transactionClicked(const QModelIndex &index);
    void outOfSyncWarningClicked();

private:
    // PRIVATESEND BEGIN
    QTimer *timer;
    // PRIVATESEND END
    Ui::OverviewPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    interfaces::WalletBalances m_balances;
    bool fShowAdvancedPSUI; //PRIVATESEND

    TxViewDelegate *txdelegate;
    std::unique_ptr<TransactionFilterProxy> filter;
    void DisablePrivateSendCompletely(); // PRIVATESEND
    void SetupTransactionList(int nNumItems); // PRIVATESEND


private Q_SLOTS:
    // PRIVATESEND START
    void togglePrivateSend(const interfaces::WalletBalances& balances);
    void privateSendAuto();
    void privateSendReset();
    void privateSendInfo();
    //void updatePrivateSendProgress();
    void updatePrivateSendProgress(const interfaces::WalletBalances& balances);
    void updateAdvancedPSUI(bool fShowAdvancedPSUI);
    // PRIVATESEND END
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex &index);
    void updateAlerts(const QString &warnings);
    void updateWatchOnlyLabels(bool showWatchOnly);
    void handleOutOfSyncWarningClicks();
};

#endif // BITCOIN_QT_OVERVIEWPAGE_H
