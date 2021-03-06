/***************************************************************************
 *   Copyright (C) 2010 by Xavier Lefage                                   *
 *   xavier.kwooty@gmail.com                                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "kconfiggrouphandler.h"

#include <KGlobal>
#include "kwooty_debug.h"
#include <KMessageBox>

#include <QApplication>

#include "mainwindow.h"
#include "core.h"
#include "kwootysettings.h"
#include "preferences/preferencesserver.h"
#include "utilities/utility.h"
using namespace UtilityNamespace;

KConfigGroupHandler::KConfigGroupHandler(MainWindow *mainwindow) : QObject(qApp)
{

    mMainWindow = mainwindow;
    mWallet = 0;
    mDialogButtonCode = 0;
    mUseKwallet = Settings::useKwallet();

    instance = this;

}

KConfigGroupHandler *KConfigGroupHandler::instance = 0;
KConfigGroupHandler *KConfigGroupHandler::getInstance()
{
    return instance;
}

KConfigGroupHandler::~KConfigGroupHandler()
{
    if (mWallet) {
        delete mWallet;
    }
}

//======================================================================================//
//                                    SLOTS :                                           //
//======================================================================================//

void KConfigGroupHandler::settingsChangedSlot()
{

    // switch passwords between config file and kwallet :
    if (mUseKwallet != Settings::useKwallet() && openWallet()) {

        // avoid to display dialog box during synchronization :
        mDialogButtonCode = KMessageBox::Ok;

        for (int serverId = 0; serverId < readServerNumberSettings(); serverId++) {

            // set old value right now :
            mUseKwallet = !Settings::useKwallet();

            KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Server_%1").arg(serverId));

            // read password from plain text or kwallet :
            QString password = readPassword(serverId, configGroup);

            // set new value right now :
            mUseKwallet = Settings::useKwallet();

            // write password kwallet or plain text :
            writePassword(serverId, configGroup, password);

            configGroup.sync();

        }

        // store new setting value :
        mUseKwallet = Settings::useKwallet();

        mDialogButtonCode = 0;
    }

}

void KConfigGroupHandler::walletClosedSlot()
{
    // if wallet is closed, delete the current one to reopen later a new one if needed :
    delete mWallet;
}

//======================================================================================//
//                                    KWallet :                                         //
//======================================================================================//

void KConfigGroupHandler::openWalletFails()
{

    if (mDialogButtonCode == 0) {

        mDialogButtonCode = KMessageBox::Cancel;
        mDialogButtonCode = KMessageBox::messageBox(0,
                                 KMessageBox::Sorry,
                                 i18n("No running KWallet found. Passwords will be saved in plaintext."),
                                 i18n("KWallet is not running"));

        // disable kwallet usage :
        Settings::setUseKwallet(false);
        mUseKwallet = Settings::useKwallet();

        if (mDialogButtonCode == KMessageBox::Ok) {
            mDialogButtonCode = 0;
        }

    }

}

bool KConfigGroupHandler::openWallet()
{

    bool walletOpen = false;

    if (mMainWindow) {

        // open the wallet if not open :
        if (!mWallet) {

            mWallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), mMainWindow->winId());

            // be notified that the wallet has been closed :
            connect(mWallet, SIGNAL(walletClosed()), this, SLOT(walletClosedSlot()));
        }

        // if wallet has been successfully open :
        if (mWallet) {

            // check if kwooty's entry exists :
            QString entryStr = "kwooty";

            // check if wallet is open and current folder properly set :
            if (mWallet->isOpen() &&
                    mWallet->currentFolder() == entryStr) {
                walletOpen = true;
            }
            // create and set wallet in proper folder :
            else {

                // create the entry if it not already exists :
                if (!mWallet->hasEntry(entryStr)) {
                    mWallet->createFolder(entryStr);
                }

                // set the current working folder :
                if (!mWallet->hasEntry(entryStr)) {
                    walletOpen = mWallet->setFolder(entryStr);
                }
            }
        }
        // wallet can not be open :
        else {
            walletOpen = false;
        }

    }

    if (!walletOpen) {
        openWalletFails();

    }

    return walletOpen;

}

QString KConfigGroupHandler::readPassword(const int &serverId, KConfigGroup &configGroup)
{

    QString password = "";

    // read password from kwallet if enabled in settings :
    if (mUseKwallet) {

        if (openWallet()) {

            QString passwordAlias = QString("PasswordServer_%1").arg(serverId);
            mWallet->readPassword(passwordAlias, password);
        }

    }
    // read password from kconfig file :
    else {
        password = configGroup.readEntry("password", QString());
    }

    return password;
}

void KConfigGroupHandler::writePassword(const int &serverId, KConfigGroup &configGroup, const QString &password)
{

    // write password to kwallet if enabled in settings :
    if (mUseKwallet) {

        if (openWallet()) {

            QString passwordAlias = QString("PasswordServer_%1").arg(serverId);
            int writeSuccessful = mWallet->writePassword(passwordAlias, password);

            // password has been successfully written in wallet, remove eventual plain text password :
            if (writeSuccessful == 0) {
                removePasswordEntry(configGroup);
            }
        }
    }
    // write password to kconfig file :
    else {
        configGroup.writeEntry("password", password);
    }

}

//======================================================================================//
//                              Multi-server settings :                                 //
//======================================================================================//

ServerData KConfigGroupHandler::readServerSettings(int serverId, const PasswordData &passwordData)
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Server_%1").arg(serverId));

    // if KConfigGroup is invalid, it may be the first launch from a previous kwooty version
    // try get previous settings :
    bool firstLaunchFromPreviousVersion = false;

    if ((serverId == MasterServer) && !configGroup.exists()) {

        configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("server"));
        firstLaunchFromPreviousVersion = true;
    }

    // read data from config file and kwallet :
    ServerData serverData = fillServerData(serverId, configGroup, passwordData);

    // remove previous kwooty version server group if any :
    if (firstLaunchFromPreviousVersion) {
        configGroup.deleteGroup();
    }

    return serverData;

}

ServerData KConfigGroupHandler::fillServerData(const int &serverId, KConfigGroup &configGroup, const PasswordData &passwordData)
{

    ServerData serverData;

    serverData.setServerId(configGroup.readEntry("serverId", serverId));
    serverData.setHostName(configGroup.readEntry("hostName", QString()));
    serverData.setServerName(configGroup.readEntry("serverName", QString()).remove("&"));
    serverData.setPort(configGroup.readEntry("port", 119));
    serverData.setConnectionNumber(configGroup.readEntry("connectionNumber", 4));
    serverData.setAuthentication(configGroup.readEntry("authentication", false));
    serverData.setLogin(configGroup.readEntry("login", QString()));
    serverData.setDisconnectTimeout(configGroup.readEntry("disconnectTimeout", 5));
    serverData.setEnableSSL(configGroup.readEntry("enableSSL", false));
    serverData.setServerModeIndex(configGroup.readEntry("serverModeIndex", 0));

    // read password with Kwallet or kconfig file :
    if (passwordData == ReadPasswordData) {
        serverData.setPassword(readPassword(serverId, configGroup));
    }

    // if this is the first kwooty launch, config file is new and server name will be empty
    // set serverName to Master for first Id :
    if ((serverId == MasterServer) && serverData.getServerName().isEmpty()) {
        serverData.setServerName(i18n("Master"));
    }

    return serverData;

}

void KConfigGroupHandler::writeServerSettings(int serverId, ServerData serverData)
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Server_%1").arg(serverId));

    configGroup.writeEntry("serverId", serverData.getServerId());
    configGroup.writeEntry("serverName", serverData.getServerName());
    configGroup.writeEntry("hostName", serverData.getHostName());
    configGroup.writeEntry("port", serverData.getPort());
    configGroup.writeEntry("connectionNumber", serverData.getConnectionNumber());
    configGroup.writeEntry("authentication", serverData.isAuthentication());
    configGroup.writeEntry("login", serverData.getLogin());
    configGroup.writeEntry("disconnectTimeout", serverData.getDisconnectTimeout());
    configGroup.writeEntry("enableSSL", serverData.isEnableSSL());
    configGroup.writeEntry("serverModeIndex", serverData.getServerModeIndex());

    // write password with Kwallet or kconfig file :
    writePassword(serverId, configGroup, serverData.getPassword());

    configGroup.sync();

}

void KConfigGroupHandler::writeServerNumberSettings(const int &serverNumber)
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("NumberOfServers"));
    configGroup.writeEntry("serverNumber", serverNumber);
    configGroup.sync();
}

int KConfigGroupHandler::readServerNumberSettings()
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("NumberOfServers"));
    int serverNumber = configGroup.readEntry("serverNumber", 1);

    return qMin(UtilityNamespace::MAX_SERVERS, serverNumber);

}

void KConfigGroupHandler::removeServerSettings(const int &serverId)
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Server_%1").arg(serverId));

    if (configGroup.exists()) {
        configGroup.deleteGroup();
    }

}

void KConfigGroupHandler::removePasswordEntry(KConfigGroup &configGroup)
{

    QString passwordStr = "password";

    // check if password entry exists :
    if (configGroup.hasKey(passwordStr)) {
        configGroup.deleteEntry(passwordStr);
    }

}

int KConfigGroupHandler::serverConnectionNumber(const int &serverId)
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Server_%1").arg(serverId));
    return configGroup.readEntry("connectionNumber", 4);

}

QString KConfigGroupHandler::tabName(const int &serverId, const QString &tabText)
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Server_%1").arg(serverId));
    return configGroup.readEntry("serverName", tabText);
}

//======================================================================================//
//                              Side bar states :                                       //
//======================================================================================//

void KConfigGroupHandler::writeSideBarDisplay(const bool &display)
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("SideBar"));
    configGroup.writeEntry("sideBarDisplay", display);
    configGroup.sync();
}

bool KConfigGroupHandler::readSideBarDisplay()
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("SideBar"));
    return configGroup.readEntry("sideBarDisplay", false);

}

void KConfigGroupHandler::writeSideBarTabOnlyDisplay(const bool &tabOnlyDisplay)
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("SideBar"));
    configGroup.writeEntry("sideBarTabOnlyDisplay", tabOnlyDisplay);
    configGroup.sync();
}

bool KConfigGroupHandler::readSideBarTabOnlyDisplay()
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("SideBar"));
    return configGroup.readEntry("sideBarTabOnlyDisplay", false);

}

void KConfigGroupHandler::writeSideBarServerIndex(const int &index)
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("SideBar"));
    configGroup.writeEntry("sideBarServerIndex", index);
    configGroup.sync();
}

int KConfigGroupHandler::readSideBarServerIndex()
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("SideBar"));

    // be sure that returned index is not negative :
    return qMax(configGroup.readEntry("sideBarServerIndex", 0), 0);

}

//======================================================================================//
//                              mainwindow display :                                    //
//======================================================================================//
bool KConfigGroupHandler::readMainWindowHiddenOnExit()
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("MainWindow"));
    return configGroup.readEntry("MainWindowHiddenOnExit", true);

}

void KConfigGroupHandler::writeMainWindowHiddenOnExit(const bool &hiddenOnExit)
{

    KConfigGroup configGroup = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("MainWindow"));
    configGroup.writeEntry("MainWindowHiddenOnExit", hiddenOnExit);
    configGroup.sync();
}

